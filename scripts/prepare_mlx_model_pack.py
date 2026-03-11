#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
from pathlib import Path


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
        help="Optional PyTorch checkpoint (.pth) to convert to MLX safetensors.",
    )
    parser.add_argument(
        "--weights-path",
        type=Path,
        help="Optional existing safetensors file to copy into the output directory.",
    )
    parser.add_argument(
        "--export-mlxfn",
        type=Path,
        help="Optional path for a bridge .mlxfn export using the prepared safetensors weights.",
    )
    parser.add_argument(
        "--export-size",
        type=int,
        default=512,
        help="Resolution used when exporting the optional .mlxfn bridge.",
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


def export_bridge(weights_path: Path, export_path: Path, export_size: int) -> None:
    import mlx.core as mx
    from corridorkey_mlx.inference.pipeline import load_model

    ensure_parent(export_path)

    model = load_model(weights_path, img_size=export_size, compile=False, slim=True, stage_gc=False)
    model._compiled = True

    def bridge_forward(x):
        outputs = model(x)
        return outputs["alpha_final"], outputs["fg_final"]

    example = mx.zeros((1, export_size, export_size, 4), dtype=mx.float32)
    mx.export_function(str(export_path), bridge_forward, example)


def main() -> int:
    args = parse_args()

    os.environ.setdefault("CORRIDORKEY_MLX_WEIGHTS_REPO", "nikopueringer/corridorkey-mlx")

    from corridorkey_mlx.convert import convert_checkpoint
    from corridorkey_mlx.weights import download_weights

    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    weights_target = output_dir / args.asset_name

    if args.weights_path is not None:
        weights_path = copy_file(args.weights_path, weights_target, args.force)
        source = "existing_weights"
    elif args.checkpoint is not None:
        if weights_target.exists() and not args.force:
            weights_path = weights_target
        else:
            convert_checkpoint(args.checkpoint, weights_target)
            weights_path = weights_target
        source = "converted_checkpoint"
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

    if args.export_mlxfn is not None:
        if args.export_mlxfn.exists() and not args.force:
            pass
        else:
            export_bridge(weights_path, args.export_mlxfn, args.export_size)
        result["mlxfn_path"] = str(args.export_mlxfn.resolve())
        result["export_size"] = args.export_size

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
