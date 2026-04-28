#include <catch2/catch_all.hpp>

#include "plugins/ofx/ofx_shared.hpp"

using namespace corridorkey::ofx;

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
