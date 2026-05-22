#include <catch2/catch_all.hpp>
#include <limits>
#include <string>
#include <unordered_set>

#include "common/runtime_paths.hpp"
#include "ofxImageEffect.h"
#include "plugins/ofx/ofx_plugin_descriptors.hpp"

using namespace corridorkey::ofx;

// Captured by the unit-test stub of plugin_main_entry_dispatch in
// tests/unit/test_ofx_stubs.cpp. Lets the trampoline-routing tests below
// drive each descriptor's mainEntry pointer and verify that the per-
// descriptor trampoline bakes the right plugin identifier into the
// dispatch call before the next slice (task 0009) extends the dispatcher
// to branch by identity.
namespace corridorkey::ofx::dispatch_capture {
extern const char* last_identifier;
extern const char* last_action;
}  // namespace corridorkey::ofx::dispatch_capture

//
// Test-file tidy-suppression rationale.
//
// Test fixtures legitimately use single-letter loop locals, magic
// numbers (descriptor indices, expected counts), and Catch2 styles that
// pre-date the project's tightened .clang-tidy ruleset. The test source
// is verified behaviourally by ctest; converting every site to
// bounds-checked / designated-init / ranges form would obscure intent
// without changing what the tests prove. The same suppressions are
// documented and applied on the src/ tree where the underlying APIs
// live.
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)

TEST_CASE("OFX bundle exposes two descriptors", "[unit][ofx][descriptor]") {
    REQUIRE(descriptor_count() == 2);
}

TEST_CASE("Green descriptor preserves the legacy reverse-DNS identifier",
          "[unit][ofx][descriptor]") {
    OfxPlugin* green = descriptor_at(0);
    REQUIRE(green != nullptr);
    REQUIRE(std::string(green->pluginIdentifier) == "com.corridorkey.resolve");
    REQUIRE(std::string(green->pluginIdentifier) == kPluginIdentifierGreen);
}

TEST_CASE("Blue descriptor uses the dedicated-screen identifier locked by ADR-0006",
          "[unit][ofx][descriptor]") {
    OfxPlugin* blue = descriptor_at(1);
    REQUIRE(blue != nullptr);
    REQUIRE(std::string(blue->pluginIdentifier) == "com.corridorkey.resolve.blue");
    REQUIRE(std::string(blue->pluginIdentifier) == kPluginIdentifierBlue);
}

TEST_CASE("Green and Blue descriptor identifiers are distinct", "[unit][ofx][descriptor]") {
    OfxPlugin* green = descriptor_at(0);
    OfxPlugin* blue = descriptor_at(1);
    REQUIRE(green != nullptr);
    REQUIRE(blue != nullptr);
    REQUIRE(std::string(green->pluginIdentifier) != std::string(blue->pluginIdentifier));
}

TEST_CASE("Green and Blue descriptor labels are distinct", "[unit][ofx][descriptor]") {
    REQUIRE(std::string(kPluginLabelGreen) != std::string(kPluginLabelBlue));
    REQUIRE(std::string(label_for_identifier(kPluginIdentifierGreen)) == kPluginLabelGreen);
    REQUIRE(std::string(label_for_identifier(kPluginIdentifierBlue)) == kPluginLabelBlue);
}

TEST_CASE("label_for_identifier defaults unknown or null identifiers to the Green label",
          "[unit][ofx][descriptor]") {
    REQUIRE(std::string(label_for_identifier(nullptr)) == kPluginLabelGreen);
    REQUIRE(std::string(label_for_identifier("com.unknown.plugin")) == kPluginLabelGreen);
    REQUIRE(std::string(label_for_identifier("")) == kPluginLabelGreen);
}

TEST_CASE("Out-of-range descriptor indices return nullptr", "[unit][ofx][descriptor]") {
    REQUIRE(descriptor_at(-1) == nullptr);
    REQUIRE(descriptor_at(2) == nullptr);
    REQUIRE(descriptor_at(100) == nullptr);
    REQUIRE(descriptor_at(std::numeric_limits<int>::max()) == nullptr);
    REQUIRE(descriptor_at(std::numeric_limits<int>::min()) == nullptr);
}

TEST_CASE("Each descriptor advertises the OFX image-effect API and a non-null mainEntry",
          "[unit][ofx][descriptor]") {
    for (int nth = 0; nth < descriptor_count(); ++nth) {
        OfxPlugin* descriptor = descriptor_at(nth);
        REQUIRE(descriptor != nullptr);
        REQUIRE(std::string(descriptor->pluginApi) == kOfxImageEffectPluginApi);
        REQUIRE(descriptor->apiVersion == kOfxImageEffectPluginApiVersion);
        REQUIRE(descriptor->mainEntry != nullptr);
        REQUIRE(descriptor->setHost != nullptr);
        REQUIRE(descriptor->pluginIdentifier != nullptr);
    }
}

TEST_CASE("Per-descriptor trampolines are distinct function pointers",
          "[unit][ofx][descriptor]") {
    OfxPlugin* green = descriptor_at(0);
    OfxPlugin* blue = descriptor_at(1);
    REQUIRE(green != nullptr);
    REQUIRE(blue != nullptr);
    // OFX Support Library pattern: each descriptor carries its own thin
    // trampoline that bakes the identifier into the dispatch call. Sharing
    // one mainEntry would mean describe() cannot tell which descriptor is
    // being initialised and both nodes would render with the same label.
    REQUIRE(green->mainEntry != blue->mainEntry);
}

TEST_CASE("is_blue_node_identifier classifies descriptor identities correctly",
          "[unit][ofx][descriptor]") {
    REQUIRE(is_blue_node_identifier(kPluginIdentifierBlue));
    REQUIRE_FALSE(is_blue_node_identifier(kPluginIdentifierGreen));
    REQUIRE_FALSE(is_blue_node_identifier(nullptr));
    REQUIRE_FALSE(is_blue_node_identifier(""));
    REQUIRE_FALSE(is_blue_node_identifier("com.unknown.plugin"));
    REQUIRE_FALSE(is_blue_node_identifier("com.corridorkey.resolve.blueish"));
    // Pointer equality fast path also accepts a copy of the canonical
    // string — the helper falls back to string_view comparison.
    const std::string blue_copy = kPluginIdentifierBlue;
    REQUIRE(is_blue_node_identifier(blue_copy.c_str()));
}

TEST_CASE("Sidecar ports are distinct per node-family identifier",
          "[unit][ofx][descriptor][runtime]") {
    // Spec 0002 task 0010 follow-up: Green and Blue must compute different
    // sidecar ports so they get separate runtime-server processes. Same
    // family converges on one port; the empty-family hash matches the
    // familyless default_host_plugin_runtime_port.
    const auto green_port =
        corridorkey::common::default_host_plugin_runtime_port_for_family(kPluginIdentifierGreen);
    const auto blue_port =
        corridorkey::common::default_host_plugin_runtime_port_for_family(kPluginIdentifierBlue);
    REQUIRE(green_port != blue_port);

    const auto green_port_again =
        corridorkey::common::default_host_plugin_runtime_port_for_family(kPluginIdentifierGreen);
    REQUIRE(green_port_again == green_port);

    const auto legacy_port = corridorkey::common::default_host_plugin_runtime_port();
    const auto empty_family_port =
        corridorkey::common::default_host_plugin_runtime_port_for_family({});
    REQUIRE(legacy_port == empty_family_port);
}

TEST_CASE("Identifier strings have stable storage across descriptor lookups",
          "[unit][ofx][descriptor]") {
    // OFX hosts cache the OfxPlugin* and the pluginIdentifier string across
    // the lifetime of the loaded binary. Calling descriptor_at multiple times
    // must return the same pointer and the identifier string must remain
    // address-stable so a host that hashes by pointer does not re-key.
    OfxPlugin* first_green = descriptor_at(0);
    OfxPlugin* second_green = descriptor_at(0);
    REQUIRE(first_green == second_green);
    REQUIRE(first_green->pluginIdentifier == second_green->pluginIdentifier);

    OfxPlugin* first_blue = descriptor_at(1);
    OfxPlugin* second_blue = descriptor_at(1);
    REQUIRE(first_blue == second_blue);
    REQUIRE(first_blue->pluginIdentifier == second_blue->pluginIdentifier);
}

TEST_CASE("Green trampoline forwards kPluginIdentifierGreen into the dispatcher",
          "[unit][ofx][descriptor]") {
    OfxPlugin* green = descriptor_at(0);
    REQUIRE(green != nullptr);
    REQUIRE(green->mainEntry != nullptr);

    dispatch_capture::last_identifier = nullptr;
    dispatch_capture::last_action = nullptr;
    const OfxStatus status =
        green->mainEntry("test_dispatch_action", nullptr, nullptr, nullptr);
    REQUIRE(status == kOfxStatReplyDefault);
    REQUIRE(dispatch_capture::last_identifier != nullptr);
    REQUIRE(std::string(dispatch_capture::last_identifier) == kPluginIdentifierGreen);
    REQUIRE(std::string(dispatch_capture::last_action) == "test_dispatch_action");
}

TEST_CASE("Blue trampoline forwards kPluginIdentifierBlue into the dispatcher",
          "[unit][ofx][descriptor]") {
    OfxPlugin* blue = descriptor_at(1);
    REQUIRE(blue != nullptr);
    REQUIRE(blue->mainEntry != nullptr);

    dispatch_capture::last_identifier = nullptr;
    dispatch_capture::last_action = nullptr;
    const OfxStatus status =
        blue->mainEntry("test_dispatch_action", nullptr, nullptr, nullptr);
    REQUIRE(status == kOfxStatReplyDefault);
    REQUIRE(dispatch_capture::last_identifier != nullptr);
    REQUIRE(std::string(dispatch_capture::last_identifier) == kPluginIdentifierBlue);
    REQUIRE(std::string(dispatch_capture::last_action) == "test_dispatch_action");
}

TEST_CASE("Trampolines do not cross-route Green and Blue identifiers",
          "[unit][ofx][descriptor]") {
    OfxPlugin* green = descriptor_at(0);
    OfxPlugin* blue = descriptor_at(1);
    REQUIRE(green != nullptr);
    REQUIRE(blue != nullptr);

    dispatch_capture::last_identifier = nullptr;
    green->mainEntry("any", nullptr, nullptr, nullptr);
    REQUIRE(std::string(dispatch_capture::last_identifier) != kPluginIdentifierBlue);

    dispatch_capture::last_identifier = nullptr;
    blue->mainEntry("any", nullptr, nullptr, nullptr);
    REQUIRE(std::string(dispatch_capture::last_identifier) != kPluginIdentifierGreen);
}

TEST_CASE("Identifiers satisfy the OpenFX 'no whitespace, printable ASCII' rule",
          "[unit][ofx][descriptor]") {
    // ofxCore.h on OfxPlugin.pluginIdentifier: "It must be a legal ASCII
    // string and have no whitespace in the name and no non printing chars."
    for (int nth = 0; nth < descriptor_count(); ++nth) {
        OfxPlugin* descriptor = descriptor_at(nth);
        REQUIRE(descriptor != nullptr);
        const std::string identifier = descriptor->pluginIdentifier;
        REQUIRE_FALSE(identifier.empty());
        for (char ch : identifier) {
            const auto byte = static_cast<unsigned char>(ch);
            REQUIRE(byte >= 0x21);
            REQUIRE(byte <= 0x7E);
        }
    }

    std::unordered_set<std::string> seen;
    for (int nth = 0; nth < descriptor_count(); ++nth) {
        OfxPlugin* descriptor = descriptor_at(nth);
        REQUIRE(descriptor != nullptr);
        const auto inserted = seen.insert(descriptor->pluginIdentifier).second;
        REQUIRE(inserted);
    }
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
