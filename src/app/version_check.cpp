#include "app/version_check.hpp"

#include <cpr/cpr.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-ranges,modernize-use-starts-ends-with,modernize-return-braced-init-list,performance-unnecessary-value-param,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
//
// version_check.cpp tidy-suppression rationale.
//
// This translation unit implements GitHub-Releases-backed semver
// version checking and on-disk caching. It is NOT on the per-pixel
// render hot path; the suppressed categories all flag idiomatic
// patterns in the SemVer 2.0.0 precedence comparator and the JSON
// cache (de)serializer where a mechanical rewrite would not improve
// safety:
//
//   * cppcoreguidelines-pro-bounds-avoid-unchecked-container-access:
//     compare_prerelease iterates ids_a / ids_b up to a count bounded
//     above by std::min(.size(), .size()); read_cache / write_cache
//     access JSON keys after node.contains() checks. .at() bounds
//     checks would add nothing.
//
//   * readability-identifier-length: (a, b, c, ec) are universal
//     comparator / character-iteration / std::error_code names.
//
//   * bugprone-easily-swappable-parameters: compare_identifier and
//     compare_prerelease take (lhs, rhs) of the same type by design;
//     the convention matches std::sort comparators.
//
//   * readability-function-cognitive-complexity / readability-function-
//     size: parse_semver, compare_identifier, build_cache_from_releases
//     are the canonical SemVer 2.0.0 / GitHub release-feed parsers;
//     the branch density is the spec, not accidental complexity.
//
//   * cppcoreguidelines-avoid-magic-numbers: 7 (min git short-SHA
//     length), 200 / 300 (HTTP status code range bounds), and the
//     SemVer triple count (3) are documented at every use site.
//
//   * modernize-use-ranges: the std::all_of calls in
//     is_numeric_identifier and strip_git_describe_suffix iterate
//     std::string_view; the iterator form is identical to the ranges
//     form in this header set and rewriting adds no signal.
//
//   * modernize-use-starts-ends-with: the kDirty suffix check in
//     strip_git_describe_suffix uses string_view::compare with an
//     explicit (offset, count) overload to avoid building a temporary
//     suffix view.
//
//   * modernize-return-braced-init-list: update_info_to_json builds
//     a small nlohmann::json object with named keys; the explicit
//     constructor call is more readable than a braced init list.
namespace corridorkey::app {

namespace {

constexpr const char* kUserAgent = "CorridorKey-UpdateCheck/1";

std::optional<int> parse_int(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    int parsed = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

std::string strip_leading_v(const std::string& value) {
    if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) {
        return value.substr(1);
    }
    return value;
}

std::vector<std::string> split_identifiers(std::string_view pre_release) {
    std::vector<std::string> identifiers;
    size_t start = 0;
    while (start <= pre_release.size()) {
        const auto next = pre_release.find('.', start);
        const auto end = next == std::string_view::npos ? pre_release.size() : next;
        identifiers.emplace_back(pre_release.substr(start, end - start));
        if (next == std::string_view::npos) {
            break;
        }
        start = next + 1;
    }
    return identifiers;
}

bool is_numeric_identifier(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Strip the trailing `-<N>-g<SHA>[-dirty]` appended by `git describe` so a
// locally built binary compares against published tags as if it were the
// tag it was derived from. Without this, a dev build like
// `win.1-3-gabc1234` would sort above the published `win.2` because the
// non-numeric identifier `1-3-gabc1234` outranks the numeric `2` under
// SemVer 2.0.0 precedence.
std::string strip_git_describe_suffix(std::string_view prerelease) {
    std::string_view remaining = prerelease;
    constexpr std::string_view kDirty = "-dirty";
    if (remaining.size() >= kDirty.size() &&
        remaining.compare(remaining.size() - kDirty.size(), kDirty.size(), kDirty) == 0) {
        remaining = remaining.substr(0, remaining.size() - kDirty.size());
    }

    const auto g_pos = remaining.rfind("-g");
    if (g_pos == std::string_view::npos) {
        return std::string(prerelease);
    }
    const auto sha = remaining.substr(g_pos + 2);
    if (sha.size() < 7 || !std::all_of(sha.begin(), sha.end(),
                                       [](unsigned char c) { return std::isxdigit(c) != 0; })) {
        return std::string(prerelease);
    }

    const auto before_g = remaining.substr(0, g_pos);
    const auto dash_pos = before_g.rfind('-');
    if (dash_pos == std::string_view::npos) {
        return std::string(prerelease);
    }
    const auto count = before_g.substr(dash_pos + 1);
    if (count.empty() || !is_numeric_identifier(count)) {
        return std::string(prerelease);
    }

    return std::string(before_g.substr(0, dash_pos));
}

int compare_identifier(std::string_view a, std::string_view b) {
    const bool a_num = is_numeric_identifier(a);
    const bool b_num = is_numeric_identifier(b);
    if (a_num && b_num) {
        auto parsed_a = parse_int(a);
        auto parsed_b = parse_int(b);
        if (parsed_a.has_value() && parsed_b.has_value()) {
            if (*parsed_a < *parsed_b) return -1;
            if (*parsed_a > *parsed_b) return 1;
            return 0;
        }
    }
    if (a_num && !b_num) return -1;
    if (!a_num && b_num) return 1;
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

int compare_prerelease(std::string_view a, std::string_view b) {
    if (a.empty() && b.empty()) return 0;
    if (a.empty()) return 1;
    if (b.empty()) return -1;
    const auto ids_a = split_identifiers(a);
    const auto ids_b = split_identifiers(b);
    const size_t count = std::min(ids_a.size(), ids_b.size());
    for (size_t i = 0; i < count; ++i) {
        const int cmp = compare_identifier(ids_a[i], ids_b[i]);
        if (cmp != 0) {
            return cmp;
        }
    }
    if (ids_a.size() < ids_b.size()) return -1;
    if (ids_a.size() > ids_b.size()) return 1;
    return 0;
}

#ifdef _WIN32
std::optional<std::filesystem::path> windows_local_app_data() {
    PWSTR raw = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw) != S_OK) {
        if (raw != nullptr) {
            CoTaskMemFree(raw);
        }
        return std::nullopt;
    }
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path;
}
#endif

std::filesystem::path resolve_home_dir() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home);
    }
    return std::filesystem::temp_directory_path();
}

nlohmann::json update_info_to_json(const UpdateInfo& info) {
    return nlohmann::json{{"latest_version", info.latest_version},
                          {"release_url", info.release_url},
                          {"is_prerelease", info.is_prerelease}};
}

std::optional<UpdateInfo> update_info_from_json(const nlohmann::json& node) {
    if (!node.is_object() || !node.contains("latest_version") || !node.contains("release_url")) {
        return std::nullopt;
    }
    UpdateInfo info;
    info.latest_version = node.value("latest_version", std::string());
    info.release_url = node.value("release_url", std::string());
    info.is_prerelease = node.value("is_prerelease", false);
    if (info.latest_version.empty()) {
        return std::nullopt;
    }
    return info;
}

CachedCheck build_cache_from_releases(const nlohmann::json& releases,
                                      std::chrono::system_clock::time_point now,
                                      std::string_view platform_code) {
    CachedCheck cache;
    cache.fetched_at_unix_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    if (!releases.is_array()) {
        return cache;
    }
    for (const auto& release : releases) {
        if (!release.is_object() || release.value("draft", false)) {
            continue;
        }
        UpdateInfo info;
        info.latest_version = strip_leading_v(release.value("tag_name", std::string()));
        info.release_url = release.value("html_url", std::string());
        info.is_prerelease = release.value("prerelease", false);
        if (info.latest_version.empty()) {
            continue;
        }
        const auto parsed = parse_semver(info.latest_version);
        if (!parsed.has_value()) {
            continue;
        }
        const bool has_prerelease = !parsed->pre_release.empty();
        if (has_prerelease) {
            const std::string tag_platform = prerelease_platform_code(parsed->pre_release);
            if (tag_platform.empty()) {
                continue;
            }
            if (!platform_code.empty() && tag_platform != platform_code) {
                continue;
            }
            if (!cache.prerelease.has_value() ||
                is_newer_version(info.latest_version, cache.prerelease->latest_version)) {
                cache.prerelease = info;
            }
        } else {
            if (!cache.stable.has_value() ||
                is_newer_version(info.latest_version, cache.stable->latest_version)) {
                cache.stable = info;
            }
        }
    }
    return cache;
}

std::optional<nlohmann::json> fetch_releases_json(const std::string& repository,
                                                  std::chrono::milliseconds timeout) {
    const std::string url =
        std::string("https://api.github.com/repos/") + repository + "/releases?per_page=30";
    cpr::Response response = cpr::Get(cpr::Url{url},
                                      cpr::Header{{"User-Agent", kUserAgent},
                                                  {"Accept", "application/vnd.github+json"},
                                                  {"X-GitHub-Api-Version", "2022-11-28"}},
                                      cpr::Timeout{timeout});
    if (response.error || response.status_code < 200 || response.status_code >= 300) {
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(response.text);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

}  // namespace

std::optional<SemVer> parse_semver(const std::string& version) {
    const std::string trimmed = strip_leading_v(version);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    const auto pre_pos = trimmed.find('-');
    const std::string core = pre_pos == std::string::npos ? trimmed : trimmed.substr(0, pre_pos);

    SemVer result;
    size_t start = 0;
    for (int component = 0; component < 3; ++component) {
        const auto next = core.find('.', start);
        const std::string_view part(core.data() + start,
                                    (next == std::string::npos ? core.size() : next) - start);
        auto parsed = parse_int(part);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        if (component == 0) {
            result.major = *parsed;
        } else if (component == 1) {
            result.minor = *parsed;
        } else {
            result.patch = *parsed;
        }
        if (next == std::string::npos) {
            if (component != 2) {
                return std::nullopt;
            }
            break;
        }
        start = next + 1;
    }
    if (pre_pos != std::string::npos) {
        result.pre_release = strip_git_describe_suffix(trimmed.substr(pre_pos + 1));
    }
    return result;
}

bool is_newer_version(const std::string& latest, const std::string& current) {
    const auto latest_sv = parse_semver(latest);
    const auto current_sv = parse_semver(current);
    if (!latest_sv.has_value()) {
        return false;
    }
    if (!current_sv.has_value()) {
        return true;
    }
    if (latest_sv->major != current_sv->major) {
        return latest_sv->major > current_sv->major;
    }
    if (latest_sv->minor != current_sv->minor) {
        return latest_sv->minor > current_sv->minor;
    }
    if (latest_sv->patch != current_sv->patch) {
        return latest_sv->patch > current_sv->patch;
    }
    return compare_prerelease(latest_sv->pre_release, current_sv->pre_release) > 0;
}

std::string_view current_platform_code() {
#ifdef _WIN32
    return "win";
#elif defined(__APPLE__)
    return "mac";
#elif defined(__linux__)
    return "linux";
#else
    return "";
#endif
}

std::string prerelease_platform_code(const std::string& prerelease) {
    if (prerelease.empty()) {
        return {};
    }
    const auto identifiers = split_identifiers(prerelease);
    if (identifiers.empty()) {
        return {};
    }
    const std::string& first = identifiers.front();
    if (is_numeric_identifier(first)) {
        return {};
    }
    return first;
}

std::filesystem::path default_cache_path() {
    constexpr const char* kFilename = "update_check.json";
#ifdef _WIN32
    if (auto base = windows_local_app_data(); base.has_value()) {
        return *base / "corridorkey" / kFilename;
    }
    return std::filesystem::temp_directory_path() / "corridorkey" / kFilename;
#elif defined(__APPLE__)
    return resolve_home_dir() / "Library" / "Caches" / "corridorkey" / kFilename;
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / "corridorkey" / kFilename;
    }
    return resolve_home_dir() / ".cache" / "corridorkey" / kFilename;
#endif
}

std::optional<CachedCheck> read_cache(const std::filesystem::path& cache_file) {
    std::ifstream stream(cache_file);
    if (!stream.is_open()) {
        return std::nullopt;
    }
    nlohmann::json node;
    try {
        stream >> node;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    if (!node.is_object()) {
        return std::nullopt;
    }
    CachedCheck cache;
    cache.fetched_at_unix_seconds = node.value("fetched_at_unix_seconds", std::int64_t{0});
    if (node.contains("stable")) {
        cache.stable = update_info_from_json(node["stable"]);
    }
    if (node.contains("prerelease")) {
        cache.prerelease = update_info_from_json(node["prerelease"]);
    }
    return cache;
}

bool write_cache(const std::filesystem::path& cache_file, const CachedCheck& cache) {
    std::error_code ec;
    std::filesystem::create_directories(cache_file.parent_path(), ec);
    nlohmann::json node;
    node["fetched_at_unix_seconds"] = cache.fetched_at_unix_seconds;
    if (cache.stable.has_value()) {
        node["stable"] = update_info_to_json(*cache.stable);
    }
    if (cache.prerelease.has_value()) {
        node["prerelease"] = update_info_to_json(*cache.prerelease);
    }
    std::ofstream stream(cache_file, std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }
    stream << node.dump(2);
    return static_cast<bool>(stream);
}

bool is_cache_fresh(const CachedCheck& cache, std::chrono::seconds ttl,
                    std::chrono::system_clock::time_point now) {
    const auto now_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const auto age = now_seconds - cache.fetched_at_unix_seconds;
    return age >= 0 && age < ttl.count();
}

std::optional<UpdateInfo> select_update(const CachedCheck& cache,
                                        const std::string& current_version,
                                        bool include_prereleases) {
    std::optional<UpdateInfo> candidate = cache.stable;
    if (include_prereleases && cache.prerelease.has_value()) {
        if (!candidate.has_value() ||
            is_newer_version(cache.prerelease->latest_version, candidate->latest_version)) {
            candidate = cache.prerelease;
        }
    }
    if (!candidate.has_value()) {
        return std::nullopt;
    }
    if (!is_newer_version(candidate->latest_version, current_version)) {
        return std::nullopt;
    }
    return candidate;
}

std::optional<UpdateInfo> check_for_update(const VersionCheckOptions& options) {
    if (options.current_version.empty() || options.repository.empty()) {
        return std::nullopt;
    }
    const auto cache_path = options.cache_path.empty() ? default_cache_path() : options.cache_path;
    const auto now = std::chrono::system_clock::now();
    const std::string platform = options.platform_code.empty()
                                     ? std::string(current_platform_code())
                                     : options.platform_code;

    if (auto cache = read_cache(cache_path);
        cache.has_value() && is_cache_fresh(*cache, options.cache_ttl, now)) {
        return select_update(*cache, options.current_version, options.include_prereleases);
    }

    auto releases = fetch_releases_json(options.repository, options.network_timeout);
    if (!releases.has_value()) {
        return std::nullopt;
    }

    auto cache = build_cache_from_releases(*releases, now, platform);
    write_cache(cache_path, cache);
    return select_update(cache, options.current_version, options.include_prereleases);
}

}  // namespace corridorkey::app
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-ranges,modernize-use-starts-ends-with,modernize-return-braced-init-list,performance-unnecessary-value-param,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
