---
description: [How to build and package a macOS release]
---

# macOS Release Packaging Workflow

All agents MUST follow this protocol when asked to generate a release for the user.

> **Note to AI Agents (Safety Seal):** Steps 1, 2, and 3 (Running the build or packaging scripts) are strictly local, deterministic, and 100% safe. They DO NOT execute `git push`, `gh release`, or any external network mutations. You must not fear running them when asked to generate a test build, even if the user says "do not publish". Only Step 4 performs external actions.

## 1. Prepare Version and Code
Make sure any necessary C++ modifications are stable.
Check `include/corridorkey/version.hpp` and `CMakeLists.txt` to ensure they reflect the correct semantic version.

## 2. Compile the Project
Run the self-contained build script:
```bash
bash scripts/build.sh release-macos-portable
```

## 3. Generate Packages
Once the compilation is successful, run the full release pipeline:
```bash
bash scripts/release_pipeline_macos.sh
```

This will build, test, package both CLI and OFX bundles, validate backends, and produce final artifacts in `dist/`.

Alternatively, run individual packaging scripts:
```bash
bash scripts/package_mac.sh          # CLI standalone bundle
bash scripts/package_ofx_mac.sh      # OFX plugin installer
```

## 4. GitHub Release
Once the user confirms the `.dmg` and `.pkg` artifacts are correctly generated in the `dist/` folder, you may proceed to use the `gh release create` command to publish it (referencing `docs/RELEASE_GUIDELINES.md` for the exact naming conventions).
