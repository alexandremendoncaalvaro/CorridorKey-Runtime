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

// Match the OfxMessageSuiteV2 setPersistentMessage / clearPersistentMessage
// helpers that the production plugin defines in ofx_plugin.cpp. The unit
// test target does not compile ofx_plugin.cpp (it owns the global suite
// fetch machinery the tests stub manually), so the symbols are provided
// here as no-ops. Tests that need to assert the message-suite call path
// stub g_suites.message with their own table.
void set_persistent_message(const char* message_type, const char* message_id,
                            const char* message, OfxImageEffectHandle effect) {
    (void)message_type;
    (void)message_id;
    (void)message;
    (void)effect;
}

void clear_persistent_message(OfxImageEffectHandle effect) {
    (void)effect;
}

}  // namespace corridorkey::ofx
