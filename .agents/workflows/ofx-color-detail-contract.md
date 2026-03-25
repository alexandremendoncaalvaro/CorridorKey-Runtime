---
description: [OFX color and detail contract with Windows-only validation scope]
---

# OFX Color and Detail Contract Workflow

This workflow exists to keep Windows work focused on validation of host and
runtime behavior after the OFX contract itself is already implemented and
covered locally.

## 1. Current Contract

The OFX plugin now follows this contract:

- `Input Color Space` supports `sRGB`, `Linear`, and `Auto (Host Managed)`.
- `Auto (Host Managed)` accepts only `srgb_tx` and `lin_rec709_srgb`.
- If `Auto (Host Managed)` cannot negotiate one of those colourspaces, it
  falls back to manual `Linear Rec.709 (sRGB)` and reports that in the runtime
  panel.
- `GetClipPreferences` requests:
  - `Source`: `srgb_tx`, then `lin_rec709_srgb`
  - `Alpha Hint`: `Raw`
- `GetOutputColourspace` returns:
  - `Processed`, `FG+Matte`, `Foreground Only`, `Source+Matte`:
    `lin_rec709_srgb`
  - `Matte Only`: `Raw`
- `Recover Original Details` remains the feature name, but it now belongs to
  `Interior Detail`, not `Edge & Spill`.
- `Matte Shrink/Grow`, `Matte Edge Blur`, `Details Edge Shrink`, and
  `Details Edge Feather` scale from the source long edge with a `1920px`
  baseline.
- `Details Edge Shrink` and `Details Edge Feather` now allow values up to
  `100`.
- The runtime panel now makes `Auto` explicit as a source-size-derived target
  instead of making it look like a fixed `2048` default.

## 2. Files That Define This Behavior

- `src/plugins/ofx/ofx_actions.cpp`
- `src/plugins/ofx/ofx_constants.hpp`
- `src/plugins/ofx/ofx_instance.cpp`
- `src/plugins/ofx/ofx_plugin.cpp`
- `src/plugins/ofx/ofx_render.cpp`
- `tests/unit/test_ofx_color_management.cpp`
- `tests/unit/test_ofx_output_modes.cpp`
- `help/OFX_PANEL_GUIDE.md`
- `help/OFX_RESOLVE_TUTORIALS.md`

## 3. Local Gates For This Workflow

Run these before Windows handoff and after any follow-up edits in this track:

```bash
cmake --preset release
cmake --build --preset release --target test_unit -j8
ctest --test-dir build/release --output-on-failure -R unit_tests
python scripts/check_docs_consistency.py
```

## 4. Windows Validation Only

Use Windows to validate behavior that cannot be proven on macOS:

- Resolve host negotiation on Windows for:
  - RTX / TensorRT
  - non-RTX / DirectML
- `Auto (Host Managed)` correctly reporting:
  - host-managed `srgb_tx`
  - host-managed `lin_rec709_srgb`
  - fallback to manual linear when unsupported
- Output modes looking correct under Resolve color management:
  - `Processed`
  - `Foreground Only`
  - `Source+Matte`
  - `Matte Only`
- First-render startup versus real timeout failures on Windows hosts
- Confirmation that the color contract changes do not introduce regressions in:
  - TensorRT engine bootstrap
  - DirectML startup
  - pre-existing `1536/2048` model-prep failures

## 5. Do Not Expand This Track

Keep this workflow bounded:

- Do not add OCIO in this pass.
- Do not support arbitrary host colourspaces in this pass.
- Do not retune timeout defaults without Windows evidence.
- Do not mix backend/model-resolution policy changes into this track.
