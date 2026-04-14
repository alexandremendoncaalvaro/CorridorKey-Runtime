#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <cstdarg>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "plugins/ofx/ofx_runtime_client.hpp"
#include "plugins/ofx/ofx_shared.hpp"

using namespace corridorkey::ofx;

namespace {

struct FakeParamValue;

struct FakePropertySet {
    FakeParamValue* owner_param = nullptr;
    std::unordered_map<std::string, std::vector<std::string>> strings;
    std::unordered_map<std::string, std::vector<int>> ints;
    std::unordered_map<std::string, std::vector<double>> doubles;
    std::unordered_map<std::string, std::vector<void*>> pointers;
};

struct FakeParamValue {
    enum class Kind { Integer, Double, Boolean, String, Choice, Group, PushButton };

    explicit FakeParamValue(Kind param_kind) : kind(param_kind) {
        props.owner_param = this;
    }

    Kind kind = Kind::String;
    FakePropertySet props = {};
    int int_value = 0;
    double double_value = 0.0;
    std::string string_value = {};
};

struct FakeParamSet {
    std::unordered_map<std::string, std::unique_ptr<FakeParamValue>> params;
    std::vector<std::string> define_order;
};

struct FakeEffect {
    FakePropertySet props = {};
    FakeParamSet param_set = {};
    std::unordered_map<std::string, std::unique_ptr<FakePropertySet>> clips;
};

template <typename T>
void ensure_slot(std::unordered_map<std::string, std::vector<T>>& values, const char* name,
                 int index) {
    auto& slots = values[name == nullptr ? "" : name];
    if (index >= static_cast<int>(slots.size())) {
        slots.resize(static_cast<std::size_t>(index) + 1);
    }
}

FakePropertySet* as_props(OfxPropertySetHandle handle) {
    return reinterpret_cast<FakePropertySet*>(handle);
}

FakeParamValue* as_param(OfxParamHandle handle) {
    return reinterpret_cast<FakeParamValue*>(handle);
}

FakeEffect* as_effect(OfxImageEffectHandle handle) {
    return reinterpret_cast<FakeEffect*>(handle);
}

FakePropertySet* define_clip(FakeEffect& effect, const char* name) {
    auto props = std::make_unique<FakePropertySet>();
    auto* raw = props.get();
    effect.clips[name == nullptr ? "" : name] = std::move(props);
    return raw;
}

FakeParamValue* define_param(FakeEffect& effect, const char* name, FakeParamValue::Kind kind) {
    auto param = std::make_unique<FakeParamValue>(kind);
    auto* raw = param.get();
    effect.param_set.params[name == nullptr ? "" : name] = std::move(param);
    return raw;
}

FakeParamValue::Kind param_kind_for_type(const char* param_type) {
    if (std::strcmp(param_type, kOfxParamTypeInteger) == 0) {
        return FakeParamValue::Kind::Integer;
    }
    if (std::strcmp(param_type, kOfxParamTypeDouble) == 0) {
        return FakeParamValue::Kind::Double;
    }
    if (std::strcmp(param_type, kOfxParamTypeBoolean) == 0) {
        return FakeParamValue::Kind::Boolean;
    }
    if (std::strcmp(param_type, kOfxParamTypeChoice) == 0) {
        return FakeParamValue::Kind::Choice;
    }
    if (std::strcmp(param_type, kOfxParamTypeGroup) == 0) {
        return FakeParamValue::Kind::Group;
    }
    if (std::strcmp(param_type, kOfxParamTypePushButton) == 0) {
        return FakeParamValue::Kind::PushButton;
    }
    return FakeParamValue::Kind::String;
}

void sync_param_default(FakePropertySet* props, const char* name, int index) {
    if (props == nullptr || props->owner_param == nullptr || index != 0 || name == nullptr ||
        std::strcmp(name, kOfxParamPropDefault) != 0) {
        return;
    }

    FakeParamValue* param = props->owner_param;
    switch (param->kind) {
        case FakeParamValue::Kind::Integer:
        case FakeParamValue::Kind::Boolean:
        case FakeParamValue::Kind::Choice: {
            auto it = props->ints.find(name);
            if (it != props->ints.end() && !it->second.empty()) {
                param->int_value = it->second.front();
            }
            break;
        }
        case FakeParamValue::Kind::Double: {
            auto it = props->doubles.find(name);
            if (it != props->doubles.end() && !it->second.empty()) {
                param->double_value = it->second.front();
            }
            break;
        }
        case FakeParamValue::Kind::String: {
            auto it = props->strings.find(name);
            if (it != props->strings.end() && !it->second.empty()) {
                param->string_value = it->second.front();
            }
            break;
        }
        case FakeParamValue::Kind::Group:
        case FakeParamValue::Kind::PushButton:
            break;
    }
}

OfxStatus fake_prop_set_string(OfxPropertySetHandle handle, const char* name, int index,
                               const char* value) {
    auto* props = as_props(handle);
    if (props == nullptr || name == nullptr || index < 0) {
        return kOfxStatErrBadHandle;
    }
    ensure_slot(props->strings, name, index);
    props->strings[name][index] = value == nullptr ? "" : value;
    sync_param_default(props, name, index);
    return kOfxStatOK;
}

OfxStatus fake_prop_set_string_n(OfxPropertySetHandle handle, const char* name, int count,
                                 const char* const* values) {
    if (count < 0) {
        return kOfxStatErrBadHandle;
    }
    for (int index = 0; index < count; ++index) {
        fake_prop_set_string(handle, name, index, values == nullptr ? nullptr : values[index]);
    }
    return kOfxStatOK;
}

OfxStatus fake_prop_get_string(OfxPropertySetHandle handle, const char* name, int index,
                               char** value) {
    auto* props = as_props(handle);
    if (props == nullptr || name == nullptr || value == nullptr || index < 0) {
        return kOfxStatErrBadHandle;
    }
    auto it = props->strings.find(name);
    if (it == props->strings.end() || index >= static_cast<int>(it->second.size())) {
        return kOfxStatErrBadHandle;
    }
    *value = const_cast<char*>(it->second[static_cast<std::size_t>(index)].c_str());
    return kOfxStatOK;
}

OfxStatus fake_prop_set_int(OfxPropertySetHandle handle, const char* name, int index, int value) {
    auto* props = as_props(handle);
    if (props == nullptr || name == nullptr || index < 0) {
        return kOfxStatErrBadHandle;
    }
    ensure_slot(props->ints, name, index);
    props->ints[name][index] = value;
    sync_param_default(props, name, index);
    return kOfxStatOK;
}

OfxStatus fake_prop_set_int_n(OfxPropertySetHandle handle, const char* name, int count,
                              const int* values) {
    if (count < 0) {
        return kOfxStatErrBadHandle;
    }
    for (int index = 0; index < count; ++index) {
        fake_prop_set_int(handle, name, index, values == nullptr ? 0 : values[index]);
    }
    return kOfxStatOK;
}

OfxStatus fake_prop_get_int(OfxPropertySetHandle handle, const char* name, int index, int* value) {
    auto* props = as_props(handle);
    if (props == nullptr || name == nullptr || value == nullptr || index < 0) {
        return kOfxStatErrBadHandle;
    }
    auto it = props->ints.find(name);
    if (it == props->ints.end() || index >= static_cast<int>(it->second.size())) {
        return kOfxStatErrBadHandle;
    }
    *value = it->second[static_cast<std::size_t>(index)];
    return kOfxStatOK;
}

OfxStatus fake_prop_set_double(OfxPropertySetHandle handle, const char* name, int index,
                               double value) {
    auto* props = as_props(handle);
    if (props == nullptr || name == nullptr || index < 0) {
        return kOfxStatErrBadHandle;
    }
    ensure_slot(props->doubles, name, index);
    props->doubles[name][index] = value;
    sync_param_default(props, name, index);
    return kOfxStatOK;
}

OfxStatus fake_prop_set_pointer(OfxPropertySetHandle handle, const char* name, int index,
                                void* value) {
    auto* props = as_props(handle);
    if (props == nullptr || name == nullptr || index < 0) {
        return kOfxStatErrBadHandle;
    }
    ensure_slot(props->pointers, name, index);
    props->pointers[name][index] = value;
    return kOfxStatOK;
}

OfxStatus fake_prop_get_pointer(OfxPropertySetHandle handle, const char* name, int index,
                                void** value) {
    auto* props = as_props(handle);
    if (props == nullptr || name == nullptr || value == nullptr || index < 0) {
        return kOfxStatErrBadHandle;
    }
    auto it = props->pointers.find(name);
    if (it == props->pointers.end() || index >= static_cast<int>(it->second.size())) {
        return kOfxStatErrBadHandle;
    }
    *value = it->second[static_cast<std::size_t>(index)];
    return kOfxStatOK;
}

OfxStatus fake_get_property_set(OfxImageEffectHandle handle, OfxPropertySetHandle* props) {
    if (handle == nullptr || props == nullptr) {
        return kOfxStatErrBadHandle;
    }
    *props = reinterpret_cast<OfxPropertySetHandle>(&as_effect(handle)->props);
    return kOfxStatOK;
}

OfxStatus fake_clip_define(OfxImageEffectHandle handle, const char* name,
                           OfxPropertySetHandle* props) {
    if (handle == nullptr || props == nullptr || name == nullptr) {
        return kOfxStatErrBadHandle;
    }
    *props = reinterpret_cast<OfxPropertySetHandle>(define_clip(*as_effect(handle), name));
    return kOfxStatOK;
}

OfxStatus fake_get_param_set(OfxImageEffectHandle handle, OfxParamSetHandle* param_set) {
    if (handle == nullptr || param_set == nullptr) {
        return kOfxStatErrBadHandle;
    }
    *param_set = reinterpret_cast<OfxParamSetHandle>(&as_effect(handle)->param_set);
    return kOfxStatOK;
}

OfxStatus fake_clip_get_property_set(OfxImageClipHandle clip, OfxPropertySetHandle* props) {
    if (clip == nullptr || props == nullptr) {
        return kOfxStatErrBadHandle;
    }
    *props = reinterpret_cast<OfxPropertySetHandle>(clip);
    return kOfxStatOK;
}

OfxStatus fake_param_define(OfxParamSetHandle handle, const char* param_type, const char* name,
                            OfxPropertySetHandle* props) {
    auto* param_set = reinterpret_cast<FakeParamSet*>(handle);
    if (param_set == nullptr || param_type == nullptr || name == nullptr || props == nullptr) {
        return kOfxStatErrBadHandle;
    }
    auto param = std::make_unique<FakeParamValue>(param_kind_for_type(param_type));
    auto* raw = param.get();
    param_set->define_order.emplace_back(name);
    param_set->params[name] = std::move(param);
    *props = reinterpret_cast<OfxPropertySetHandle>(&raw->props);
    return kOfxStatOK;
}

OfxStatus fake_param_get_handle(OfxParamSetHandle handle, const char* name, OfxParamHandle* param,
                                OfxPropertySetHandle* props) {
    auto* param_set = reinterpret_cast<FakeParamSet*>(handle);
    if (param_set == nullptr || name == nullptr || param == nullptr) {
        return kOfxStatErrBadHandle;
    }
    auto it = param_set->params.find(name);
    if (it == param_set->params.end()) {
        return kOfxStatErrBadHandle;
    }
    *param = reinterpret_cast<OfxParamHandle>(it->second.get());
    if (props != nullptr) {
        *props = reinterpret_cast<OfxPropertySetHandle>(&it->second->props);
    }
    return kOfxStatOK;
}

OfxStatus fake_param_get_property_set(OfxParamHandle param, OfxPropertySetHandle* props) {
    if (param == nullptr || props == nullptr) {
        return kOfxStatErrBadHandle;
    }
    *props = reinterpret_cast<OfxPropertySetHandle>(&as_param(param)->props);
    return kOfxStatOK;
}

OfxStatus fake_param_get_value(OfxParamHandle param, ...) {
    auto* fake_param = as_param(param);
    if (fake_param == nullptr) {
        return kOfxStatErrBadHandle;
    }

    va_list args;
    va_start(args, param);
    switch (fake_param->kind) {
        case FakeParamValue::Kind::Integer:
        case FakeParamValue::Kind::Boolean:
        case FakeParamValue::Kind::Choice: {
            int* value = va_arg(args, int*);
            if (value == nullptr) {
                va_end(args);
                return kOfxStatErrBadHandle;
            }
            *value = fake_param->int_value;
            break;
        }
        case FakeParamValue::Kind::Double: {
            double* value = va_arg(args, double*);
            if (value == nullptr) {
                va_end(args);
                return kOfxStatErrBadHandle;
            }
            *value = fake_param->double_value;
            break;
        }
        case FakeParamValue::Kind::String: {
            char** value = va_arg(args, char**);
            if (value == nullptr) {
                va_end(args);
                return kOfxStatErrBadHandle;
            }
            *value = const_cast<char*>(fake_param->string_value.c_str());
            break;
        }
        case FakeParamValue::Kind::Group:
        case FakeParamValue::Kind::PushButton:
            break;
    }
    va_end(args);
    return kOfxStatOK;
}

OfxStatus fake_param_set_value(OfxParamHandle param, ...) {
    auto* fake_param = as_param(param);
    if (fake_param == nullptr) {
        return kOfxStatErrBadHandle;
    }

    va_list args;
    va_start(args, param);
    switch (fake_param->kind) {
        case FakeParamValue::Kind::Integer:
        case FakeParamValue::Kind::Boolean:
        case FakeParamValue::Kind::Choice:
            fake_param->int_value = va_arg(args, int);
            break;
        case FakeParamValue::Kind::Double:
            fake_param->double_value = va_arg(args, double);
            break;
        case FakeParamValue::Kind::String: {
            const char* value = va_arg(args, const char*);
            fake_param->string_value = value == nullptr ? "" : value;
            break;
        }
        case FakeParamValue::Kind::Group:
        case FakeParamValue::Kind::PushButton:
            break;
    }
    va_end(args);
    return kOfxStatOK;
}

struct SuiteScope {
    SuiteScope() : previous(g_suites) {
        property_suite.propSetString = fake_prop_set_string;
        property_suite.propSetStringN = fake_prop_set_string_n;
        property_suite.propGetString = fake_prop_get_string;
        property_suite.propSetInt = fake_prop_set_int;
        property_suite.propSetIntN = fake_prop_set_int_n;
        property_suite.propGetInt = fake_prop_get_int;
        property_suite.propSetDouble = fake_prop_set_double;
        property_suite.propSetPointer = fake_prop_set_pointer;
        property_suite.propGetPointer = fake_prop_get_pointer;

        image_suite.getPropertySet = fake_get_property_set;
        image_suite.clipDefine = fake_clip_define;
        image_suite.getParamSet = fake_get_param_set;
        image_suite.clipGetPropertySet = fake_clip_get_property_set;

        parameter_suite.paramDefine = fake_param_define;
        parameter_suite.paramGetHandle = fake_param_get_handle;
        parameter_suite.paramGetPropertySet = fake_param_get_property_set;
        parameter_suite.paramGetValue = fake_param_get_value;
        parameter_suite.paramSetValue = fake_param_set_value;

        g_suites.property = &property_suite;
        g_suites.image_effect = &image_suite;
        g_suites.parameter = &parameter_suite;
    }

    ~SuiteScope() {
        g_suites = previous;
    }

    OfxSuites previous = {};
    OfxPropertySuiteV1 property_suite = {};
    OfxImageEffectSuiteV1 image_suite = {};
    OfxParameterSuiteV1 parameter_suite = {};
};

const std::vector<std::string>& prop_strings(const FakePropertySet& props, const char* name) {
    static const std::vector<std::string> empty;
    auto it = props.strings.find(name == nullptr ? "" : name);
    return it == props.strings.end() ? empty : it->second;
}

const std::vector<int>& prop_ints(const FakePropertySet& props, const char* name) {
    static const std::vector<int> empty;
    auto it = props.ints.find(name == nullptr ? "" : name);
    return it == props.ints.end() ? empty : it->second;
}

std::string clip_property_key(const char* property_name, const char* clip_name) {
    return std::string(property_name) + "_" + clip_name;
}

}  // namespace

TEST_CASE("ofx descriptor advertises core colour management support", "[unit][ofx][regression]") {
    SuiteScope suites;
    FakeEffect descriptor;

    REQUIRE(describe(reinterpret_cast<OfxImageEffectHandle>(&descriptor)) == kOfxStatOK);
    CHECK(prop_strings(descriptor.props, kOfxImageEffectPropColourManagementStyle).front() ==
          kOfxImageEffectColourManagementCore);
    CHECK(prop_strings(descriptor.props, kOfxImageEffectPropColourManagementAvailableConfigs)
              .front() == kOfxConfigIdentifier);
}

TEST_CASE("describe_in_context places recover original details in interior detail",
          "[unit][ofx][regression]") {
    SuiteScope suites;
    FakeEffect descriptor;

    REQUIRE(describe_in_context(reinterpret_cast<OfxImageEffectHandle>(&descriptor),
                                kOfxImageEffectContextFilter) == kOfxStatOK);

    const auto& recover_props = descriptor.param_set.params.at(kParamSourcePassthrough)->props;
    const auto& edge_erode_props = descriptor.param_set.params.at(kParamEdgeErode)->props;
    const auto& despill_props = descriptor.param_set.params.at(kParamDespillStrength)->props;
    const auto& gamma_props = descriptor.param_set.params.at(kParamAlphaGamma)->props;
    const auto& spill_method_props = descriptor.param_set.params.at(kParamSpillMethod)->props;
    const auto& render_timeout_props = descriptor.param_set.params.at(kParamRenderTimeout)->props;
    const auto& input_color_props = descriptor.param_set.params.at(kParamInputColorSpace)->props;

    CHECK(prop_strings(recover_props, kOfxParamPropParent).front() == "interior_detail_group");
    CHECK(prop_strings(edge_erode_props, kOfxParamPropParent).front() ==
          "advanced_interior_detail_group");
    CHECK(prop_strings(despill_props, kOfxParamPropParent).front() == "edge_spill_group");
    CHECK(prop_strings(gamma_props, kOfxParamPropParent).front() == "advanced_matte_group");
    CHECK(prop_strings(spill_method_props, kOfxParamPropParent).front() ==
          "advanced_processing_group");
    CHECK(prop_strings(render_timeout_props, kOfxParamPropParent).front() ==
          "advanced_runtime_group");
    CHECK(prop_strings(gamma_props, kOfxParamPropHint).front().find("Values above 1.0 brighten") !=
          std::string::npos);
    CHECK(prop_strings(input_color_props, kOfxParamPropChoiceOption).size() == 3);
    CHECK(prop_strings(input_color_props, kOfxParamPropChoiceOption).at(2) == "Host Managed");
    CHECK(prop_ints(input_color_props, kOfxParamPropDefault).front() == kDefaultInputColorSpace);
}

TEST_CASE("describe_in_context exposes the current public quality ladder",
          "[unit][ofx][regression]") {
    SuiteScope suites;
    FakeEffect descriptor;

    REQUIRE(describe_in_context(reinterpret_cast<OfxImageEffectHandle>(&descriptor),
                                kOfxImageEffectContextFilter) == kOfxStatOK);

    const auto& quality_props = descriptor.param_set.params.at(kParamQualityMode)->props;
    const auto& coarse_resolution_props =
        descriptor.param_set.params.at(kParamCoarseResolutionOverride)->props;

    CHECK(prop_strings(quality_props, kOfxParamPropChoiceOption) ==
          std::vector<std::string>{"Recommended", "Draft (512)", "High (1024)", "Ultra (1536)",
                                   "Maximum (2048)"});
    CHECK(prop_ints(quality_props, kOfxParamPropDefault).front() == kQualityPreview);
    CHECK(prop_strings(coarse_resolution_props, kOfxParamPropChoiceOption) ==
          std::vector<std::string>{"Recommended", "512", "1024", "1536", "2048"});
}

TEST_CASE("describe_in_context keeps runtime first and help second with advanced diagnostics gated",
          "[unit][ofx][regression]") {
    SuiteScope suites;
    FakeEffect descriptor;

    REQUIRE(describe_in_context(reinterpret_cast<OfxImageEffectHandle>(&descriptor),
                                kOfxImageEffectContextFilter) == kOfxStatOK);

    REQUIRE(descriptor.param_set.define_order.size() >= 2);
    std::vector<std::string> group_order;
    for (const auto& name : descriptor.param_set.define_order) {
        auto it = descriptor.param_set.params.find(name);
        REQUIRE(it != descriptor.param_set.params.end());
        if (it->second->kind == FakeParamValue::Kind::Group) {
            group_order.push_back(name);
        }
    }
    REQUIRE(group_order.size() >= 2);
    CHECK(group_order.front() == "runtime_group");
    CHECK(group_order.at(1) == kParamHelpGroup);

    const auto& runtime_group_props = descriptor.param_set.params.at("runtime_group")->props;
    const auto& help_group_props = descriptor.param_set.params.at(kParamHelpGroup)->props;
    const auto& runtime_details_group_props =
        descriptor.param_set.params.at("runtime_details_group")->props;
    const auto& guide_source_props =
        descriptor.param_set.params.at(kParamRuntimeGuideSource)->props;
    const auto& requested_quality_props =
        descriptor.param_set.params.at(kParamRuntimeRequestedQuality)->props;
    const auto& ceiling_props =
        descriptor.param_set.params.at(kParamRuntimeSafeQualityCeiling)->props;
    const auto& runtime_path_props = descriptor.param_set.params.at(kParamRuntimePath)->props;
    const auto& backend_work_props =
        descriptor.param_set.params.at(kParamRuntimeBackendWork)->props;
    const auto& refinement_props = descriptor.param_set.params.at(kParamRefinementMode)->props;
    const auto& advanced_group_props = descriptor.param_set.params.at("advanced_group")->props;
    const auto& advanced_detail_group_props =
        descriptor.param_set.params.at("advanced_interior_detail_group")->props;
    const auto& advanced_matte_group_props =
        descriptor.param_set.params.at("advanced_matte_group")->props;
    const auto& advanced_processing_group_props =
        descriptor.param_set.params.at("advanced_processing_group")->props;
    const auto& advanced_runtime_group_props =
        descriptor.param_set.params.at("advanced_runtime_group")->props;

    CHECK(prop_ints(runtime_group_props, kOfxParamPropGroupOpen).front() == 1);
    CHECK(prop_ints(help_group_props, kOfxParamPropGroupOpen).front() == 0);
    CHECK(prop_strings(runtime_details_group_props, kOfxParamPropParent).front() ==
          "runtime_group");
    CHECK(prop_ints(runtime_details_group_props, kOfxParamPropGroupOpen).front() == 0);
    CHECK(prop_strings(guide_source_props, kOfxParamPropParent).front() == "runtime_group");
    CHECK(prop_strings(requested_quality_props, kOfxParamPropParent).front() ==
          "runtime_details_group");
    CHECK(prop_strings(ceiling_props, kOfxParamPropParent).front() == "runtime_details_group");
    CHECK(prop_strings(runtime_path_props, kOfxParamPropParent).front() == "runtime_details_group");
    CHECK(prop_strings(backend_work_props, kOfxParamPropParent).front() == "runtime_details_group");
    CHECK(prop_ints(advanced_group_props, kOfxParamPropGroupOpen).front() == 0);
    CHECK(prop_strings(advanced_detail_group_props, kOfxParamPropParent).front() ==
          "advanced_group");
    CHECK(prop_strings(advanced_matte_group_props, kOfxParamPropParent).front() ==
          "advanced_group");
    CHECK(prop_strings(advanced_processing_group_props, kOfxParamPropParent).front() ==
          "advanced_group");
    CHECK(prop_strings(advanced_runtime_group_props, kOfxParamPropParent).front() ==
          "advanced_group");
    CHECK(prop_ints(advanced_detail_group_props, kOfxParamPropGroupOpen).front() == 0);
    CHECK(prop_ints(advanced_matte_group_props, kOfxParamPropGroupOpen).front() == 0);
    CHECK(prop_ints(advanced_processing_group_props, kOfxParamPropGroupOpen).front() == 0);
    CHECK(prop_ints(advanced_runtime_group_props, kOfxParamPropGroupOpen).front() == 0);
    CHECK(prop_strings(refinement_props, kOfxParamPropParent).front() ==
          "advanced_processing_group");
    CHECK(prop_ints(refinement_props, kOfxParamPropEnabled).front() == 0);
}

TEST_CASE("clip preferences request host-managed source colourspaces and raw alpha hint",
          "[unit][ofx][regression]") {
    SuiteScope suites;
    FakeEffect instance;
    FakePropertySet* source_clip = define_clip(instance, kOfxImageEffectSimpleSourceClipName);
    source_clip->strings[kOfxImageEffectPropPixelDepth] = {kOfxBitDepthFloat};

    InstanceData data{};
    data.source_clip = reinterpret_cast<OfxImageClipHandle>(source_clip);
    set_instance_data(reinterpret_cast<OfxImageEffectHandle>(&instance), &data);

    FakePropertySet out_args;
    REQUIRE(get_clip_preferences(reinterpret_cast<OfxImageEffectHandle>(&instance),
                                 reinterpret_cast<OfxPropertySetHandle>(&out_args)) == kOfxStatOK);

    CHECK(prop_strings(out_args, clip_property_key(kOfxImageClipPropPreferredColourspaces,
                                                   kOfxImageEffectSimpleSourceClipName)
                                     .c_str()) ==
          std::vector<std::string>{kOfxColourspaceSrgbTx, kOfxColourspaceLinRec709Srgb});
    CHECK(prop_strings(
              out_args,
              clip_property_key(kOfxImageClipPropPreferredColourspaces, kClipAlphaHint).c_str()) ==
          std::vector<std::string>{kOfxColourspaceRaw});
}

TEST_CASE("output colourspace action follows output mode", "[unit][ofx][regression]") {
    SuiteScope suites;
    FakeEffect instance;
    FakeParamValue* output_mode =
        define_param(instance, kParamOutputMode, FakeParamValue::Kind::Choice);

    InstanceData data{};
    data.output_mode_param = reinterpret_cast<OfxParamHandle>(output_mode);
    set_instance_data(reinterpret_cast<OfxImageEffectHandle>(&instance), &data);

    FakePropertySet out_args;

    SECTION("processed outputs scene-linear") {
        output_mode->int_value = kOutputProcessed;
        REQUIRE(get_output_colourspace(reinterpret_cast<OfxImageEffectHandle>(&instance), nullptr,
                                       reinterpret_cast<OfxPropertySetHandle>(&out_args)) ==
                kOfxStatOK);
        CHECK(prop_strings(out_args, kOfxImageClipPropColourspace).front() ==
              kOfxColourspaceLinRec709Srgb);
    }

    SECTION("matte only stays raw") {
        output_mode->int_value = kOutputMatteOnly;
        REQUIRE(get_output_colourspace(reinterpret_cast<OfxImageEffectHandle>(&instance), nullptr,
                                       reinterpret_cast<OfxPropertySetHandle>(&out_args)) ==
                kOfxStatOK);
        CHECK(prop_strings(out_args, kOfxImageClipPropColourspace).front() == kOfxColourspaceRaw);
    }
}

TEST_CASE("input color auto resolves supported host-managed colourspaces and falls back cleanly",
          "[unit][ofx][regression]") {
    CHECK(resolve_input_color_runtime_mode(kInputColorAutoHostManaged, kOfxColourspaceSrgbTx) ==
          InputColorRuntimeMode::HostManagedSrgbTx);
    CHECK(resolve_input_color_runtime_mode(kInputColorAutoHostManaged,
                                           kOfxColourspaceLinRec709Srgb) ==
          InputColorRuntimeMode::HostManagedLinearRec709Srgb);
    CHECK(resolve_input_color_runtime_mode(kInputColorAutoHostManaged, "davinci_intermediate") ==
          InputColorRuntimeMode::AutoFallbackLinear);
    CHECK(resolve_input_color_runtime_mode(kInputColorLinear, kOfxColourspaceSrgbTx) ==
          InputColorRuntimeMode::ManualLinear);
    CHECK(resolve_input_color_runtime_mode(kInputColorSrgb, kOfxColourspaceLinRec709Srgb) ==
          InputColorRuntimeMode::ManualSrgb);
}

TEST_CASE("resolution-relative controls scale from a 1920px baseline", "[unit][ofx][regression]") {
    CHECK(source_long_edge_scale_factor(1920, 1080) == Catch::Approx(1.0));
    CHECK(source_long_edge_scale_factor(7680, 4320) == Catch::Approx(4.0));
    CHECK(scale_pixels_to_source_long_edge(10.0, 1920, 1080) == Catch::Approx(10.0));
    CHECK(scale_pixels_to_source_long_edge(10.0, 7680, 4320) == Catch::Approx(40.0));
    CHECK(scale_integer_pixels_to_source_long_edge(7, 7680, 4320) == 28);
}

TEST_CASE("runtime panel wording exposes auto target and color status", "[unit][ofx][regression]") {
    InstanceData data{};
    data.active_quality_mode = kQualityAuto;
    data.requested_resolution = 2048;
    data.color_management_status = "Color: Host Managed (Linear Rec.709 (sRGB))";
    data.last_warning = "Auto target 2048px unavailable on this hardware -- using 1536px";
    data.last_frame_ms = 14.0;
    data.avg_frame_ms = 16.0;

    CHECK(requested_quality_runtime_label(data.active_quality_mode, data.requested_resolution,
                                          data.cpu_quality_guardrail_active)
              .find("source-size target: 2048px") != std::string::npos);
    const std::string status = runtime_status_runtime_label(data);
    CHECK(status.find("Color: Host Managed") != std::string::npos);
    CHECK(status.find("Note:") != std::string::npos);
}
