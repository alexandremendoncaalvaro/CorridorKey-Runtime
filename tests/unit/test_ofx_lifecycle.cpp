#include <catch2/catch_all.hpp>

#include <array>
#include <map>
#include <string>

#include "plugins/ofx/ofx_frame_cache.hpp"
#include "plugins/ofx/ofx_runtime_client.hpp"
#include "plugins/ofx/ofx_shared.hpp"

using namespace corridorkey;
using namespace corridorkey::ofx;

namespace {

struct FakeEffectProps {
    InstanceData* instance_data = nullptr;
};

struct FakeRoiProps {
    std::array<double, 4> roi{};
    std::map<std::string, std::array<double, 4>> clip_rois;
};

OfxStatus fake_prop_get_pointer(OfxPropertySetHandle handle, const char* name, int, void** value) {
    if (handle == nullptr || value == nullptr || name == nullptr) {
        return kOfxStatErrBadHandle;
    }
    if (std::strcmp(name, kOfxPropInstanceData) != 0) {
        return kOfxStatErrBadHandle;
    }
    auto* props = reinterpret_cast<FakeEffectProps*>(handle);
    *value = props->instance_data;
    return kOfxStatOK;
}

OfxStatus fake_prop_set_pointer(OfxPropertySetHandle handle, const char* name, int,
                                void* value) {
    if (handle == nullptr || name == nullptr) {
        return kOfxStatErrBadHandle;
    }
    if (std::strcmp(name, kOfxPropInstanceData) != 0) {
        return kOfxStatErrBadHandle;
    }
    auto* props = reinterpret_cast<FakeEffectProps*>(handle);
    props->instance_data = reinterpret_cast<InstanceData*>(value);
    return kOfxStatOK;
}

OfxStatus fake_get_property_set(OfxImageEffectHandle handle, OfxPropertySetHandle* props) {
    if (props == nullptr) {
        return kOfxStatErrBadHandle;
    }
    *props = reinterpret_cast<OfxPropertySetHandle>(handle);
    return kOfxStatOK;
}

OfxStatus fake_prop_get_double_n(OfxPropertySetHandle handle, const char* name, int count,
                                 double* values) {
    if (handle == nullptr || name == nullptr || values == nullptr || count != 4) {
        return kOfxStatErrBadHandle;
    }
    if (std::strcmp(name, kOfxImageEffectPropRegionOfInterest) != 0) {
        return kOfxStatErrBadHandle;
    }
    auto* props = reinterpret_cast<FakeRoiProps*>(handle);
    for (int index = 0; index < 4; ++index) {
        values[index] = props->roi[static_cast<std::size_t>(index)];
    }
    return kOfxStatOK;
}

OfxStatus fake_prop_set_double_n(OfxPropertySetHandle handle, const char* name, int count,
                                 const double* values) {
    if (handle == nullptr || name == nullptr || values == nullptr || count != 4) {
        return kOfxStatErrBadHandle;
    }
    auto* props = reinterpret_cast<FakeRoiProps*>(handle);
    std::array<double, 4> stored{};
    for (int index = 0; index < 4; ++index) {
        stored[static_cast<std::size_t>(index)] = values[index];
    }
    props->clip_rois[name] = stored;
    return kOfxStatOK;
}

ImageBuffer filled_alpha() {
    ImageBuffer buffer(2, 2, 1);
    std::fill(buffer.view().data.begin(), buffer.view().data.end(), 0.5F);
    return buffer;
}

ImageBuffer filled_foreground() {
    ImageBuffer buffer(2, 2, 3);
    std::fill(buffer.view().data.begin(), buffer.view().data.end(), 0.25F);
    return buffer;
}

}  // namespace

TEST_CASE("begin and end sequence render reset per-instance caches", "[unit][ofx][regression]") {
    InstanceData data{};
    data.cached_result.alpha = filled_alpha();
    data.cached_result.foreground = filled_foreground();
    data.cached_result_valid = true;
    data.cached_signature = 123;
    data.cached_signature_valid = true;
    data.temporal_alpha = filled_alpha();
    data.temporal_foreground = filled_foreground();
    data.temporal_state_valid = true;
    data.temporal_width = 2;
    data.temporal_height = 2;
    data.render_count = 7;
    data.last_frame_ms = 15.0;
    data.avg_frame_ms = 14.0;
    data.frame_time_samples = 3;

    FakeEffectProps props{.instance_data = &data};

    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetPointer = fake_prop_get_pointer;
    property_suite.propSetPointer = fake_prop_set_pointer;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;

    auto* previous_property_suite = g_suites.property;
    auto* previous_image_suite = g_suites.image_effect;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;

    REQUIRE(begin_sequence_render(reinterpret_cast<OfxImageEffectHandle>(&props), nullptr) ==
            kOfxStatOK);
    CHECK_FALSE(data.cached_result_valid);
    CHECK_FALSE(data.cached_signature_valid);
    CHECK_FALSE(data.temporal_state_valid);
    CHECK(data.render_count == 0);
    CHECK(data.last_frame_ms == 0.0);
    CHECK(data.avg_frame_ms == 0.0);
    CHECK(data.frame_time_samples == 0);

    data.cached_result.alpha = filled_alpha();
    data.cached_result.foreground = filled_foreground();
    data.cached_result_valid = true;
    data.temporal_alpha = filled_alpha();
    data.temporal_foreground = filled_foreground();
    data.temporal_state_valid = true;
    data.last_frame_ms = 22.0;
    data.avg_frame_ms = 18.0;
    data.frame_time_samples = 2;

    REQUIRE(end_sequence_render(reinterpret_cast<OfxImageEffectHandle>(&props), nullptr) ==
            kOfxStatOK);
    CHECK_FALSE(data.cached_result_valid);
    CHECK_FALSE(data.temporal_state_valid);
    CHECK(data.last_frame_ms == 22.0);
    CHECK(data.avg_frame_ms == 18.0);
    CHECK(data.frame_time_samples == 2);

    g_suites.property = previous_property_suite;
    g_suites.image_effect = previous_image_suite;
}

TEST_CASE("purge caches clears shared and instance caches", "[unit][ofx][regression]") {
    InstanceData data{};
    data.cached_result.alpha = filled_alpha();
    data.cached_result.foreground = filled_foreground();
    data.cached_result_valid = true;
    data.temporal_state_valid = true;
    data.render_count = 4;
    data.last_frame_ms = 11.0;
    data.avg_frame_ms = 10.0;
    data.frame_time_samples = 2;

    FakeEffectProps props{.instance_data = &data};

    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetPointer = fake_prop_get_pointer;
    property_suite.propSetPointer = fake_prop_set_pointer;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;

    auto* previous_property_suite = g_suites.property;
    auto* previous_image_suite = g_suites.image_effect;
    auto previous_cache = std::move(g_frame_cache);
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;
    g_frame_cache = std::make_unique<SharedFrameCache>();

    SharedCacheKey key{1, 2, 3, 0};
    auto alpha = filled_alpha();
    auto foreground = filled_foreground();
    std::vector<StageTiming> stage_timings = {
        StageTiming{"ort_run", 1400.0, 1, 1},
    };
    g_frame_cache->store(key, alpha.view(), foreground.view(), stage_timings);

    ImageBuffer retrieved_alpha;
    ImageBuffer retrieved_foreground;
    std::vector<StageTiming> retrieved_stage_timings;
    REQUIRE(g_frame_cache->try_retrieve(key, retrieved_alpha, retrieved_foreground,
                                        &retrieved_stage_timings));
    REQUIRE(retrieved_stage_timings.size() == 1);
    CHECK(retrieved_stage_timings.front().name == "ort_run");
    CHECK(retrieved_stage_timings.front().total_ms == Catch::Approx(1400.0));

    REQUIRE(purge_caches(reinterpret_cast<OfxImageEffectHandle>(&props)) == kOfxStatOK);
    CHECK_FALSE(data.cached_result_valid);
    CHECK_FALSE(data.temporal_state_valid);
    CHECK(data.render_count == 0);
    CHECK(data.last_frame_ms == 0.0);
    CHECK(data.avg_frame_ms == 0.0);
    CHECK(data.frame_time_samples == 0);
    CHECK_FALSE(g_frame_cache->try_retrieve(key, retrieved_alpha, retrieved_foreground,
                                            &retrieved_stage_timings));

    g_frame_cache = std::move(previous_cache);
    g_suites.property = previous_property_suite;
    g_suites.image_effect = previous_image_suite;
}

TEST_CASE("get regions of interest propagates the requested ROI to source clips",
          "[unit][ofx][regression]") {
    FakeRoiProps in_args{.roi = {10.0, 20.0, 110.0, 220.0}};
    FakeRoiProps out_args{};

    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetDoubleN = fake_prop_get_double_n;
    property_suite.propSetDoubleN = fake_prop_set_double_n;

    auto* previous_property_suite = g_suites.property;
    g_suites.property = &property_suite;

    REQUIRE(get_regions_of_interest(nullptr, reinterpret_cast<OfxPropertySetHandle>(&in_args),
                                    reinterpret_cast<OfxPropertySetHandle>(&out_args)) ==
            kOfxStatOK);

    const auto source_it =
        out_args.clip_rois.find(std::string("OfxImageClipPropRoI_") + kOfxImageEffectSimpleSourceClipName);
    REQUIRE(source_it != out_args.clip_rois.end());
    CHECK(source_it->second == in_args.roi);

    const auto hint_it =
        out_args.clip_rois.find(std::string("OfxImageClipPropRoI_") + kClipAlphaHint);
    REQUIRE(hint_it != out_args.clip_rois.end());
    CHECK(hint_it->second == in_args.roi);

    g_suites.property = previous_property_suite;
}

TEST_CASE("is identity remains conservative for CorridorKey output modes",
          "[unit][ofx][regression]") {
    REQUIRE(is_identity(nullptr, nullptr, nullptr) == kOfxStatReplyDefault);
}
