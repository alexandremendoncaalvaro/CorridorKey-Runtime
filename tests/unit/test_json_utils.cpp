#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include "common/json_utils.hpp"

using namespace corridorkey::common;

TEST_CASE("safe_json_dump replaces invalid UTF-8 instead of throwing",
          "[unit][doctor][regression]") {
    nlohmann::json json;
    json["message"] = std::string("bad-\xC3\x28", 6);

    REQUIRE_THROWS_AS(json.dump(2), nlohmann::json::type_error);

    const auto dumped = safe_json_dump(json, 2);

    REQUIRE_FALSE(dumped.empty());
    REQUIRE(dumped.find("\"message\"") != std::string::npos);
}
