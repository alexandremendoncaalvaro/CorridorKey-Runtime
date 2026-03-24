# CorridorKey Resolve Tutorials

This guide exists to show reliable CorridorKey workflows in DaVinci Resolve.
It focuses on decisions that matter in real sessions: how to get a clean first
result fast, when to add an Alpha Hint, and when to use Recover Original
Details or Tiling.

## Tutorial 1: Fast First Key

Use this when you want the quickest path to a stable result.

1. Add CorridorKey to the clip.
2. Set **Quality** to `High (1024)`.
3. Read the runtime panel at the top.
4. Confirm **Effective Quality** says `1024px`.
5. Leave **Recover Original Details** enabled.
6. Check **Processed** as the default output.
7. Only try `Ultra (1536)` if `High (1024)` is stable and not detailed enough.

If the status panel says the requested quality is unavailable and it fell back,
do not keep pushing that machine. Stay on the highest stable setting.

## Tutorial 2: Difficult Hair Or Motion Blur With Alpha Hint

Use this when hair, fur, motion blur, or weak edges need guidance.

### Color Page

1. Create a rough matte with a Qualifier, 3D Keyer, or another matte tool.
2. Right-click the CorridorKey node and choose **Add OFX Input**.
3. Route the rough matte into the new green OFX input.
4. In CorridorKey, keep **Alpha Hint** connected and adjust the normal **Matte**
   controls only after the hint is in place.

### Fusion

1. Create or import a guide matte.
2. Connect it to the CorridorKey **Alpha Hint** input.
3. Re-check the result in **Processed** and **Matte Only**.

Use the hint to steer ambiguous regions, not to replace CorridorKey's matte
logic entirely.

## Tutorial 3: Recover Opaque Texture

Use this when the subject interior looks too plastic or too smoothed.

1. Keep **Recover Original Details** enabled.
2. Inspect the face, clothing, and textured opaque areas.
3. If the interior is still too soft, keep it enabled and adjust:
   - **Details Edge Shrink** to keep the recovered texture out of unstable edge zones
   - **Details Edge Feather** to smooth the transition back into the model result
4. If the edge starts to look brittle or contaminated, reduce those controls or
   disable **Recover Original Details**.

This feature is meant to restore trusted foreground texture, not to rescue bad
edges.

## Tutorial 4: Add Detail With Tiling

Use this when the model resolution itself is the limit.

1. Start from a stable non-tiled result.
2. Enable **Tiling**.
3. Keep **Tile Overlap** high enough to avoid seams.
4. Compare the result against the non-tiled version at 100% view.
5. Keep it only if the added detail is worth the extra time and memory.

Tiling is the right tool when `High (1024)` is stable but too soft. It is not
the right tool for every shot.

## Tutorial 5: Diagnose Before Escalating Quality

Before blaming model quality, isolate the actual problem.

1. Use **Matte Only** to inspect the alpha.
2. Use **Foreground Only** to inspect despill and foreground texture.
3. Use **Source+Matte** if you want to compare CorridorKey's matte against the
   original source.
4. Read the runtime panel after every quality change.

If the top status panel says the machine is not keeping the requested quality,
the next move is not more tweaking. The next move is either:

- stay on the highest stable quality
- enable **Tiling**
- collect logs and troubleshoot the backend

## Next Steps

If the result is unstable because the requested quality will not stay loaded,
or the backend is not what you expected, use:

- [Troubleshooting](TROUBLESHOOTING.md)
- [Support Matrix](SUPPORT_MATRIX.md)
