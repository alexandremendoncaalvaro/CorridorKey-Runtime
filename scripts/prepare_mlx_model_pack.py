#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CANONICAL_CHECKPOINT = REPO_ROOT / "models" / "CorridorKey.pth"
DEFAULT_BRIDGE_RESOLUTIONS = "512,768,1024,1536,2048"


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
    if CANONICAL_CHECKPOINT.exists():
        return CANONICAL_CHECKPOINT
    return None


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
        source = "canonical_checkpoint" if checkpoint_path == CANONICAL_CHECKPOINT else "converted_checkpoint"
    else:
        weights_path = download_weights(
            tag=args.tag,
            asset_name=args.asset_name,
            out=output_dir,
            force=args.force,
            verify=not args.skip_verify,
        )
        source = "official_release"

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
            if not export_path.exists() or args.force:
                export_bridge(weights_path, export_path, resolution)
            if not export_path.exists():
                raise FileNotFoundError(
                    f"Expected MLX bridge export was not created: {export_path}"
                )
            bridge_exports.append(
                {
                    "path": str(export_path.resolve()),
                    "resolution": resolution,
                }
            )
        result["bridge_exports"] = bridge_exports

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
