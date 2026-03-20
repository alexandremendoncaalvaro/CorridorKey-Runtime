#include "plugins/ofx/ofx_shared.hpp"

namespace corridorkey::ofx {

OfxHost* g_host = nullptr;
OfxSuites g_suites = {};

void post_message(const char* message_type, const char* message, OfxImageEffectHandle effect) {
    (void)message_type;
    (void)message;
    (void)effect;
}

}  // namespace corridorkey::ofx
