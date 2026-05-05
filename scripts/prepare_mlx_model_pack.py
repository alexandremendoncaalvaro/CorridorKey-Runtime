#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
# Local checkpoint candidates. Accept both the historical bare name and
# the upstream-canonical `CorridorKey_v1.0.pth` that nikopueringer
# publishes. First match wins.
CANONICAL_CHECKPOINT_CANDIDATES = (
    REPO_ROOT / "models" / "CorridorKey.pth",
    REPO_ROOT / "models" / "CorridorKey_v1.0.pth",
)
DEFAULT_BRIDGE_RESOLUTIONS = "512,768,1024,1536,2048"

# Primary artifact host. Hugging Face handles large model files with no
# budget ceiling and cleaner version history than a GitHub Release, so
# the default download path goes there first. The GitHub Release
# fallback below stays wired so machines without huggingface_hub
# installed can still pick up the weights pack.
DEFAULT_HF_MODEL_REPO = "alexandrealvaro/CorridorKey"
DEFAULT_HF_REVISION = "main"
# All MLX artefacts (safetensors weights and per-resolution bridges)
# live under this prefix in the canonical repo.
HF_MLX_PREFIX = "mlx/"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare the macOS MLX model pack from the official release or a PyTorch checkpoint."
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("models"),
        help="Destination directory for prepared MLX artifacts.",
    )
    parser.add_argument(
        "--asset-name",
        default="corridorkey_mlx.safetensors",
        help="MLX weights filename to materialize in the output directory.",
    )
    parser.add_argument(
        "--tag",
        default="v1.0.0",
        help="GitHub release tag to download when no local weights or checkpoint is provided.",
    )
    parser.add_argument(
        "--hf-repo",
        default=DEFAULT_HF_MODEL_REPO,
        help=(
            "Hugging Face model repo that hosts the weights pack. Pass an empty string "
            "to disable the Hugging Face source and fall back to the GitHub Release path."
        ),
    )
    parser.add_argument(
        "--hf-revision",
        default=DEFAULT_HF_REVISION,
        help="Git revision (branch, tag, or commit SHA) to pull from the Hugging Face repo.",
    )
    parser.add_argument(
        "--checkpoint",
        type=Path,
        help=(
            "Optional PyTorch checkpoint (.pth) to convert to MLX safetensors. "
            "When omitted, the script uses models/CorridorKey.pth if present."
        ),
    )
    parser.add_argument(
        "--weights-path",
        type=Path,
        help="Optional existing safetensors file to copy into the output directory.",
    )
    parser.add_argument(
        "--bridge-resolutions",
        default=DEFAULT_BRIDGE_RESOLUTIONS,
        help="Comma-separated bridge export resolutions bundled with the Apple model pack.",
    )
    parser.add_argument(
        "--skip-bridge-export",
        action="store_true",
        help="Skip bridge export when only the safetensors weights are needed.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite existing outputs and force re-download/re-conversion.",
    )
    parser.add_argument(
        "--skip-verify",
        action="store_true",
        help="Skip checksum verification when downloading official weights.",
    )
    return parser.parse_args()


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def copy_file(source: Path, target: Path, force: bool) -> Path:
    if source.resolve() == target.resolve():
        return target
    if target.exists() and not force:
        return target
    ensure_parent(target)
    shutil.copy2(source, target)
    return target


def default_checkpoint_path() -> Path | None:
    for candidate in CANONICAL_CHECKPOINT_CANDIDATES:
        if candidate.exists():
            return candidate
    return None


def download_from_huggingface(
    repo_id: str,
    filename: str,
    local_path: Path,
    revision: str,
    force: bool,
) -> Path | None:
    """Fetch a file from a Hugging Face model repo into ``local_path``.

    Returns the final path on success, ``None`` when the huggingface_hub
    library is missing or the download fails. Failure is logged but not
    raised so the caller can transparently fall back to the GitHub Release
    path without breaking older setups.
    """
    if not repo_id:
        return None
    if local_path.exists() and not force:
        return local_path
    try:
        from huggingface_hub import hf_hub_download
    except ImportError:
        print(
            "[warn] huggingface_hub not installed; skipping Hugging Face source. "
            "Install with `.venv-macos-mlx/bin/python -m pip install huggingface_hub` "
            "or rely on the GitHub Release fallback.",
            flush=True,
        )
        return None
    try:
        downloaded = hf_hub_download(
            repo_id=repo_id,
            filename=filename,
            revision=revision,
            local_dir=str(local_path.parent),
        )
    except Exception as error:  # noqa: BLE001 -- fall-through is intentional
        print(
            f"[warn] hugging face download failed for {repo_id}/{filename}: "
            f"{type(error).__name__}: {error}",
            flush=True,
        )
        return None
    source_path = Path(downloaded)
    if source_path.resolve() == local_path.resolve():
        return local_path
    ensure_parent(local_path)
    shutil.copy2(source_path, local_path)
    return local_path


def export_bridge(weights_path: Path, export_path: Path, export_size: int) -> None:
    import mlx.core as mx
    from corridorkey_mlx.inference.pipeline import compile_model, load_model

    ensure_parent(export_path)

    model = load_model(weights_path, img_size=export_size, compile=False, slim=True, stage_gc=False)
    model = compile_model(model, shapeless=False)

    def bridge_forward(x):
        outputs = model(x)
        return outputs["alpha_final"], outputs["fg_final"]

    example = mx.zeros((1, export_size, export_size, 4), dtype=mx.float32)
    mx.eval(bridge_forward(example))
    mx.export_function(str(export_path), bridge_forward, example)


def parse_bridge_resolutions(value: str) -> list[int]:
    resolutions: list[int] = []
    for item in value.split(","):
        text = item.strip()
        if not text:
            continue
        resolution = int(text)
        if resolution <= 0:
            raise ValueError("bridge resolutions must be positive integers")
        if resolution not in resolutions:
            resolutions.append(resolution)
    return resolutions


def main() -> int:
    args = parse_args()

    os.environ.setdefault("CORRIDORKEY_MLX_WEIGHTS_REPO", "nikopueringer/corridorkey-mlx")

    from corridorkey_mlx.convert import convert_checkpoint
    from corridorkey_mlx.weights import download_weights

    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    weights_target = output_dir / args.asset_name
    checkpoint_path = args.checkpoint or default_checkpoint_path()

    if args.weights_path is not None:
        weights_path = copy_file(args.weights_path, weights_target, args.force)
        source = "existing_weights"
    elif checkpoint_path is not None:
        if weights_target.exists() and not args.force:
            weights_path = weights_target
        else:
            convert_checkpoint(checkpoint_path, weights_target)
            weights_path = weights_target
        source = (
            "canonical_checkpoint"
            if checkpoint_path in CANONICAL_CHECKPOINT_CANDIDATES
            else "converted_checkpoint"
        )
    else:
        # Prefer Hugging Face when the repo is reachable; fall back to the
        # GitHub Release channel only if the HF hop fails (offline machine,
        # missing huggingface_hub package, or user explicitly passed
        # --hf-repo "" to opt out).
        hf_path = download_from_huggingface(
            repo_id=args.hf_repo,
            filename=f"{HF_MLX_PREFIX}{args.asset_name}",
            local_path=weights_target,
            revision=args.hf_revision,
            force=args.force,
        )
        if hf_path is not None:
            weights_path = hf_path
            source = "huggingface"
        else:
            weights_path = download_weights(
                tag=args.tag,
                asset_name=args.asset_name,
                out=output_dir,
                force=args.force,
                verify=not args.skip_verify,
            )
            source = "github_release"

    result = {
        "weights_path": str(weights_path.resolve()),
        "source": source,
    }
    if checkpoint_path is not None:
        result["checkpoint_path"] = str(checkpoint_path.resolve())

    if not args.skip_bridge_export:
        bridge_exports = []
        for resolution in parse_bridge_resolutions(args.bridge_resolutions):
            export_path = output_dir / f"{weights_path.stem}_bridge_{resolution}.mlxfn"
            bridge_source = "local_cache"
            if not export_path.exists() or args.force:
                # Prefer the pre-built bridge from Hugging Face before re-running
                # the MLX exporter. MLX compile for 1536/2048 takes minutes and
                # is deterministic from the same weights, so downloading the
                # published artifact saves significant bring-up time on every
                # new checkout.
                hf_path = download_from_huggingface(
                    repo_id=args.hf_repo,
                    filename=f"{HF_MLX_PREFIX}{export_path.name}",
                    local_path=export_path,
                    revision=args.hf_revision,
                    force=args.force,
                )
                if hf_path is not None:
                    bridge_source = "huggingface"
                else:
                    export_bridge(weights_path, export_path, resolution)
                    bridge_source = "exported_locally"
            if not export_path.exists():
                raise FileNotFoundError(
                    f"Expected MLX bridge export was not created: {export_path}"
                )
            bridge_exports.append(
                {
                    "path": str(export_path.resolve()),
                    "resolution": resolution,
                    "source": bridge_source,
                }
            )
        result["bridge_exports"] = bridge_exports

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
