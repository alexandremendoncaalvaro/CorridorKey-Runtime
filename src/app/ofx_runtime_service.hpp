#pragma once

#include <chrono>
#include <corridorkey/api_export.hpp>
#include <filesystem>

#include "../common/local_ipc.hpp"
#include "../common/ofx_runtime_defaults.hpp"
#include "ofx_runtime_protocol.hpp"
#include "ofx_session_broker.hpp"

namespace corridorkey::app {

struct OfxRuntimeServiceOptions {
    common::LocalJsonEndpoint endpoint = {};
    std::chrono::milliseconds idle_timeout = common::kDefaultOfxIdleTimeout;
    std::filesystem::path log_path = {};
    OfxSessionBrokerOptions broker = {};
};

class CORRIDORKEY_API OfxRuntimeService {
   public:
    static Result<void> run(const OfxRuntimeServiceOptions& options);
};

}  // namespace corridorkey::app
