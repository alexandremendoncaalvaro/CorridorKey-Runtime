"""Build distribution manifest from current Hugging Face state.

Queries `alexandrealvaro/CorridorKey` for every artifact the installer
distributes and produces `scripts/installer/distribution_manifest.json`,
which is consumed by:

  - `scripts/package_ofx_installer_windows.ps1` (downloads referenced
    here when building the offline flavor; emits the URL+SHA256 pairs
    consumed by the Inno Setup `[Code]` block when building the online
    flavor).
  - `scripts/fetch_models.ps1` (uses the same source-of-truth so no
    drift between fetch tooling and installer payload).

The manifest is regenerated whenever the Hugging Face state changes
(new model upload, new revision, etc.). It MUST be committed: every
release ships against an exact, reviewable manifest snapshot, not a
runtime-resolved list.

Usage:

    python scripts/installer/build_distribution_manifest.py

Requires the `huggingface_hub` Python package. Auth is not required
for read-only `alexandrealvaro/CorridorKey` (public repo); the cache
at `~/.cache/huggingface/token` is honored when present so a future
private repo migration does not need a script change.
"""

from __future__ import annotations

import json
from pathlib import Path

from huggingface_hub import HfApi, hf_hub_url

REPO = "alexandrealvaro/CorridorKey"
REVISION = "main"

# Pack definitions. Each pack maps to one component in the Inno Setup
# `[Components]` section. The dest_subdir is rooted at the OFX bundle's
# `Contents/Resources/` so the runtime walk in
# `src/core/torch_trt_loader.cpp` resolve_torchtrt_runtime_bin keeps
# working when the blue pack lands in `Resources/torchtrt-runtime/bin/`.
PACKS = {
    "green-models": {
        "label": "Green pack (ONNX models)",
        "component": "green",
        "dest_subdir": "models",
        "files": [
            "onnx/fp16/corridorkey_fp16_512.onnx",
            "onnx/fp16/corridorkey_fp16_1024.onnx",
            "onnx/fp16/corridorkey_fp16_1536.onnx",
            "onnx/fp16/corridorkey_fp16_2048.onnx",
            "onnx/fp16_ctx/corridorkey_fp16_512_ctx.onnx",
            "onnx/fp16_ctx/corridorkey_fp16_1024_ctx.onnx",
            "onnx/fp16_ctx/corridorkey_fp16_1536_ctx.onnx",
            "onnx/fp16_ctx/corridorkey_fp16_2048_ctx.onnx",
        ],
    },
    "blue-models": {
        "label": "Blue pack - dynamic TorchScript model",
        "component": "blue",
        "dest_subdir": "models",
        "files": [
            "torchtrt/dynamic-blue/corridorkey_dynamic_blue_fp16.ts",
        ],
    },
    "blue-runtime": {
        "label": "Blue pack - runtime DLLs (LibTorch + CUDA + TensorRT)",
        "component": "blue",
        "dest_subdir": "torchtrt-runtime/bin",
        "is_archive": True,
        "extract": True,
        "files": [
            # Single 7z bundle containing the curated TorchTRT runtime
            # DLLs. Built from
            # vendor/torchtrt-windows/bin/ excluding our per-build
            # wrapper corridorkey_torchtrt.dll (which ships inside the
            # installer Win64 dir directly because it changes per build).
            "torchtrt/runtime/corridorkey_blue_torchtrt_runtime.7z",
        ],
        "installed_size_bytes": 5011471040,
        "installed_file_count": 41,
    },
}


def main() -> int:
    api = HfApi()
    manifest = {
        "manifest_version": 1,
        "repo": REPO,
        "revision": REVISION,
        "generated_by": "scripts/installer/build_distribution_manifest.py",
        "packs": {},
    }

    for pack_name, pack_meta in PACKS.items():
        paths = pack_meta["files"]
        print(f"[{pack_name}] querying {len(paths)} files ...", flush=True)
        info_list = api.get_paths_info(REPO, paths, revision=REVISION)
        found = {info.path: info for info in info_list}
        entries = []
        for path in paths:
            info = found.get(path)
            if info is None or info.lfs is None:
                entries.append({
                    "remote_path": path,
                    "filename": path.rsplit("/", 1)[-1],
                    "url": None,
                    "sha256": None,
                    "size_bytes": None,
                    "status": "not_uploaded",
                })
                print(f"  [warn] {path} not present on HF (status=not_uploaded)", flush=True)
                continue
            entries.append({
                "remote_path": path,
                "filename": path.rsplit("/", 1)[-1],
                "url": hf_hub_url(REPO, path, revision=REVISION),
                "sha256": info.lfs.sha256,
                "size_bytes": info.size,
                "status": "ready",
            })
        manifest["packs"][pack_name] = {**pack_meta, "files": entries}

    out_path = Path("scripts/installer/distribution_manifest.json")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(manifest, indent=2, sort_keys=False) + "\n", encoding="utf-8")

    total = sum(len(p["files"]) for p in manifest["packs"].values())
    print(f"[done] wrote {out_path} with {total} entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
