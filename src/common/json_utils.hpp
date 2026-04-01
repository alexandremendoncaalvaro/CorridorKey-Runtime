#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace corridorkey::common {

inline std::string safe_json_dump(const nlohmann::json& json, int indent = -1) {
    return json.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace corridorkey::common
