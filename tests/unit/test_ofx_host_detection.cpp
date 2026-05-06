#include <catch2/catch_all.hpp>

#include "plugins/ofx/ofx_shared.hpp"

using namespace corridorkey::ofx;

namespace {

OfxStatus message_suite_noop(void* /*handle*/, const char* /*message_type*/,
                             const char* /*message_id*/, const char* /*format*/, ...) {
    return kOfxStatOK;
}

OfxStatus clear_persistent_message_noop(void* /*handle*/) {
    return kOfxStatOK;
}

}  // namespace

TEST_CASE("is_nuke_host_name matches the canonical Foundry Nuke string", "[unit][ofx]") {
    REQUIRE(is_nuke_host_name("uk.co.thefoundry.nuke"));
    REQUIRE(is_nuke_host_name(kHostNameNuke));
}

TEST_CASE("is_resolve_host_name matches the canonical DaVinci Resolve string", "[unit][ofx]") {
    REQUIRE(is_resolve_host_name("DaVinciResolveLite"));
    REQUIRE(is_resolve_host_name(kHostNameResolve));
}

TEST_CASE("host detection helpers reject empty and unknown host names", "[unit][ofx]") {
    REQUIRE_FALSE(is_nuke_host_name(""));
    REQUIRE_FALSE(is_resolve_host_name(""));
    REQUIRE_FALSE(is_nuke_host_name("com.blackmagicdesign.Fusion"));
    REQUIRE_FALSE(is_resolve_host_name("uk.co.thefoundry.nuke"));
    REQUIRE_FALSE(is_nuke_host_name("DaVinciResolveLite"));
}

TEST_CASE("host detection is case-sensitive (matches OFX spec)", "[unit][ofx]") {
    // Hosts advertise the exact strings; the spec does not promise case
    // normalization, so do not pretend to match different casing.
    REQUIRE_FALSE(is_nuke_host_name("UK.CO.THEFOUNDRY.NUKE"));
    REQUIRE_FALSE(is_resolve_host_name("davinciresolvelite"));
}

TEST_CASE("global helpers track g_host_name", "[unit][ofx]") {
    const std::string previous = g_host_name;

    g_host_name = kHostNameNuke;
    REQUIRE(is_nuke_host());
    REQUIRE_FALSE(is_resolve_host());

    g_host_name = kHostNameResolve;
    REQUIRE(is_resolve_host());
    REQUIRE_FALSE(is_nuke_host());

    g_host_name.clear();
    REQUIRE_FALSE(is_nuke_host());
    REQUIRE_FALSE(is_resolve_host());

    g_host_name = previous;
}

TEST_CASE("V1 message suite does not expose persistent-message callbacks",
          "[unit][ofx][regression]") {
    OfxMessageSuiteV1 message_v1{};
    message_v1.message = message_suite_noop;

    OfxSuites suites{};
    suites.message_v1 = &message_v1;

    REQUIRE(has_transient_message_suite(suites));
    REQUIRE_FALSE(has_persistent_message_suite(suites));
    REQUIRE_FALSE(has_clear_persistent_message_suite(suites));
}

TEST_CASE("V2 message suite exposes persistent-message callbacks only through V2",
          "[unit][ofx][regression]") {
    OfxMessageSuiteV2 message_v2{};
    message_v2.message = message_suite_noop;
    message_v2.setPersistentMessage = message_suite_noop;
    message_v2.clearPersistentMessage = clear_persistent_message_noop;

    OfxSuites suites{};
    suites.message_v2 = &message_v2;

    REQUIRE(has_transient_message_suite(suites));
    REQUIRE(has_persistent_message_suite(suites));
    REQUIRE(has_clear_persistent_message_suite(suites));
}
