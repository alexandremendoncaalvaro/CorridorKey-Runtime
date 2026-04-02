#pragma once

#include <filesystem>
#include <string_view>

namespace corridorkey::ofx {

void log_message(std::string_view scope, std::string_view message);
void close_log();
std::filesystem::path log_file_path();

}  // namespace corridorkey::ofx
