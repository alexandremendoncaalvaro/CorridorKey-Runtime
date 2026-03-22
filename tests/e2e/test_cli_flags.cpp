#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <string>

#ifndef CLI_EXECUTABLE_PATH
#define CLI_EXECUTABLE_PATH "corridorkey_cli"
#endif

TEST_CASE("CLI handles arguments correctly and returns proper exit codes", "[e2e][cli]") {
    std::string exe_path = CLI_EXECUTABLE_PATH;

    SECTION("Executing with --help returns 0") {
        std::string cmd = exe_path + " --help > NUL 2>&1";
        int result = std::system(cmd.c_str());
        REQUIRE(result == 0);
    }

    SECTION("Executing with no arguments returns 0 and prints quickstart") {
        std::string cmd = exe_path + " > NUL 2>&1";
        int result = std::system(cmd.c_str());
        // The CLI explicitly returns 0 and prints a quickstart guide when called with no args
        REQUIRE(result == 0); 
    }

    SECTION("Executing with invalid flags returns non-zero code") {
        std::string cmd = exe_path + " --this-is-a-completely-invalid-flag > NUL 2>&1";
        int result = std::system(cmd.c_str());
        REQUIRE(result != 0);
    }
}
