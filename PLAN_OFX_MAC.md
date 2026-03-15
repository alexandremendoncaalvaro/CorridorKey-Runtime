# CorridorKey OFX Plugin for macOS Apple Silicon

## Business Context

The goal is a frictionless DaVinci Resolve installation path on macOS Apple Silicon that keeps the runtime portable, predictable, and easy to validate. The plugin must ship with the curated Apple model pack and MLX acceleration while preserving CPU/CoreML fallback behavior. The install experience should minimize user choices and eliminate manual steps beyond the standard macOS installer flow.

## Summary

- Build the OpenFX plugin on macOS and package a self-contained `.ofx.bundle`.
- Ship a notarized DMG containing a single `.pkg` installer.
- Default to system-wide installation, with optional advanced choices for per-user install and cache clearing.

## Implementation Decisions

- The OFX target builds on macOS with a module binary named `CorridorKey.ofx`.
- The bundle layout is:
  - `Contents/MacOS`: plugin binary and runtime dylibs.
  - `Contents/Resources/models`: curated model pack and MLX bridge artifacts.
  - `Contents/Info.plist`: minimal bundle metadata with `CFBundlePackageType=BNDL`.
- MLX is the primary backend; CPU/CoreML remain available as fallbacks.
- Runtime dylibs are relocated and re-linked to `@loader_path` to avoid absolute paths and host conflicts.

## Installer Experience

- DMG contains a single notarized `.pkg`.
- Defaults:
  - Install location: `/Library/OFX/Plugins`.
  - Clear Resolve OFX cache after install.
- Advanced options in “Customize” allow:
  - Per-user install at `~/Library/OFX/Plugins`.
  - Disabling cache clearing.

## Validation

- Bundle validation checks for layout, signed binaries, dependencies, and rpath hygiene.
- Manual verification in DaVinci Resolve confirms plugin visibility and processing on Apple Silicon.
