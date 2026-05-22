#include "adobe_effect_metadata.hpp"

#include "AE_Effect.h"

#if defined(_WIN32)
#define CORRIDORKEY_ADOBE_EXPORT extern "C" __declspec(dllexport)
#else
#define CORRIDORKEY_ADOBE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

using AdobeStatus = PF_Err;

constexpr AdobeStatus kAdobeStatusOk = static_cast<AdobeStatus>(0);
constexpr AdobeStatus kAdobeStatusFailed = static_cast<AdobeStatus>(1);

auto make_effect_version() noexcept -> A_long {
    return static_cast<A_long>(
        PF_VERSION(corridorkey::adobe::kEffectVersionMajor,
                   corridorkey::adobe::kEffectVersionMinor,
                   corridorkey::adobe::kEffectVersionBug,
                   corridorkey::adobe::kEffectVersionStage,
                   corridorkey::adobe::kEffectVersionBuild));
}

void setup_global(PF_OutData& output_data) noexcept {
    output_data.my_version = make_effect_version();
    output_data.out_flags =
        static_cast<PF_OutFlags>(corridorkey::adobe::kGlobalOutFlags);
    output_data.out_flags2 =
        static_cast<PF_OutFlags2>(corridorkey::adobe::kGlobalOutFlags2);
}

void setup_parameters(PF_OutData& output_data) noexcept {
    output_data.num_params = corridorkey::adobe::kEffectParameterCount;
}

}  // namespace

CORRIDORKEY_ADOBE_EXPORT AdobeStatus EffectMain(PF_Cmd command,
                                                PF_InData*,
                                                PF_OutData* output_data,
                                                PF_ParamDef*[],
                                                PF_LayerDef*,
                                                void*) noexcept {
    try {
        if (output_data == nullptr) {
            return kAdobeStatusOk;
        }

        switch (command) {
            case PF_Cmd_GLOBAL_SETUP:
                setup_global(*output_data);
                break;
            case PF_Cmd_PARAMS_SETUP:
                setup_parameters(*output_data);
                break;
            default:
                break;
        }
        return kAdobeStatusOk;
    } catch (...) {
        return kAdobeStatusFailed;
    }
}
