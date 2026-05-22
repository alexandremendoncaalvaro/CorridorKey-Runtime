#include "plugins/ofx/ofx_frame_cache.hpp"
#include "plugins/ofx/ofx_shared.hpp"

namespace corridorkey::ofx {

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
void set_persistent_message(const char* message_type, const char* message_id, const char* message,
                            OfxImageEffectHandle effect) {
    (void)message_type;
    (void)message_id;
    (void)message;
    (void)effect;
}

void clear_persistent_message(OfxImageEffectHandle effect) {
    if (g_suites.message == nullptr || g_suites.message->clearPersistentMessage == nullptr ||
        effect == nullptr) {
        return;
    }
    (void)g_suites.message->clearPersistentMessage(effect);
}

// ProgressScope stub mirrors the production helper from ofx_plugin.cpp
// but does no host-side work. The unit test target does not compile
// ofx_plugin.cpp (it owns the global suite-fetch machinery the tests stub
// manually); test cases that exercise progress-suite behaviour install
// their own table on g_suites.
ProgressScope::ProgressScope(OfxImageEffectHandle effect, const char* label, const char* message_id)
    : m_effect(effect) {
    (void)label;
    (void)message_id;
}

ProgressScope::~ProgressScope() = default;

// Mirrors the production ProgressScope::update signature — making the
// stub static would break the symbol the test harness expects.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool ProgressScope::update(double progress) {
    (void)progress;
    return true;
}

// Stub for the action dispatcher that ofx_plugin_descriptors.cpp's
// per-descriptor trampolines forward into. Production defines this in
// ofx_plugin.cpp; the unit test target does not link ofx_plugin.cpp because
// the suite-fetch globals there race with the per-test stubs above.
//
// The stub captures the identifier and action pointers passed by the
// trampoline so test_ofx_descriptor_split.cpp can drive each descriptor's
// mainEntry function pointer and assert that Green's trampoline forwards
// kPluginIdentifierGreen and Blue's trampoline forwards
// kPluginIdentifierBlue. The captured pointers are reset by the test
// before each invocation. The stub returns kOfxStatReplyDefault so the
// trampoline body executes to completion without invoking any further
// real action handler.
namespace dispatch_capture {
const char* last_identifier = nullptr;
const char* last_action = nullptr;
}  // namespace dispatch_capture

OfxStatus plugin_main_entry_dispatch(const char* plugin_identifier, const char* action,
                                     const void* /*handle*/, OfxPropertySetHandle /*in_args*/,
                                     OfxPropertySetHandle /*out_args*/) {
    dispatch_capture::last_identifier = plugin_identifier;
    dispatch_capture::last_action = action;
    return kOfxStatReplyDefault;
}

}  // namespace corridorkey::ofx

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
