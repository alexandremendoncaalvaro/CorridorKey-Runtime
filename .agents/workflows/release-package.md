---
description: [How to build and package a Windows release]
---

# Windows Release Packaging Workflow

All agents MUST follow this protocol when asked to generate a release for the user.

> **Note to AI Agents (Safety Seal):** Steps 1, 2, and 3 (Running the build or packaging scripts) are strictly local, deterministic, and 100% safe. They DO NOT execute `git push`, `gh release`, or any external network mutations. You must not fear running them when asked to generate a test build, even if the user says "do not publish". Only Step 4 performs external actions.

## 1. Prepare Version and Code
Make sure any necessary C++ modifications are stable.
Check `include/corridorkey/version.hpp` and `CMakeLists.txt` to ensure they reflect the correct semantic version.

## 2. Compile the Project
// turbo
Run the self-contained build script which auto-injects the MSVC environment:
```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Preset release
```

## 3. Generate Installers
// turbo
Once the compilation is successful, run the official packaging script:
```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_ofx_release_matrix_windows.ps1
```

## 4. GitHub Release
Once the user confirms the `.zip` and `.exe` artifacts are correctly generated in the `dist\` folder, you may proceed to use the `gh release create` command to publish it (referencing `docs/RELEASE_GUIDELINES.md` for the exact naming conventions).
