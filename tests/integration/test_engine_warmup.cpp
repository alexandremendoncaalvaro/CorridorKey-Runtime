#include <algorithm>
#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <filesystem>

#include "../test_model_artifact_utils.hpp"

using namespace corridorkey;

namespace {

bool has_stage(const std::vector<StageTiming>& timings, const std::string& name) {
    return std::any_of(timings.begin(), timings.end(),
                       [&](const StageTiming& timing) { return timing.name == name; });
}

}  // namespace

TEST_CASE("Engine warmup happens on first processing call", "[integration][engine]") {
    const std::filesystem::path model_path = "models/corridorkey_int8_512.onnx";
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(model_path);
        reason.has_value()) {
        SKIP(*reason);
    }

    std::vector<StageTiming> create_timings;
    auto create_res =
        Engine::create(model_path, DeviceInfo{"Generic CPU", 0, Backend::CPU},
                       [&](const StageTiming& timing) { create_timings.push_back(timing); });
    REQUIRE(create_res.has_value());
    REQUIRE_FALSE(has_stage(create_timings, "engine_warmup"));
    REQUIRE(has_stage(create_timings, "ort_env_acquire"));
    REQUIRE(has_stage(create_timings, "ort_session_options"));
    REQUIRE(has_stage(create_timings, "ort_session_create"));
    REQUIRE(has_stage(create_timings, "ort_metadata_extract"));

    ImageBuffer rgb_buf(32, 32, 3);
    ImageBuffer hint_buf(32, 32, 1);
    std::fill(rgb_buf.view().data.begin(), rgb_buf.view().data.end(), 0.0f);
    std::fill(hint_buf.view().data.begin(), hint_buf.view().data.end(), 0.0f);

    std::vector<StageTiming> run_timings;
    auto run_res =
        (*create_res)
            ->process_frame(rgb_buf.view(), hint_buf.view(), {},
                            [&](const StageTiming& timing) { run_timings.push_back(timing); });
    REQUIRE(run_res.has_value());
    REQUIRE(has_stage(run_timings, "engine_warmup"));
    REQUIRE(has_stage(run_timings, "frame_prepare_inputs"));
    REQUIRE(has_stage(run_timings, "ort_run"));
    REQUIRE(has_stage(run_timings, "frame_extract_outputs"));
    REQUIRE(has_stage(run_timings, "frame_extract_outputs_tensor_materialize"));
    REQUIRE(has_stage(run_timings, "frame_extract_outputs_resize"));
    REQUIRE(has_stage(run_timings, "frame_extract_outputs_finalize"));
}
