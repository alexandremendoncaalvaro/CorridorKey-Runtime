#include <catch2/catch_all.hpp>

#include "core/ort_process_context.hpp"

using namespace corridorkey::core;

TEST_CASE("ORT process context reuses one env and shared CPU allocator", "[unit][runtime]") {
    OrtProcessContext context;

    Ort::Env& first_env = context.acquire_env(ORT_LOGGING_LEVEL_ERROR);
    Ort::Env& second_env = context.acquire_env(ORT_LOGGING_LEVEL_WARNING);

    REQUIRE(&first_env == &second_env);

    auto cpu_memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto allocator = first_env.GetSharedAllocator(cpu_memory_info);
    REQUIRE(static_cast<OrtAllocator*>(allocator) != nullptr);
}

TEST_CASE("ORT process context ownership is explicit per injected instance", "[unit][runtime]") {
    OrtProcessContext first_context;
    OrtProcessContext second_context;

    Ort::Env& first_env = first_context.acquire_env(ORT_LOGGING_LEVEL_ERROR);
    Ort::Env& second_env = second_context.acquire_env(ORT_LOGGING_LEVEL_ERROR);

    REQUIRE(&first_env != &second_env);
}
