# CorridorKey Resolve Tutorials

This guide exists to show practical CorridorKey workflows in DaVinci Resolve
using the behavior the current OFX build actually exposes. It focuses on how
to get from a blank node to a usable key, how to feed the Alpha Hint input
correctly, and how to decide between quality, matte refinement, Recover
Original Details, and Tiling.

## Tutorial 1: First Usable Result on the Color Page

Use this when you want the fastest path to a working key inside the Color page.

1. Add CorridorKey to the clip.
2. Build a rough matte with a Qualifier, 3D Keyer, or another matte tool.
3. Right-click the CorridorKey node and choose **Add OFX Input**.
4. Route the rough matte into the new green input.
5. Check the runtime panel. Do not continue until the status stops saying
   `Waiting for Alpha Hint connection.`
6. Set **Quality** to **High (1024)**.
7. Leave **Output Mode** on **Processed**.
8. Confirm **Effective Quality** matches the mode you asked for.

If the runtime panel reports a fallback, stay on the highest stable quality
instead of forcing a higher one that the machine is not keeping active.

## Tutorial 2: Feed Alpha Hint in a Format the Plugin Reads Correctly

Use this when you need to prepare or troubleshoot the guide matte itself.

Current input interpretation:

- **RGBA** input -> CorridorKey reads the alpha channel
- **Alpha** input -> CorridorKey reads the single channel directly
- **RGB** input -> CorridorKey reads the red channel

Practical guidance:

1. Prefer a real alpha or single-channel matte whenever possible.
2. If you are sending RGB, make sure the matte actually lives in the red
   channel.
3. Recheck the result in **Matte Only** after connecting the guide input.

Use an RGB guide only when you know exactly how it was authored. For most
Resolve workflows, a true alpha or single-channel matte is safer and easier to
reason about.

## Tutorial 3: Build the Alpha Hint in Fusion

Use this when the Color page tools are not precise enough for the shot.

1. Open the clip in Fusion.
2. Create or import the guide matte.
3. Connect that matte to CorridorKey's secondary **Alpha Hint** input.
4. Return to **Processed** to evaluate the comp result.
5. Use **Matte Only** to evaluate the generated alpha.

This is a good path for shots that need hand-shaped guidance, paint work, or a
matte built from multiple operations.

## Tutorial 4: Refine the Generated Matte

Use this after the plugin is already running with a connected Alpha Hint.

1. Start with **Matte Only**.
2. Use **Matte Clip Black** and **Matte Clip White** to lock the extremes of
   the matte.
3. Use **Matte Shrink/Grow** to tighten or expand the edge footprint.
4. Use **Matte Edge Blur** only as much as needed to soften hard transitions.
5. Use **Matte Gamma** to rebalance semi-transparent regions.
6. Enable **Auto Despeckle** only if you see small isolated matte islands.
7. Raise **Min Region Size** only enough to remove those islands.
8. Use **Temporal Smoothing** only when motion flicker persists across frames.

Recommended mindset: clip the matte first, then shape the edge, then stabilize
it. Avoid moving several controls at once.

## Tutorial 5: Recover Interior Texture

Use this when the keyed subject interior looks too plastic or too smooth.

1. Enable **Recover Original Details**.
2. Inspect solid areas such as face, clothing, or textured opaque surfaces.
3. Increase **Details Edge Shrink** if recovered detail is creeping too close
   to unstable edges.
4. Increase **Details Edge Feather** if the handoff between recovered source
   detail and model foreground looks abrupt.
5. Disable the feature if it brings back contamination or brittle edge
   structure.

This feature is for opaque interior texture. It is not a substitute for a good
Alpha Hint or a clean matte edge.

## Tutorial 6: Add Detail Without Forcing an Unstable Quality

Use this when **High (1024)** is stable but still too soft.

1. First confirm the requested quality is actually holding in the runtime
   panel.
2. If `Ultra (1536)` or `Maximum (2048)` falls back, stay on the highest stable
   non-tiled mode.
3. Enable **Tiling**.
4. Compare the tiled result against the non-tiled result at 100% view.
5. Raise **Tile Overlap** only if you can actually see tile seams.
6. Keep tiling only if the detail gain is worth the extra time and memory.

Tiling is the right lever when the model resolution is the bottleneck. It is
not the right lever for missing Alpha Hint, backend failures, or wrong package
selection.

## Tutorial 7: Diagnose Output Before Tweaking More

Use this when you need to isolate the problem before adding more adjustments.

1. Use **Processed** for the normal compositing result.
2. Use **Matte Only** to inspect the generated alpha directly.
3. Use **Foreground Only** to inspect despill and interior texture without the
   matte presentation.
4. Use **Source+Matte** to compare the original source RGB against CorridorKey's
   generated alpha.
5. Read the runtime panel after every quality or backend-related change.

If the panel still says `Waiting for Alpha Hint connection.`, stop there first.
That status means you are not evaluating a keyed result yet.

## Next Steps

If the result is unstable because the requested quality will not stay loaded,
or the backend is not what you expected, use:

- [Troubleshooting](TROUBLESHOOTING.md)
- [Support Matrix](SUPPORT_MATRIX.md)
