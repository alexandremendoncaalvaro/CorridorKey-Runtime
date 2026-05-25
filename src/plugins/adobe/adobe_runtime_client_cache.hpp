#pragma once

#include <cstdint>
#include <memory>

#include <corridorkey/types.hpp>

#include "app/host_plugin_runtime_client.hpp"

namespace corridorkey::adobe {

std::uint64_t next_adobe_runtime_client_key() noexcept;

Result<std::shared_ptr<app::HostPluginRuntimeClient>> acquire_cached_adobe_runtime_client(
    std::uint64_t key, app::HostPluginRuntimeClientOptions options);

void release_cached_adobe_runtime_client(std::uint64_t key) noexcept;
void release_all_cached_adobe_runtime_clients() noexcept;

}  // namespace corridorkey::adobe
