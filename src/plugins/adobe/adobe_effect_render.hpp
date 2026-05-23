#pragma once

#include "AE_Effect.h"

namespace corridorkey::adobe {

PF_Err render_frame(PF_InData* input_data, PF_OutData& output_data, PF_ParamDef* parameters[],
                    PF_LayerDef* output);

PF_Err smart_pre_render(PF_InData* input_data, PF_OutData& output_data, void* extra);

}  // namespace corridorkey::adobe
