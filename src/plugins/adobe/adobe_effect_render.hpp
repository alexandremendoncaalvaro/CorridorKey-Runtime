#pragma once

#include <filesystem>

#include "AE_Effect.h"

namespace corridorkey::adobe {

std::filesystem::path resolve_adobe_models_root(const std::filesystem::path& plugin_module_path);

PF_Err render_frame(PF_InData* input_data, PF_OutData& output_data, PF_ParamDef* parameters[],
                    PF_LayerDef* output);

PF_Err smart_pre_render(PF_InData* input_data, PF_OutData& output_data, void* extra);

PF_Err smart_render(PF_InData* input_data, PF_OutData& output_data, PF_ParamDef* parameters[],
                    void* extra);

}  // namespace corridorkey::adobe
