#include <cstdint>
#include <cstdio>
#include <string>

#include "AE_Effect.h"
#include "AE_EffectCB.h"
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
constexpr std::uint32_t kSequenceStateVersion = 1;

struct AdobeSequenceState {
    std::uint32_t version = kSequenceStateVersion;
    std::uint32_t reserved = 0;
};

struct HostHandleCallbacks {
    PF_Handle (*new_handle)(A_u_longlong) = nullptr;
    void* (*lock_handle)(PF_Handle) = nullptr;
    void (*unlock_handle)(PF_Handle) = nullptr;
    void (*dispose_handle)(PF_Handle) = nullptr;
};

HostHandleCallbacks handle_callbacks_for(PF_InData* input_data) noexcept {
    if (input_data == nullptr || input_data->utils == nullptr) {
        return {};
    }
    return HostHandleCallbacks{
        .new_handle = input_data->utils->host_new_handle,
        .lock_handle = input_data->utils->host_lock_handle,
        .unlock_handle = input_data->utils->host_unlock_handle,
        .dispose_handle = input_data->utils->host_dispose_handle,
    };
}

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

AdobeStatus setup_sequence(PF_InData* input_data, PF_OutData& output_data) noexcept {
    const auto callbacks = handle_callbacks_for(input_data);
    if (callbacks.new_handle == nullptr || callbacks.lock_handle == nullptr ||
        callbacks.unlock_handle == nullptr || callbacks.dispose_handle == nullptr) {
        set_return_message(output_data,
                           "CorridorKey sequence state requires host handle callbacks.");
        return kAdobeStatusUnsupported;
    }

    PF_Handle sequence_data = callbacks.new_handle(sizeof(AdobeSequenceState));
    if (sequence_data == nullptr) {
        set_return_message(output_data, "CorridorKey could not allocate sequence state.");
        return PF_Err_OUT_OF_MEMORY;
    }

    auto* state = static_cast<AdobeSequenceState*>(callbacks.lock_handle(sequence_data));
    if (state == nullptr) {
        callbacks.dispose_handle(sequence_data);
        set_return_message(output_data, "CorridorKey could not lock sequence state.");
        return kAdobeStatusFailed;
    }

    *state = AdobeSequenceState{};
    callbacks.unlock_handle(sequence_data);
    output_data.sequence_data = sequence_data;
    output_data.flat_sdata_size = static_cast<A_long>(sizeof(AdobeSequenceState));
    return kAdobeStatusOk;
}

AdobeStatus setdown_sequence(PF_InData* input_data, PF_OutData& output_data) noexcept {
    const auto callbacks = handle_callbacks_for(input_data);
    if (input_data != nullptr && input_data->sequence_data != nullptr &&
        callbacks.dispose_handle != nullptr) {
        callbacks.dispose_handle(input_data->sequence_data);
        input_data->sequence_data = nullptr;
    }
    output_data.sequence_data = nullptr;
    return kAdobeStatusOk;
}

AdobeStatus resetup_sequence(PF_InData* input_data, PF_OutData& output_data) noexcept {
    const AdobeStatus setdown_status = setdown_sequence(input_data, output_data);
    if (setdown_status != kAdobeStatusOk) {
        return setdown_status;
    }
    return setup_sequence(input_data, output_data);
}

}  // namespace

CORRIDORKEY_ADOBE_EXPORT AdobeStatus EffectMain(PF_Cmd command, PF_InData* input_data,
                                                PF_OutData* output_data, PF_ParamDef* parameters[],
                                                PF_LayerDef* output, void* extra) noexcept {
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
                break;
            case PF_Cmd_SEQUENCE_SETUP:
                return setup_sequence(input_data, *output_data);
            case PF_Cmd_SEQUENCE_RESETUP:
                return resetup_sequence(input_data, *output_data);
            case PF_Cmd_SEQUENCE_SETDOWN:
                return setdown_sequence(input_data, *output_data);
            case PF_Cmd_PARAMS_SETUP:
                return corridorkey::adobe::setup_effect_parameters(input_data, *output_data);
            case PF_Cmd_RENDER:
                return corridorkey::adobe::render_frame(input_data, *output_data, parameters,
                                                        output);
            case PF_Cmd_SMART_PRE_RENDER:
                return corridorkey::adobe::smart_pre_render(input_data, *output_data, extra);
            case PF_Cmd_SMART_RENDER:
                return corridorkey::adobe::smart_render(input_data, *output_data, parameters,
                                                        extra);
            default:
                break;
        }
        return kAdobeStatusOk;
    } catch (...) {
        return kAdobeStatusFailed;
    }
}
