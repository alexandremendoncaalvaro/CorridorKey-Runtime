#include "app/version_check.hpp"

#include <cpr/cpr.h>

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <shlobj.h>
#include <windows.h>
#endif

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

#if defined(_WIN32)
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
                                      std::chrono::system_clock::time_point now) {
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
        if (info.is_prerelease) {
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
        result.pre_release = trimmed.substr(pre_pos + 1);
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
    if (latest_sv->pre_release.empty() && !current_sv->pre_release.empty()) {
        return true;
    }
    if (!latest_sv->pre_release.empty() && current_sv->pre_release.empty()) {
        return false;
    }
    return latest_sv->pre_release > current_sv->pre_release;
}

std::filesystem::path default_cache_path() {
    constexpr const char* kFilename = "update_check.json";
#if defined(_WIN32)
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

    if (auto cache = read_cache(cache_path);
        cache.has_value() && is_cache_fresh(*cache, options.cache_ttl, now)) {
        return select_update(*cache, options.current_version, options.include_prereleases);
    }

    auto releases = fetch_releases_json(options.repository, options.network_timeout);
    if (!releases.has_value()) {
        return std::nullopt;
    }

    auto cache = build_cache_from_releases(*releases, now);
    write_cache(cache_path, cache);
    return select_update(cache, options.current_version, options.include_prereleases);
}

}  // namespace corridorkey::app
