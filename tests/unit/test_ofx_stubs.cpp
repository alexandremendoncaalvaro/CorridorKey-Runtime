#include "plugins/ofx/ofx_frame_cache.hpp"
#include "plugins/ofx/ofx_shared.hpp"

namespace corridorkey::ofx {

OfxHost* g_host = nullptr;
OfxSuites g_suites = {};
std::unique_ptr<SharedFrameCache> g_frame_cache = nullptr;
std::string g_host_name;

void capture_host_name() {
    // Test stub: production capture_host_name() reads kOfxPropHostName from
    // the host. Tests that exercise host-aware behavior assign g_host_name
    // directly.
}

void post_message(const char* message_type, const char* message, OfxImageEffectHandle effect) {
    (void)message_type;
    (void)message;
    (void)effect;
}

}  // namespace corridorkey::ofx
