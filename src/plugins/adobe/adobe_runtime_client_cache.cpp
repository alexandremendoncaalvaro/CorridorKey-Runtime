#include "adobe_runtime_client_cache.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace corridorkey::adobe {
namespace {

using RuntimeClientPtr = std::shared_ptr<app::HostPluginRuntimeClient>;

std::atomic_uint64_t& runtime_client_key_counter() {
    static std::atomic_uint64_t counter{1};
    return counter;
}

std::mutex& runtime_client_cache_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::uint64_t, RuntimeClientPtr>& runtime_client_cache() {
    static std::unordered_map<std::uint64_t, RuntimeClientPtr> cache;
    return cache;
}

}  // namespace

std::uint64_t next_adobe_runtime_client_key() noexcept {
    std::uint64_t key = runtime_client_key_counter().fetch_add(1, std::memory_order_relaxed);
    if (key == 0) {
        key = runtime_client_key_counter().fetch_add(1, std::memory_order_relaxed);
    }
    return key;
}

Result<RuntimeClientPtr> acquire_cached_adobe_runtime_client(
    std::uint64_t key, app::HostPluginRuntimeClientOptions options) {
    if (key == 0) {
        return Unexpected<Error>(
            Error{ErrorCode::InvalidParameters, "Adobe runtime client cache key is required."});
    }

    const std::scoped_lock lock(runtime_client_cache_mutex());
    auto& cache = runtime_client_cache();
    if (auto existing = cache.find(key); existing != cache.end()) {
        return existing->second;
    }

    auto client = app::HostPluginRuntimeClient::create(std::move(options));
    if (!client) {
        return Unexpected<Error>(client.error());
    }

    auto shared_client = RuntimeClientPtr(std::move(*client));
    cache.emplace(key, shared_client);
    return shared_client;
}

void release_cached_adobe_runtime_client(std::uint64_t key) noexcept {
    if (key == 0) {
        return;
    }

    RuntimeClientPtr client;
    try {
        const std::scoped_lock lock(runtime_client_cache_mutex());
        auto& cache = runtime_client_cache();
        auto existing = cache.find(key);
        if (existing == cache.end()) {
            return;
        }
        client = std::move(existing->second);
        cache.erase(existing);
    } catch (...) {
        return;
    }
}

void release_all_cached_adobe_runtime_clients() noexcept {
    std::vector<RuntimeClientPtr> clients;
    try {
        const std::scoped_lock lock(runtime_client_cache_mutex());
        auto& cache = runtime_client_cache();
        clients.reserve(cache.size());
        for (auto& entry : cache) {
            clients.push_back(std::move(entry.second));
        }
        cache.clear();
    } catch (...) {
    }
}

}  // namespace corridorkey::adobe
