#pragma once

#include <chrono>
#include <corridorkey/api_export.hpp>
#include <filesystem>

#include "../common/local_ipc.hpp"
#include "../common/host_plugin_runtime_defaults.hpp"
#include "host_plugin_runtime_protocol.hpp"
#include "host_plugin_session_broker.hpp"

namespace corridorkey::app {

struct HostPluginRuntimeServiceOptions {
    common::LocalJsonEndpoint endpoint = {};
    std::chrono::milliseconds idle_timeout = common::kDefaultHostPluginIdleTimeout;
    std::filesystem::path log_path = {};
    HostPluginSessionBrokerOptions broker = {};
};

class CORRIDORKEY_API HostPluginRuntimeService {
   public:
    static Result<void> run(const HostPluginRuntimeServiceOptions& options);
};

}  // namespace corridorkey::app
