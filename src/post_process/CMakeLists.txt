# Pixel math implementation files (ColorUtils, Despill, Despeckle, Passthrough).
# Pure pixel math, ORT-free, lives in corridorkey_common.

target_sources(corridorkey_common PRIVATE
    alpha_edge.cpp
    color_utils.cpp
    despill.cpp
    despeckle.cpp
    source_passthrough.cpp
)
