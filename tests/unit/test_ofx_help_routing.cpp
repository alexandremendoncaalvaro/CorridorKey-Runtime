#include <catch2/catch_all.hpp>

#include "plugins/ofx/ofx_shared.hpp"

using namespace corridorkey::ofx;

TEST_CASE("select_tutorial_doc routes Nuke host to the Nuke tutorial", "[unit][ofx]") {
    REQUIRE(select_tutorial_doc(kHostNameNuke) == "OFX_NUKE_TUTORIALS.md");
}

TEST_CASE("select_tutorial_doc routes Resolve host to the Resolve tutorial", "[unit][ofx]") {
    REQUIRE(select_tutorial_doc(kHostNameResolve) == "OFX_RESOLVE_TUTORIALS.md");
}

TEST_CASE("select_tutorial_doc falls back to the host-neutral panel guide for unknown hosts",
          "[unit][ofx]") {
    REQUIRE(select_tutorial_doc("") == "OFX_PANEL_GUIDE.md");
    REQUIRE(select_tutorial_doc("com.blackmagicdesign.Fusion") == "OFX_PANEL_GUIDE.md");
    REQUIRE(select_tutorial_doc("com.example.unknown") == "OFX_PANEL_GUIDE.md");
}

TEST_CASE("host_qualified_phrase appends host suffix only for known hosts", "[unit][ofx]") {
    REQUIRE(host_qualified_phrase(kHostNameNuke, "Open the quick-start guide") ==
            "Open the quick-start guide in Nuke");
    REQUIRE(host_qualified_phrase(kHostNameResolve, "Open the quick-start guide") ==
            "Open the quick-start guide in Resolve");
    REQUIRE(host_qualified_phrase("", "Open the quick-start guide") ==
            "Open the quick-start guide");
    REQUIRE(host_qualified_phrase("com.example.unknown", "Open the quick-start guide") ==
            "Open the quick-start guide");
}

TEST_CASE("host_qualified_phrase tolerates a null base phrase", "[unit][ofx]") {
    REQUIRE(host_qualified_phrase(kHostNameNuke, nullptr).empty());
    REQUIRE(host_qualified_phrase(kHostNameResolve, nullptr).empty());
    REQUIRE(host_qualified_phrase("", nullptr).empty());
}
