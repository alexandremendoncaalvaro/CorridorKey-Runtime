#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "app/version_check.hpp"

namespace {

//
// Test-file tidy-suppression rationale.
//
// Test fixtures legitimately use single-letter loop locals, magic
// numbers (resolution rungs, pixel coordinates, expected error counts),
// std::vector::operator[] on indices the test itself just constructed,
// and Catch2 / aggregate-init styles that pre-date the project's
// tightened .clang-tidy ruleset. The test source is verified
// behaviourally by ctest; converting every site to bounds-checked /
// designated-init / ranges form would obscure intent without changing
// what the tests prove. The same suppressions are documented and
// applied on the src/ tree where the underlying APIs live.
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)

std::filesystem::path temp_cache_path(const std::string& suffix) {
    auto dir = std::filesystem::temp_directory_path() / "corridorkey_version_check_tests";
    std::filesystem::create_directories(dir);
    return dir / ("cache_" + suffix + ".json");
}

}  // namespace

TEST_CASE("parse_semver accepts common formats", "[unit][version_check]") {
    using corridorkey::app::parse_semver;

    auto a = parse_semver("0.7.5");
    REQUIRE(a.has_value());
    REQUIRE(a->major == 0);
    REQUIRE(a->minor == 7);
    REQUIRE(a->patch == 5);
    REQUIRE(a->pre_release.empty());

    auto b = parse_semver("v1.2.3");
    REQUIRE(b.has_value());
    REQUIRE(b->major == 1);
    REQUIRE(b->pre_release.empty());

    auto c = parse_semver("0.7.5-rc.1");
    REQUIRE(c.has_value());
    REQUIRE(c->patch == 5);
    REQUIRE(c->pre_release == "rc.1");

    REQUIRE_FALSE(parse_semver("").has_value());
    REQUIRE_FALSE(parse_semver("garbage").has_value());
    REQUIRE_FALSE(parse_semver("1.2").has_value());
    REQUIRE_FALSE(parse_semver("1.a.3").has_value());
}

TEST_CASE("is_newer_version ordering", "[unit][version_check]") {
    using corridorkey::app::is_newer_version;

    REQUIRE(is_newer_version("0.7.6", "0.7.5"));
    REQUIRE(is_newer_version("0.8.0", "0.7.9"));
    REQUIRE(is_newer_version("1.0.0", "0.9.9"));
    REQUIRE_FALSE(is_newer_version("0.7.5", "0.7.5"));
    REQUIRE_FALSE(is_newer_version("0.7.4", "0.7.5"));

    REQUIRE(is_newer_version("0.7.5", "0.7.5-rc.1"));
    REQUIRE_FALSE(is_newer_version("0.7.5-rc.1", "0.7.5"));

    REQUIRE(is_newer_version("v0.7.6", "0.7.5"));
    REQUIRE(is_newer_version("0.7.6", "v0.7.5"));

    REQUIRE_FALSE(is_newer_version("garbage", "0.7.5"));
    REQUIRE(is_newer_version("0.7.6", "garbage"));
}

TEST_CASE("is_newer_version uses SemVer 2.0.0 numeric prerelease precedence",
          "[unit][version_check]") {
    using corridorkey::app::is_newer_version;

    REQUIRE(is_newer_version("0.7.5-win.22", "0.7.5-win.21"));
    REQUIRE(is_newer_version("0.7.5-win.11", "0.7.5-win.2"));
    REQUIRE(is_newer_version("0.7.5-win.100", "0.7.5-win.99"));
    REQUIRE_FALSE(is_newer_version("0.7.5-win.21", "0.7.5-win.22"));
    REQUIRE_FALSE(is_newer_version("0.7.5-win.22", "0.7.5-win.22"));
}

TEST_CASE("is_newer_version normalizes git-describe labels before comparing",
          "[unit][version_check][regression]") {
    using corridorkey::app::is_newer_version;
    using corridorkey::app::parse_semver;

    // A local build 3 commits past v0.7.6-win.1 should parse as if it were
    // exactly at v0.7.6-win.1 for comparison purposes. Without the
    // git-describe normalization this would sort above win.2 (non-numeric
    // identifier "1-3-gabc1234" outranks numeric "2" under SemVer 2.0.0),
    // blocking the auto-updater from offering a real update to dev builds.
    auto parsed = parse_semver("0.7.6-win.1-3-gabc1234");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->pre_release == "win.1");

    auto parsed_dirty = parse_semver("0.7.6-win.1-3-gabc1234-dirty");
    REQUIRE(parsed_dirty.has_value());
    REQUIRE(parsed_dirty->pre_release == "win.1");

    REQUIRE(is_newer_version("0.7.6-win.2", "0.7.6-win.1-3-gabc1234"));
    REQUIRE(is_newer_version("0.7.6-win.2", "0.7.6-win.1-3-gabc1234-dirty"));
    REQUIRE_FALSE(is_newer_version("0.7.6-win.1-3-gabc1234", "0.7.6-win.1"));

    // Arbitrary non-describe suffix must not be mistaken for a describe tail.
    // `rc.beta` contains neither a numeric count nor `-g<hex>` and must
    // survive intact.
    auto untouched = parse_semver("0.7.6-rc.beta");
    REQUIRE(untouched.has_value());
    REQUIRE(untouched->pre_release == "rc.beta");
}

TEST_CASE("prerelease_platform_code extracts the leading non-numeric identifier",
          "[unit][version_check]") {
    using corridorkey::app::prerelease_platform_code;

    REQUIRE(prerelease_platform_code("win.22") == "win");
    REQUIRE(prerelease_platform_code("mac.10") == "mac");
    REQUIRE(prerelease_platform_code("linux.1") == "linux");
    REQUIRE(prerelease_platform_code("").empty());
    REQUIRE(prerelease_platform_code("22").empty());
    REQUIRE(prerelease_platform_code("rc.1") == "rc");
}

TEST_CASE("current_platform_code reports the compile-time platform", "[unit][version_check]") {
    using corridorkey::app::current_platform_code;

    const auto code = current_platform_code();
#if defined(_WIN32)
    REQUIRE(code == "win");
#elif defined(__APPLE__)
    REQUIRE(code == "mac");
#elif defined(__linux__)
    REQUIRE(code == "linux");
#endif
}

TEST_CASE("cache round-trips stable and prerelease entries", "[unit][version_check]") {
    using namespace corridorkey::app;

    const auto path = temp_cache_path("roundtrip");
    std::filesystem::remove(path);

    CachedCheck cache;
    cache.fetched_at_unix_seconds = 1700000000;
    cache.stable = UpdateInfo{"0.7.6", "https://example.com/releases/v0.7.6", false};
    cache.prerelease = UpdateInfo{"0.7.7-rc.1", "https://example.com/releases/v0.7.7-rc.1", true};

    REQUIRE(write_cache(path, cache));
    auto loaded = read_cache(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->fetched_at_unix_seconds == 1700000000);
    REQUIRE(loaded->stable.has_value());
    REQUIRE(loaded->stable->latest_version == "0.7.6");
    REQUIRE_FALSE(loaded->stable->is_prerelease);
    REQUIRE(loaded->prerelease.has_value());
    REQUIRE(loaded->prerelease->latest_version == "0.7.7-rc.1");
    REQUIRE(loaded->prerelease->is_prerelease);
}

TEST_CASE("read_cache returns nullopt for missing or malformed files", "[unit][version_check]") {
    using corridorkey::app::read_cache;

    REQUIRE_FALSE(read_cache(temp_cache_path("missing")).has_value());

    const auto bad_path = temp_cache_path("malformed");
    {
        std::ofstream bad(bad_path, std::ios::trunc);
        bad << "this is not json";
    }
    REQUIRE_FALSE(read_cache(bad_path).has_value());
}

TEST_CASE("is_cache_fresh honors the configured TTL", "[unit][version_check]") {
    using corridorkey::app::CachedCheck;
    using corridorkey::app::is_cache_fresh;

    CachedCheck cache;
    cache.fetched_at_unix_seconds = 1700000000;

    const auto fresh_now =
        std::chrono::system_clock::time_point(std::chrono::seconds(1700000000 + 3600));
    const auto stale_now =
        std::chrono::system_clock::time_point(std::chrono::seconds(1700000000 + 90000));

    REQUIRE(is_cache_fresh(cache, std::chrono::hours(24), fresh_now));
    REQUIRE_FALSE(is_cache_fresh(cache, std::chrono::hours(24), stale_now));
}

TEST_CASE("select_update respects prerelease toggle and current version", "[unit][version_check]") {
    using namespace corridorkey::app;

    CachedCheck cache;
    cache.stable = UpdateInfo{"0.7.6", "https://example.com/stable", false};
    cache.prerelease = UpdateInfo{"0.7.7-win.1", "https://example.com/pre", true};

    auto stable_only = select_update(cache, "0.7.5", false);
    REQUIRE(stable_only.has_value());
    REQUIRE(stable_only->latest_version == "0.7.6");

    auto with_prereleases = select_update(cache, "0.7.5", true);
    REQUIRE(with_prereleases.has_value());
    REQUIRE(with_prereleases->latest_version == "0.7.7-win.1");

    auto no_update = select_update(cache, "0.7.7", true);
    REQUIRE_FALSE(no_update.has_value());

    CachedCheck empty_cache;
    REQUIRE_FALSE(select_update(empty_cache, "0.7.5", true).has_value());
}

TEST_CASE("select_update does not offer a later prerelease when cache has only prereleases",
          "[unit][version_check][regression]") {
    using namespace corridorkey::app;

    CachedCheck cache;
    cache.prerelease = UpdateInfo{"0.7.5-win.22", "https://example.com/pre", true};

    auto same_build = select_update(cache, "0.7.5-win.22", true);
    REQUIRE_FALSE(same_build.has_value());

    auto older_build = select_update(cache, "0.7.5-win.21", true);
    REQUIRE(older_build.has_value());
    REQUIRE(older_build->latest_version == "0.7.5-win.22");

    auto stable_installed = select_update(cache, "0.7.5", true);
    REQUIRE_FALSE(stable_installed.has_value());
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
