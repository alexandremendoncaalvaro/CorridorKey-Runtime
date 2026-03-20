#include <catch2/catch_all.hpp>

#include <corridorkey/engine.hpp>
#include <cstring>

#include "plugins/ofx/ofx_shared.hpp"
#include "plugins/ofx/ofx_runtime_client.hpp"

using namespace corridorkey::ofx;

TEST_CASE("runtime panel updates are deferred during render", "[unit][ofx][regression]") {
    InstanceData data{};
    data.in_render = true;
    data.runtime_panel_dirty = false;

    update_runtime_panel(&data);

    REQUIRE(data.runtime_panel_dirty);

    data.in_render = false;
    update_runtime_panel(&data);

    REQUIRE_FALSE(data.runtime_panel_dirty);
}

namespace {
struct FakeProps {
    std::string change_reason;
};

OfxStatus fake_prop_get_pointer(OfxPropertySetHandle handle, const char* name, int,
                                void** value) {
    if (handle == nullptr || value == nullptr || name == nullptr) {
        return kOfxStatErrBadHandle;
    }
    if (std::strcmp(name, kOfxPropInstanceData) != 0) {
        return kOfxStatErrBadHandle;
    }
    *value = handle;
    return kOfxStatOK;
}

OfxStatus fake_get_property_set(OfxImageEffectHandle handle, OfxPropertySetHandle* props) {
    if (props == nullptr) {
        return kOfxStatErrBadHandle;
    }
    *props = reinterpret_cast<OfxPropertySetHandle>(handle);
    return kOfxStatOK;
}

OfxStatus fake_prop_get_string(OfxPropertySetHandle handle, const char* name, int,
                               char** value) {
    if (handle == nullptr || value == nullptr || name == nullptr) {
        return kOfxStatErrBadHandle;
    }
    if (std::strcmp(name, kOfxPropChangeReason) != 0) {
        return kOfxStatErrBadHandle;
    }
    auto* props = reinterpret_cast<FakeProps*>(handle);
    *value = const_cast<char*>(props->change_reason.c_str());
    return kOfxStatOK;
}
}  // namespace

TEST_CASE("instance_changed ignores plugin-edited callbacks", "[unit][ofx][regression]") {
    InstanceData data{};
    data.runtime_panel_dirty = true;

    FakeProps args{};
    args.change_reason = kOfxChangePluginEdited;

    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetPointer = fake_prop_get_pointer;
    property_suite.propGetString = fake_prop_get_string;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;

    auto* previous_suite = g_suites.property;
    auto* previous_image_suite = g_suites.image_effect;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;

    auto status = instance_changed(reinterpret_cast<OfxImageEffectHandle>(&data),
                                   reinterpret_cast<OfxPropertySetHandle>(&args));

    REQUIRE(status == kOfxStatOK);
    REQUIRE(data.runtime_panel_dirty);

    g_suites.property = previous_suite;
    g_suites.image_effect = previous_image_suite;
}

TEST_CASE("instance_changed flushes pending updates for user edits", "[unit][ofx][regression]") {
    InstanceData data{};
    data.runtime_panel_dirty = true;

    FakeProps args{};
    args.change_reason = kOfxChangeUserEdited;

    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetPointer = fake_prop_get_pointer;
    property_suite.propGetString = fake_prop_get_string;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;

    auto* previous_suite = g_suites.property;
    auto* previous_image_suite = g_suites.image_effect;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;

    auto status = instance_changed(reinterpret_cast<OfxImageEffectHandle>(&data),
                                   reinterpret_cast<OfxPropertySetHandle>(&args));

    REQUIRE(status == kOfxStatOK);
    REQUIRE_FALSE(data.runtime_panel_dirty);

    g_suites.property = previous_suite;
    g_suites.image_effect = previous_image_suite;
}
