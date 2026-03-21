# CorridorKey Resolve OFX v0.4.5 (Windows)

## UX Improvements

- **Linear default**: Input Color Space now defaults to Linear, preventing double-gamma when used in ACES/DWG pipelines.
- **Safer defaults**: Quality defaults to Preview (512) and Quantization to FP16, preventing VRAM crashes on first use.
- **Removed Auto from dropdowns**: Quantization and Input Color Space no longer have an ambiguous "Auto" option. Users explicitly choose FP16/INT8 and sRGB/Linear.
- **Removed Refiner Scale**: The experimental slider has been removed and hardcoded to 1.0 to prevent image corruption.
- **External Alpha Hint only**: Internal alpha hint generators have been removed. The plugin now requires an explicit Alpha Hint input connection (e.g., from DaVinci Magic Mask or a pre-keyed matte).

## Bug Fixes

- **No more flickering without Alpha Hint**: When no Alpha Hint clip is connected, the plugin now outputs a transparent black frame instead of returning an error that caused DaVinci to flicker.
- **Version string fix**: The plugin now correctly reports v0.4.5 in DaVinci's parameter list (previously v0.4.4 was misreporting as v0.4.2).
