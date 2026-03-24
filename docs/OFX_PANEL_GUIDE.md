# CorridorKey OFX Panel Guide

This guide exists to help artists get to a stable key quickly inside DaVinci
Resolve without guessing what the controls mean. It focuses on the CorridorKey
panel itself: what to touch first, what to leave alone, and what each critical
control changes in practice.

## Start With Runtime Status

The runtime panel at the top is the first thing to read after changing
quality.

- **Processing Backend** tells you which execution path is active.
- **Requested Quality** tells you what you asked for.
- **Effective Quality** tells you what the plugin is actually running.
- **Loaded Artifact** tells you which model file is active.
- **Status** tells you if the requested quality loaded, fell back, or failed.

If you request `Ultra (1536)` and the status says it is using `1024px`, the
machine did not keep the higher resolution. Trust the status panel, not the
dropdown alone.

## Quality

Use quality as a stability ladder, not as a vanity setting.

- **Auto** chooses a model based on input size and hardware. Use it when you
  want the plugin to pick the first reasonable target for the current shot.
- Start with **High (1024)**.
- Move to **Ultra (1536)** only if the status panel stays on `1536px`.
- Move to **Maximum (2048)** only if the status panel stays on `2048px`.
- If a higher mode falls back, stay on the highest mode that loads cleanly on
  that machine.

Higher quality loads larger models and increases first-load cost. On TensorRT
paths, first-run compilation can be much slower than steady-state rendering.

## Alpha Hint

**Alpha Hint** is an optional secondary matte input. Use it when you already
have a rough matte from Resolve that describes difficult regions better than
the model alone.

Good use cases:

- fine hair
- motion blur
- bad spill contamination
- semi-transparent edges
- shots with foreground elements close to the screen color

The hint input is optional. Feed CorridorKey either:

- an alpha channel
- a black-and-white luma matte

Practical sources inside Resolve:

- a Qualifier result
- a 3D Keyer result
- a garbage matte or hand-shaped matte
- a matte built in Fusion

The **Matte** controls in the panel still adjust CorridorKey's output matte.
They do not modify the incoming hint clip.

## Recover Original Details

**Recover Original Details** blends original source detail back into opaque
foreground regions where CorridorKey is already confident about the matte. Use
it to keep real texture in clothing, skin, and other solid areas that can look
too processed after inference.

Use it when:

- the subject interior looks too smoothed
- fabric texture disappears
- clean opaque detail feels too synthetic

Back it off when:

- edges become too crunchy
- haloing appears around transitions
- the recovered source brings back contamination you do not want

### Details Edge Shrink

This shrinks the recovered-details mask before source detail is blended back.
Increasing it narrows where source texture is allowed back in near edges.

### Details Edge Feather

This softens the recovered-details mask so the blend between the model result
and the original source is less abrupt.

## Tiling

**Enable Tiling** is for detail recovery when model resolution is the
bottleneck.

Use it when:

- the subject has fine detail that lower model resolutions lose
- the image is high resolution and model upscaling looks too soft
- you want to trade render time for extra detail

Do not use it by default. Tiling is slower, increases memory use, and is a
detail tool, not a fix for backend or model-load failures.

If you enable tiling:

- keep **Tile Overlap** high enough to avoid seams
- expect slower first renders
- compare against normal `High (1024)` first so you know the extra cost is
  worth it

## Output Modes

Choose output mode based on the job you are doing.

- **Processed** is CorridorKey's linear premultiplied RGBA output. This is the
  default result for viewing and compositing.
- **Matte Only** is for inspecting the alpha.
- **Foreground Only** is for inspecting the despilled foreground.
- **Source+Matte** is the original source premultiplied by CorridorKey's matte.
- **FG+Matte** is an explicit alias of **Processed** for workflows that want a
  foreground-plus-alpha label.

## Start Here

1. Set **Quality** to `High (1024)`.
2. Confirm the top status panel says `1024px`, not a fallback.
3. Keep **Recover Original Details** enabled unless it makes edges worse.
4. Add **Alpha Hint** only if you already have a rough matte that improves
   difficult regions.
5. Enable **Tiling** only after confirming normal `High (1024)` is not enough.
6. Use **Processed** as the normal output. Switch outputs only when diagnosing
   a specific problem.

## When A Higher Quality Fails

If `Ultra (1536)` or `Maximum (2048)` fails or falls back:

1. Read the top status panel.
2. Keep the highest stable quality the panel confirms.
3. If needed, use **Tiling** to regain detail without forcing a model that the
   machine cannot keep active.
4. If the machine should support the higher mode, collect the runtime log and
   use the troubleshooting guide.
