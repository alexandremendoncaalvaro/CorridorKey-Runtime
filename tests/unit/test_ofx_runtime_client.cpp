#include <catch2/catch_all.hpp>

#include "app/ofx_runtime_service.hpp"
#include "app/ofx_session_broker.hpp"
#include "common/ofx_runtime_defaults.hpp"
#include "plugins/ofx/ofx_runtime_client.hpp"

using namespace corridorkey;
using namespace corridorkey::app;
using namespace corridorkey::ofx;

TEST_CASE("ofx runtime timeout defaults stay aligned across client and service",
          "[unit][ofx][runtime][regression]") {
    OfxRuntimeClientOptions client_options;
    OfxRuntimeServiceOptions service_options;
    OfxSessionBrokerOptions broker_options;

    REQUIRE(common::kDefaultOfxRenderTimeoutSeconds == 60);
    REQUIRE(common::kDefaultOfxPrepareTimeoutSeconds == 300);
    REQUIRE(common::kDefaultOfxRequestTimeoutMs == 60000);
    REQUIRE(common::kDefaultOfxPrepareTimeoutMs == 300000);
    REQUIRE(common::kDefaultOfxIdleTimeoutMs == 300000);

    REQUIRE(client_options.request_timeout_ms == common::kDefaultOfxRequestTimeoutMs);
    REQUIRE(client_options.prepare_timeout_ms == common::kDefaultOfxPrepareTimeoutMs);
    REQUIRE(client_options.idle_timeout_ms == common::kDefaultOfxIdleTimeoutMs);
    REQUIRE(service_options.idle_timeout == common::kDefaultOfxIdleTimeout);
    REQUIRE(broker_options.idle_session_ttl == common::kDefaultOfxIdleTimeout);
}
