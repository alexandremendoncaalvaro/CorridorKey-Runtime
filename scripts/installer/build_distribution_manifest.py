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
# working when the dynamic pack lands in `Resources/torchtrt-runtime/bin/`.
PACKS = {
    "green-models": {
        "label": "Green pack - dynamic TorchScript model",
        "component": "green",
        "dest_subdir": "models",
        "files": [
            "torchtrt/dynamic-green/corridorkey_dynamic_green_fp16.ts",
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
    "torchtrt-runtime": {
        "label": "TorchTRT runtime DLLs (LibTorch + CUDA + TensorRT)",
        "component": "runtime",
        "dest_subdir": "torchtrt-runtime/bin",
        "is_archive": True,
        "extract": True,
        "files": ["torchtrt/runtime/corridorkey_torchtrt_runtime.7z"],
        "installed_size_bytes": 5011471040,
        "installed_file_count": 41,
    },
}


def pack_file_remote_path(file_spec: str | dict[str, str]) -> str:
    if isinstance(file_spec, dict):
        return file_spec["remote_path"]
    return file_spec


def pack_file_filename(file_spec: str | dict[str, str]) -> str:
    if isinstance(file_spec, dict):
        return file_spec.get("filename") or pack_file_remote_path(file_spec).rsplit("/", 1)[-1]
    return file_spec.rsplit("/", 1)[-1]


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
        paths = [pack_file_remote_path(file_spec) for file_spec in pack_meta["files"]]
        print(f"[{pack_name}] querying {len(paths)} files ...", flush=True)
        info_list = api.get_paths_info(REPO, paths, revision=REVISION)
        found = {info.path: info for info in info_list}
        entries = []
        for file_spec in pack_meta["files"]:
            path = pack_file_remote_path(file_spec)
            filename = pack_file_filename(file_spec)
            info = found.get(path)
            if info is None or info.lfs is None:
                entries.append({
                    "remote_path": path,
                    "filename": filename,
                    "url": None,
                    "sha256": None,
                    "size_bytes": None,
                    "status": "not_uploaded",
                })
                print(f"  [warn] {path} not present on HF (status=not_uploaded)", flush=True)
                continue
            entries.append({
                "remote_path": path,
                "filename": filename,
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
