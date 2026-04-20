#pragma once

#include <chrono>
#include <corridorkey/api_export.hpp>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace corridorkey::app {

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string pre_release;
};

struct UpdateInfo {
    std::string latest_version;
    std::string release_url;
    bool is_prerelease = false;
};

struct VersionCheckOptions {
    std::string current_version;
    std::string repository = "alexandremendoncaalvaro/CorridorKey-Runtime";
    bool include_prereleases = false;
    std::filesystem::path cache_path;
    std::chrono::seconds cache_ttl = std::chrono::hours(24);
    std::chrono::milliseconds network_timeout = std::chrono::seconds(5);
};

CORRIDORKEY_API std::optional<SemVer> parse_semver(const std::string& version);

CORRIDORKEY_API bool is_newer_version(const std::string& latest, const std::string& current);

CORRIDORKEY_API std::filesystem::path default_cache_path();

struct CachedCheck {
    std::int64_t fetched_at_unix_seconds = 0;
    std::optional<UpdateInfo> stable;
    std::optional<UpdateInfo> prerelease;
};

CORRIDORKEY_API std::optional<CachedCheck> read_cache(const std::filesystem::path& cache_file);

CORRIDORKEY_API bool write_cache(const std::filesystem::path& cache_file, const CachedCheck& cache);

CORRIDORKEY_API bool is_cache_fresh(const CachedCheck& cache, std::chrono::seconds ttl,
                                    std::chrono::system_clock::time_point now);

CORRIDORKEY_API std::optional<UpdateInfo> select_update(const CachedCheck& cache,
                                                        const std::string& current_version,
                                                        bool include_prereleases);

CORRIDORKEY_API std::optional<UpdateInfo> check_for_update(const VersionCheckOptions& options);

}  // namespace corridorkey::app
