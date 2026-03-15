#pragma once

#include <string_view>

namespace corridorkey::ofx {

void log_message(std::string_view scope, std::string_view message);
void close_log();

}  // namespace corridorkey::ofx
