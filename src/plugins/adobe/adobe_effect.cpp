#include <cstdio>
#include <string>

#include "AE_Effect.h"
#include "adobe_effect_metadata.hpp"
#include "adobe_effect_parameters.hpp"
#include "adobe_effect_render.hpp"

#if defined(_WIN32)
#define CORRIDORKEY_ADOBE_EXPORT extern "C" __declspec(dllexport)
#else
#define CORRIDORKEY_ADOBE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

using AdobeStatus = PF_Err;

constexpr AdobeStatus kAdobeStatusOk = static_cast<AdobeStatus>(0);
constexpr AdobeStatus kAdobeStatusFailed = PF_Err_INTERNAL_STRUCT_DAMAGED;
constexpr AdobeStatus kAdobeStatusUnsupported = PF_Err_BAD_CALLBACK_PARAM;

auto make_effect_version() noexcept -> A_long {
    return static_cast<A_long>(
        PF_VERSION(corridorkey::adobe::kEffectVersionMajor, corridorkey::adobe::kEffectVersionMinor,
                   corridorkey::adobe::kEffectVersionBug, corridorkey::adobe::kEffectVersionStage,
                   corridorkey::adobe::kEffectVersionBuild));
}

void setup_global(PF_OutData& output_data) noexcept {
    output_data.my_version = make_effect_version();
    output_data.out_flags = static_cast<PF_OutFlags>(corridorkey::adobe::kGlobalOutFlags);
    output_data.out_flags2 = static_cast<PF_OutFlags2>(corridorkey::adobe::kGlobalOutFlags2);
}

bool is_render_selector(PF_Cmd command) noexcept {
    return command == PF_Cmd_RENDER || command == PF_Cmd_SMART_PRE_RENDER ||
           command == PF_Cmd_SMART_RENDER;
}

void set_return_message(PF_OutData& output_data, const char* message) noexcept {
    std::snprintf(output_data.return_msg, sizeof(output_data.return_msg), "%s", message);
}

AdobeStatus reject_render(PF_OutData& output_data, const std::string& message) noexcept {
    set_return_message(output_data, message.c_str());
    return kAdobeStatusUnsupported;
}

}  // namespace

CORRIDORKEY_ADOBE_EXPORT AdobeStatus EffectMain(PF_Cmd command, PF_InData* input_data,
                                                PF_OutData* output_data, PF_ParamDef* parameters[],
                                                PF_LayerDef* output, void*) noexcept {
    try {
        if (output_data == nullptr) {
            return command == PF_Cmd_PARAMS_SETUP || is_render_selector(command)
                       ? kAdobeStatusUnsupported
                       : kAdobeStatusOk;
        }

        switch (command) {
            case PF_Cmd_ABOUT:
                set_return_message(*output_data, corridorkey::adobe::kEffectDisplayName);
                break;
            case PF_Cmd_GLOBAL_SETUP:
                setup_global(*output_data);
                break;
            case PF_Cmd_GLOBAL_SETDOWN:
            case PF_Cmd_SEQUENCE_SETUP:
            case PF_Cmd_SEQUENCE_RESETUP:
            case PF_Cmd_SEQUENCE_SETDOWN:
                break;
            case PF_Cmd_PARAMS_SETUP:
                return corridorkey::adobe::setup_effect_parameters(input_data, *output_data);
            case PF_Cmd_RENDER:
                return corridorkey::adobe::render_frame(input_data, *output_data, parameters,
                                                        output);
            case PF_Cmd_SMART_PRE_RENDER:
            case PF_Cmd_SMART_RENDER:
                return reject_render(*output_data,
                                     "CorridorKey SmartFX render is not enabled for this build.");
            default:
                break;
        }
        return kAdobeStatusOk;
    } catch (...) {
        return kAdobeStatusFailed;
    }
}
