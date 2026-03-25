# CorridorKey OFX Panel Guide

This guide exists to help artists get a usable result fast inside DaVinci
Resolve by explaining what the current CorridorKey OFX build actually does. It
focuses on the controls that change behavior, the status messages that matter,
and the order that avoids dead ends.

## What CorridorKey Is Solving

CorridorKey is not just a matte viewer. Its job is to produce two linked
results from a greenscreen plate:

- a foreground RGB result with spill suppression and recovered detail
- an alpha result that describes transparency

In the current Resolve OFX path, that prediction is driven by a connected
**Alpha Hint** matte. The practical workflow is therefore:

1. give CorridorKey a rough subject guide
2. confirm the model is actually running
3. refine the generated alpha
4. recover texture or add detail only if the keyed result already basically
   works

If you skip step 1, the current OFX build does not produce a real keyed result.

## If You Only Remember Three Things

- Connect **Alpha Hint** first. Until then, the node is waiting, not keying.
- Trust **Effective Quality**, not the dropdown alone.
- Use **Processed** for the normal result, then switch outputs only when you
  are diagnosing a specific problem.

## Start With Runtime Status

The runtime panel at the top is the first place to look after any change.

- **Processing Backend** tells you which execution path is active.
- **Processing Device** tells you which device the plugin is using.
- **Requested Quality** tells you what you asked for. In `Auto`, it shows the
  source-size-derived target, not a fixed mode default.
- **Effective Quality** tells you what resolution is actually running.
- **Loaded Artifact** tells you which packaged model file is active.
- **Status** tells you whether the plugin is waiting, loading, rendering,
  falling back, or using a host-managed versus manual color path.

Treat the status panel as the source of truth. Two messages matter most:

- `Waiting for Alpha Hint connection.` means CorridorKey is not keying yet.
- `Note: ... unavailable on this hardware -- using ...` means the plugin
  loaded a lower packaged model than the requested mode.

Color-management messages are also actionable:

- `Color: Host Managed (...)` means the host negotiated a supported colourspace
  for CorridorKey.
- `Color: Auto fallback to Manual Linear Rec.709 (sRGB)` means `Auto (Host
  Managed)` could not negotiate a supported host colourspace and the plugin
  fell back to its manual linear path.

## Current Resolve OFX Workflow

The current Resolve OFX path does not run inference until an **Alpha Hint**
input is connected.

- The render path still waits for a connected Alpha Hint before running the
  model.
- While waiting, CorridorKey outputs a pass-through image with opaque alpha
  rather than a keyed result.

Practical consequence: do not judge quality, despill, detail recovery, or
tiling until the runtime panel stops saying it is waiting for Alpha Hint.

## Quality

Quality selects the target model resolution. Higher modes load larger artifacts
and typically cost more VRAM, preparation time, and render time.

Current fixed modes:

- **Draft (512)**
- **Standard (768)**
- **High (1024)**
- **Ultra (1536)**
- **Maximum (2048)**

Current `Auto` behavior uses the larger input dimension:

- up to `1000` -> **Standard (768)**
- `1001` to `2000` -> **High (1024)**
- `2001` to `3000` -> **Ultra (1536)**
- above `3000` -> **Maximum (2048)**

Current backend-specific rules:

- **DirectML** is capped at `1536`, so `Maximum (2048)` will not stay at
  `2048px` on that path.
- **CPU** is clamped to **Draft (512)**.
- Fixed modes can still fall back to a lower packaged artifact if session
  preparation or engine creation fails for the requested one.

Use quality as a stability ladder:

1. Start with **High (1024)**.
2. Confirm **Effective Quality** matches what you asked for.
3. Only move up if the current mode is stable and clearly too soft.

For basic users:

- Stay on the highest mode that loads cleanly.
- Do not treat `Ultra` or `Maximum` as automatically better if the runtime
  panel is already falling back.

For advanced users:

- `Auto` is deterministic from input size, not an adaptive quality estimator.
- Fixed modes still keep a lower-resolution fallback chain when the requested
  artifact exists but session preparation fails.

On TensorRT paths, first use can be much slower than steady-state rendering
because the runtime may need to prepare and cache an optimized engine for that
GPU.

## Input Color Space

This control tells CorridorKey how to interpret the RGB image arriving at the
OFX node before inference.

Current options:

- **Auto (Host Managed)** asks the host for one of two supported colourspaces:
  `srgb_tx` or `lin_rec709_srgb`.
- **sRGB** is the manual override for display-referred sRGB-style input.
- **Linear** is the manual override for **Linear Rec.709 (sRGB)**. It is not a
  promise to handle arbitrary project-linear spaces.

Recommended use:

- In Resolve color-managed workflows, leave this on **Auto (Host Managed)**.
- Use **sRGB** only when the image reaching CorridorKey is already
  display-referred.
- Use **Linear** only when you know the image reaching CorridorKey is Linear
  Rec.709 (sRGB).

Current fallback rule:

- If **Auto (Host Managed)** cannot negotiate `srgb_tx` or `lin_rec709_srgb`,
  CorridorKey falls back to the manual **Linear** path and reports that in the
  runtime panel.

Output rule:

- Color outputs are treated as linear under host-managed operation.
- **Matte Only** is treated as raw data, not a colour-managed image.

## Alpha Hint

**Alpha Hint** is the guide-matte input that CorridorKey currently expects in
the Resolve OFX path.

Why this matters:

- the guide matte tells CorridorKey where the subject broadly is
- CorridorKey then generates the output alpha and foreground from that guided
  region
- the panel's matte controls refine CorridorKey's output, not your incoming
  guide

That makes Alpha Hint the setup step, not an optional polish step, in the
current OFX workflow.

### What the plugin reads

The input is interpreted by component layout:

- **RGBA** -> uses the alpha channel
- **Alpha** -> uses the single channel directly
- **RGB** -> uses the red channel

Recommendation:

- Prefer a true alpha or single-channel matte whenever possible.
- If you feed an RGB image, make sure the guide matte is actually stored in the
  red channel. The plugin does not compute luminance from RGB inputs.

### What makes a useful guide matte

A good Alpha Hint does not need final hair detail. It should give CorridorKey
the broad subject shape and the difficult regions that need guidance.

Good sources include:

- a Qualifier result
- a 3D Keyer result
- a garbage matte or hand-shaped matte
- a matte built in Fusion

For a basic user, the goal is simple: give CorridorKey a rough but believable
subject silhouette. It does not need perfect strands of hair. It does need the
subject separated from the screen well enough for the model to know what region
to solve.

### How to connect it in Resolve

**Color Page**

1. Add CorridorKey to the clip.
2. Right-click the node and choose **Add OFX Input**.
3. Route the rough matte into the new green input.

**Fusion**

1. Create or import the guide matte.
2. Connect it to CorridorKey's secondary **Alpha Hint** input.

### What Alpha Hint does not do

The controls in the **Matte** group do not modify the incoming guide matte.
They refine CorridorKey's generated output alpha after inference.

## Matte Controls

These controls operate on CorridorKey's generated alpha, not on the incoming
Alpha Hint.

Recommended order:

1. lock black and white points
2. adjust edge size
3. soften only if needed
4. use gamma for semi-transparent balancing
5. despeckle last

- **Matte Clip Black** remaps alpha so values at or below the chosen black
  point become fully transparent.
- **Matte Clip White** remaps alpha so values at or above the chosen white
  point become fully opaque.
- **Matte Shrink/Grow** changes the matte footprint. Negative values erode
  inward; positive values dilate outward. The effective pixel radius scales
  with the source long edge using a `1920px` baseline.
- **Matte Edge Blur** softens the alpha edge. The effective pixel radius also
  scales with the source long edge using a `1920px` baseline.
- **Matte Gamma** changes matte midtones non-linearly. Values above `1.0`
  brighten semi-transparent regions; values below `1.0` darken and tighten
  them.
- **Auto Despeckle** removes small isolated matte regions automatically.
- **Min Region Size** sets the minimum connected matte area to keep when
  despeckle is enabled.
- **Temporal Smoothing** blends the current result with the previous frame to
  reduce flicker.

## Recover Original Details

**Recover Original Details** lives in the **Interior Detail** group and blends
original source RGB back into opaque interior regions after CorridorKey
generates the matte and foreground.

Think of this as an interior-texture control, not a matte-generation control.

What it is good at:

- restoring fabric texture
- restoring skin detail
- reducing the over-smoothed look in solid foreground interiors

What it is not for:

- rescuing hair
- rescuing semi-transparent edges
- fixing contamination at the matte boundary

Current behavior:

- it starts from strongly opaque regions
- it shrinks that recovery mask before blending
- it feathers the recovery mask before the final blend
- recovered pixels still pass through despill after the blend

That is why it helps interior texture but should not be treated as an edge-fix
tool.

### Details Edge Shrink

This narrows the recovery mask before source detail is blended back. Higher
values keep recovered source texture farther away from unstable edges. The
effective pixel radius scales with the source long edge using a `1920px`
baseline.

### Details Edge Feather

This softens the recovery mask so the transition between model foreground and
source detail is less abrupt. The effective pixel radius scales with the source
long edge using a `1920px` baseline.

These sliders are only active when **Recover Original Details** is enabled.

## Tiling

**Enable Tiling** runs inference on overlapping tiles when the input frame is
larger than the active model resolution.

This is the detail-recovery path for shots that are already working but are
limited by model resolution.

Use it when:

- you already have a stable non-tiled result
- the current model resolution is holding but still looks too soft
- the frame is larger than the chosen model resolution and you want to trade
  time for extra detail

Do not use it for:

- missing Alpha Hint
- wrong backend/package
- model load failures
- hardware fallback problems

### Tile Overlap

Overlap is the shared border used to blend adjacent tiles back together.

- Larger overlap reduces visible seams.
- Larger overlap also increases total work.

Start with the default and only raise it if you can actually see tile-boundary
artifacts.

## Output Modes

Choose output mode based on what you need to inspect or deliver.

- **Processed** is the default result. It is a linear premultiplied RGBA image:
  RGB is already multiplied by alpha, which is the standard form expected by
  many compositing merge operations.
- **Matte Only** shows the generated alpha as grayscale.
- **Foreground Only** shows the despilled foreground with full alpha.
- **Source+Matte** shows the original source RGB premultiplied by CorridorKey's
  generated matte.
- **FG+Matte** is an explicit alias of **Processed** for workflows that want a
  foreground-plus-alpha label.

For advanced users:

- `Processed` and `FG+Matte` share the same linear-premultiplied semantics in
  the current code path.
- `Source+Matte` is useful for comparing the generated alpha against the
  original source plate, not for inspecting despill quality.

If the status panel still says `Waiting for Alpha Hint connection.`, these
outputs are not showing a keyed result yet.

## Start Here

Use this order when you want the fastest path to a trustworthy result.

1. Add CorridorKey to the clip.
2. Connect an **Alpha Hint** matte.
3. Confirm the status panel no longer says `Waiting for Alpha Hint connection.`
4. Set **Quality** to **High (1024)**.
5. Use **Processed** as the default output.
6. Check **Effective Quality** before pushing to `Ultra` or `Maximum`.
7. Adjust **Matte** controls only after the plugin is producing an actual key.
8. Use **Recover Original Details** for interior texture, then **Tiling** only
   if the stable result is still too soft.

## When A Higher Quality Fails

If `Ultra (1536)` or `Maximum (2048)` does not hold:

1. Read **Effective Quality** and **Status**.
2. Stay on the highest stable quality the runtime panel confirms.
3. If detail is still insufficient, try **Tiling** after the non-tiled result is
   already stable.
4. If the backend or artifact should have worked, collect the runtime log and
   use the troubleshooting guide.
