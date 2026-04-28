# CorridorKey Nuke Tutorials

This guide exists to show practical CorridorKey workflows in Foundry Nuke
using the behavior the current OFX build actually exposes. It focuses on how
to get from a blank node to a usable key, how to feed the Alpha Hint input
correctly, and how to decide between quality, matte refinement, Recover
Original Details, and Tiling.

## Before You Start

Three concepts make the rest of the guide easier:

- CorridorKey's current Nuke OFX path expects a connected **Alpha Hint**
  matte when you want the best result, but it can fall back to a rough
  automatic guide when none is readable.
- **Quality** selects the model resolution target. The runtime panel tells you
  whether that target actually held.
- **Processed** is the normal compositing result. The other outputs are mainly
  for inspection and diagnosis.

If you are completely new to the plugin, do **Tutorial 1** first and do not
skip the runtime panel checks.

## Installing the Plugin

Nuke scans the standard OpenFX plugin locations on Windows. The CorridorKey
installer drops the bundle into `C:\Program Files\Common Files\OFX\Plugins\`,
which is one of the directories Nuke reads by default. After installing:

1. Close Nuke if it is open.
2. Delete Nuke's OFX metadata cache so the new plugin is rescanned. On
   Windows the cache lives at
   `%LOCALAPPDATA%\Temp\nuke\ofxplugincache\ofxplugincache_Nuke<version>-64.xml`.
3. Reopen Nuke.

The installer does this cache cleanup automatically when it detects Nuke,
but the manual fallback above always works.

CorridorKey appears in the node menu under **OFX > Keying > CorridorKey**.

## Color-Managed Reminder

Nuke's project-level color management (OCIO or Nuke's native color
management) controls what comes into the OFX node. CorridorKey's
**Input Color Space** parameter tells the plugin how to interpret what it
receives.

- Leave **Input Color Space** on **Auto (Host Managed)** when your project
  feeds the node in `srgb_tx` or `lin_rec709_srgb`. The plugin queries
  Nuke's clip colourspace and uses the host-declared format.
- If the project uses a different working space (a custom OCIO config, an
  ACES space, etc.), set **Input Color Space** explicitly to **sRGB** or
  **Linear** based on what the upstream node delivers, then convert before
  and after the CorridorKey node as needed.

Important limitation:

- **Linear** in CorridorKey means **Linear Rec.709 (sRGB)**. It does not mean
  an arbitrary project-linear space.

If `Auto (Host Managed)` cannot negotiate one of the supported colourspaces,
the plugin falls back to the manual **Linear** path and reports that in the
runtime panel.

## Tutorial 1: First Usable Result

Use this when you want the fastest path to a working key.

1. Drop a `Read` node pointing at your green-screen plate.
2. Add CorridorKey from **OFX > Keying > CorridorKey** and connect the Read
   to its **Source** input.
3. Build a rough matte upstream. A `Keyer`, `Primatte`, `IBKColour`, or any
   pre-existing matte you already have works.
4. Connect that matte to CorridorKey's **Alpha Hint** input.
5. Check the runtime panel inside the CorridorKey properties. Prefer
   `Guide Source: External Alpha Hint` over `Guide Source: Rough Fallback`.
6. Set **Quality** to **High (1024)**.
7. Leave **Input Color Space** on **Auto (Host Managed)** unless you are
   deliberately overriding it.
8. Leave **Output Mode** on **Processed**.
9. Confirm **Effective Quality** matches the mode you asked for.
10. Connect a `Viewer` to CorridorKey's output. Nuke honors the alpha
    natively so the keyed background reads as transparent without extra
    routing.

If the runtime panel reports a fallback, stay on the highest stable quality
instead of forcing a higher one that the machine is not keeping active.

Why this works for beginners:

- the rough matte gives CorridorKey a strong guide instead of relying on the
  degraded fallback path
- `High (1024)` is a practical middle ground between load cost and detail
- `Processed` lets you judge the actual keyed result without overcomplicating
  the first pass

Windows RTX note:

- The current public OFX ladder is **Draft (512)**, **High (1024)**,
  **Ultra (1536)**, and **Maximum (2048)**.
- In `Auto`, the Windows RTX track respects the current safe quality ceiling
  for the detected VRAM tier.
- In a fixed mode, the Windows RTX track can still attempt `Ultra (1536)` or
  `Maximum (2048)` above the safe ceiling, so always confirm **Effective
  Quality** after the switch.

## Tutorial 2: Feed Alpha Hint in a Format the Plugin Reads Correctly

Use this when you need to prepare or troubleshoot the guide matte itself.

Current input interpretation:

- **RGBA** input -> CorridorKey reads the alpha channel
- **Alpha** input -> CorridorKey reads the single channel directly
- **RGB** input -> CorridorKey reads the red channel

Practical guidance for Nuke:

1. Prefer a real alpha or single-channel matte whenever possible. A `Shuffle`
   node before the Alpha Hint input can copy a matte into the alpha channel.
2. If you are sending RGB, make sure the matte actually lives in the red
   channel. Use `Shuffle` to remap channels explicitly.
3. Recheck the result in **Matte Only** after connecting the guide input.

Use an RGB guide only when you know exactly how it was authored. For most
Nuke workflows, a true alpha or single-channel matte is safer and easier to
reason about.

Advanced note:

- If you see a matte-looking RGB image but it is stored in green or blue, the
  current OFX build will still read red and the result will be wrong. Use
  `Shuffle` to move it to red.

## Tutorial 3: Build the Alpha Hint with Nuke's Native Tools

Use this when an upstream rough matte is not precise enough for the shot.

1. From the green-screen plate, branch a copy into a `Keyer`, `Primatte`,
   `IBKColour`, or any keyer chain that produces a usable matte.
2. Optionally refine with `Roto`, paint, or matte-shape nodes.
3. Use a `Shuffle` if needed to put the matte in the alpha channel.
4. Connect that matte to CorridorKey's secondary **Alpha Hint** input.
5. Return CorridorKey's **Output Mode** to **Processed** to evaluate the
   comp result.
6. Use **Matte Only** to evaluate the generated alpha.

This is a good path for shots that need hand-shaped guidance, paint work, or
a matte built from multiple operations.

This is also the best path when you want to iterate on the guide matte itself
without depending on simple keyers.

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

For advanced users, this order maps cleanly to the underlying operations:

- levels
- morphological size change
- blur
- gamma curve
- connected-component cleanup
- temporal blend

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

The recovered pixels still pass through despill after the blend, and the
Details Edge controls scale with source size using a `1920px` long-edge
baseline.

If you are unsure whether it is helping, toggle it on and off while looking at
solid areas only. Do not judge it by hair or edge haze.

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

If tiled and non-tiled results look the same at your working zoom level, keep
tiling off and take the speed win.

## Tutorial 7: Diagnose Output Before Tweaking More

Use this when you need to isolate the problem before adding more adjustments.

1. Use **Processed** for the normal compositing result.
2. Use **Matte Only** to inspect the generated alpha directly.
3. Use **Foreground Only** to inspect despill and interior texture without the
   matte presentation.
4. Use **Source+Matte** to compare the original source RGB against
   CorridorKey's generated alpha.
5. Read the runtime panel after every quality or backend-related change.

If the panel still says `Guide Source: Rough Fallback`, stop there first if you
intend to use a proper external guide. That status means you are evaluating the
degraded automatic fallback path, not the preferred guided workflow.

That single check saves the most time because it prevents tuning the wrong
stage of the pipeline.

## Next Steps

If the result is unstable because the requested quality will not stay loaded,
or the backend is not what you expected, use:

- [Troubleshooting](TROUBLESHOOTING.md)
- [Support Matrix](SUPPORT_MATRIX.md)
