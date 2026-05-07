"""Compile dynamic CorridorKey checkpoints to true Torch-TensorRT artifacts.

The generated TorchScript file has two runtime inputs:

1. normalized NCHW CorridorKey tensor, shape ``[1, 4, H, W]``
2. positional embedding grid, shape ``[1, 112, H / 4, W / 4]``

The second input keeps the Hiera positional-embedding resize outside the
TensorRT engine. The source positional embedding is embedded into the same
``.ts`` file through TorchScript extra files so the C++ runtime can rebuild and
cache the grid for each runtime resolution without shipping a sidecar model
file.
"""

from __future__ import annotations

import argparse
import gc
import json
import math
import shutil
import sys
import tempfile
from pathlib import Path
from types import MethodType

import torch
import torch.nn.functional as F  # noqa: N812
import torch_tensorrt as torch_trt
from timm.models._features import feature_take_indices
from torch.export import Dim

from compile_dynamic_torchscript import (
    NIKO_UPSTREAM_PIN,
    REPO_ROOT,
    _dynamic_reroll_tokens,
    _dynamic_unroll_tokens,
    _load_dynamic_model,
    _reroll_schedule_for_block,
    clone_upstream_repo,
)


DEFAULT_VALIDATE_RESOLUTIONS = (512, 1024)
EXTERNAL_POS_META_NAME = "corridorkey.external_pos.v1.json"
EXTERNAL_POS_DATA_NAME = "corridorkey.external_pos.v1.fp32"
ValidationCase = tuple[int, torch.Tensor, list[torch.Tensor]]


def _project_feature(linear: torch.nn.Module, feature: torch.Tensor) -> torch.Tensor:
    batch = feature.shape[0]
    channels = linear.proj.out_features
    return linear(feature.flatten(2).transpose(1, 2)).transpose(1, 2).view(
        batch,
        channels,
        feature.shape[2],
        feature.shape[3],
    )


def _patch_decoder_for_tensorrt_dynamic(model_transformer) -> None:
    def decoder_forward(self, features):
        c1, c2, c3, c4 = features
        c4_linear = F.interpolate(
            _project_feature(self.linear_c4, c4),
            scale_factor=(8.0, 8.0),
            mode="bilinear",
            align_corners=False,
        )
        c3_linear = F.interpolate(
            _project_feature(self.linear_c3, c3),
            scale_factor=(4.0, 4.0),
            mode="bilinear",
            align_corners=False,
        )
        c2_linear = F.interpolate(
            _project_feature(self.linear_c2, c2),
            scale_factor=(2.0, 2.0),
            mode="bilinear",
            align_corners=False,
        )
        c1_linear = _project_feature(self.linear_c1, c1)

        fused = self.linear_fuse(torch.cat([c4_linear, c3_linear, c2_linear, c1_linear], dim=1))
        fused = self.bn(fused)
        fused = self.relu(fused)
        return self.classifier(self.dropout(fused))

    def decode_and_refine(self, features, x, input_size):
        alpha_logits = self.alpha_decoder(features)
        fg_logits = self.fg_decoder(features)
        alpha_logits_up = F.interpolate(
            alpha_logits,
            scale_factor=(4.0, 4.0),
            mode="bilinear",
            align_corners=False,
        )
        fg_logits_up = F.interpolate(
            fg_logits,
            scale_factor=(4.0, 4.0),
            mode="bilinear",
            align_corners=False,
        )

        alpha_coarse = torch.sigmoid(alpha_logits_up)
        fg_coarse = torch.sigmoid(fg_logits_up)
        if self.config.effective_cache_clearing and x.is_cuda:
            torch.cuda.empty_cache()

        rgb = x[:, :3, :, :]
        coarse_pred = torch.cat([alpha_coarse, fg_coarse], dim=1)
        if self.use_refiner and self.refiner is not None:
            delta_logits = self.refiner(rgb, coarse_pred)
        else:
            delta_logits = torch.zeros_like(coarse_pred)

        return {
            "alpha": torch.sigmoid(alpha_logits_up + delta_logits[:, 0:1]),
            "fg": torch.sigmoid(fg_logits_up + delta_logits[:, 1:4]),
        }

    model_transformer.DecoderHead.forward = decoder_forward
    model_transformer.GreenFormer._decode_and_refine = decode_and_refine


def _patch_hiera_external_pos(hiera: torch.nn.Module) -> None:
    def forward_intermediates_external(
        self,
        x,
        pos_grid,
        mask=None,
        indices=None,
        norm=False,
        stop_early=True,
        output_fmt="NCHW",
        intermediates_only=False,
        coarse=True,
    ):
        if norm or mask is not None:
            raise RuntimeError("external-pos dynamic Hiera supports unmasked, non-normalized features only")
        if output_fmt not in ("NCHW", "NHWC"):
            raise RuntimeError("output_fmt must be NCHW or NHWC")

        token_h = x.shape[-2] // self.patch_stride[0]
        token_w = x.shape[-1] // self.patch_stride[1]
        token_size = [token_h, token_w]
        if coarse:
            take_indices, max_index = feature_take_indices(len(self.stage_ends), indices)
            take_indices = [self.stage_ends[idx] for idx in take_indices]
            max_index = self.stage_ends[max_index]
        else:
            take_indices, max_index = feature_take_indices(len(self.blocks), indices)

        x = self.patch_embed(x, mask=None)
        x = x + pos_grid.flatten(2).transpose(1, 2)
        x = _dynamic_unroll_tokens(x, token_size, list(self.unroll.schedule))

        blocks = self.blocks if (torch.jit.is_scripting() or not stop_early) else self.blocks[:max_index + 1]
        intermediates = []
        for block_idx, block in enumerate(blocks):
            x = block(x)
            if block_idx in take_indices:
                schedule, size = _reroll_schedule_for_block(self, block_idx, token_size)
                intermediate = _dynamic_reroll_tokens(x, schedule, size)
                intermediates.append(
                    intermediate.permute(0, 3, 1, 2) if output_fmt == "NCHW" else intermediate
                )

        if intermediates_only:
            return intermediates
        return x, intermediates

    hiera.forward_intermediates_external = MethodType(forward_intermediates_external, hiera)


def _patch_greenformer_external_pos(model_transformer) -> None:
    def forward_with_external_pos(self, x, pos_grid):
        input_size = x.shape[2:]
        features = self.encoder.model.forward_intermediates_external(
            x,
            pos_grid,
            indices=self.encoder.out_indices,
            norm=self.encoder.norm,
            output_fmt=self.encoder.output_fmt,
            intermediates_only=True,
        )
        return self._decode_and_refine(features, x, input_size)

    model_transformer.GreenFormer.forward_with_external_pos = forward_with_external_pos


class ExternalPosWrapper(torch.nn.Module):
    def __init__(self, model: torch.nn.Module) -> None:
        super().__init__()
        self.model = model

    def forward(self, x: torch.Tensor, pos_grid: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        out = self.model.forward_with_external_pos(x, pos_grid)
        return out["alpha"], out.get("fg", x[:, :3, :, :])


def _make_pos_grid(model: torch.nn.Module, resolution: int, dtype: torch.dtype) -> torch.Tensor:
    hiera = model.encoder.model
    pos_embed = hiera.pos_embed.float()
    channels = pos_embed.shape[2]
    src_grid = int(math.sqrt(pos_embed.shape[1]))
    token_h = resolution // hiera.patch_stride[0]
    token_w = resolution // hiera.patch_stride[1]
    grid = pos_embed.permute(0, 2, 1).view(1, channels, src_grid, src_grid)
    return F.interpolate(
        grid,
        size=(token_h, token_w),
        mode="bicubic",
        align_corners=False,
    ).to(device="cuda", dtype=dtype)


def _external_pos_extra_files(model: torch.nn.Module) -> dict[str, bytes | str]:
    hiera = model.encoder.model
    pos_embed = hiera.pos_embed.detach().float().cpu()
    channels = int(pos_embed.shape[2])
    source_grid = int(math.sqrt(pos_embed.shape[1]))
    grid = pos_embed.permute(0, 2, 1).contiguous().view(1, channels, source_grid, source_grid)
    metadata = {
        "format": "corridorkey_torchtrt_external_pos",
        "version": 1,
        "dtype": "float32",
        "shape": [1, channels, source_grid, source_grid],
        "patch_stride": [int(hiera.patch_stride[0]), int(hiera.patch_stride[1])],
        "interpolate": {"mode": "bicubic", "align_corners": False},
    }
    return {
        EXTERNAL_POS_META_NAME: json.dumps(metadata, separators=(",", ":")),
        EXTERNAL_POS_DATA_NAME: grid.numpy().tobytes(order="C"),
    }


def _collect_validation_cases(
    model: torch.nn.Module,
    base_model: torch.nn.Module,
    dtype: torch.dtype,
    validate_resolutions: list[int],
) -> list[ValidationCase]:
    cases: list[ValidationCase] = []
    for resolution in validate_resolutions:
        validation_input = torch.rand((1, 4, resolution, resolution), device="cuda", dtype=dtype)
        validation_pos = _make_pos_grid(base_model, resolution, dtype)
        with torch.no_grad():
            eager_out = model(validation_input, validation_pos)
        cases.append(
            (
                resolution,
                validation_input.detach().cpu(),
                [tensor.detach().float().cpu() for tensor in eager_out],
            )
        )
        del validation_input, validation_pos, eager_out
        gc.collect()
        torch.cuda.empty_cache()
    return cases


def _load_external_pos_model(checkpoint_path: Path, dtype: torch.dtype) -> ExternalPosWrapper:
    from CorridorKeyModule.core import model_transformer

    _patch_decoder_for_tensorrt_dynamic(model_transformer)
    _patch_greenformer_external_pos(model_transformer)

    base = _load_dynamic_model(checkpoint_path, dtype).model
    _patch_hiera_external_pos(base.encoder.model)
    return ExternalPosWrapper(base).cuda().eval()


def compile_dynamic_torchtrt(
    checkpoint_path: Path,
    output_path: Path,
    precision: str,
    enabled_precisions: list[str],
    min_resolution: int,
    opt_resolution: int,
    max_resolution: int,
    validate_resolutions: list[int],
) -> None:
    if not torch.cuda.is_available():
        raise RuntimeError("CUDA device required to compile dynamic Torch-TensorRT")

    dtype = torch.float16 if precision == "fp16" else torch.float32
    trt_precisions = {
        torch.float16 if item == "fp16" else torch.float32 for item in enabled_precisions
    }
    model = _load_external_pos_model(checkpoint_path, dtype)
    base_model = model.model

    example = torch.rand((1, 4, opt_resolution, opt_resolution), device="cuda", dtype=dtype)
    example_pos = _make_pos_grid(base_model, opt_resolution, dtype)

    height_dim = Dim("height_tokens", min=min_resolution // 32, max=max_resolution // 32)
    width_dim = Dim("width_tokens", min=min_resolution // 32, max=max_resolution // 32)
    dynamic_shapes = {
        "x": {2: 32 * height_dim, 3: 32 * width_dim},
        "pos_grid": {2: 8 * height_dim, 3: 8 * width_dim},
    }
    inputs = [
        torch_trt.Input(
            min_shape=(1, 4, min_resolution, min_resolution),
            opt_shape=(1, 4, opt_resolution, opt_resolution),
            max_shape=(1, 4, max_resolution, max_resolution),
            dtype=dtype,
        ),
        torch_trt.Input(
            min_shape=(1, 112, min_resolution // 4, min_resolution // 4),
            opt_shape=(1, 112, opt_resolution // 4, opt_resolution // 4),
            max_shape=(1, 112, max_resolution // 4, max_resolution // 4),
            dtype=dtype,
        ),
    ]

    print(
        "[compile_dynamic_torchtrt] export "
        f"{checkpoint_path.name} precision={precision} "
        f"enabled_precisions={','.join(enabled_precisions)} "
        f"range={min_resolution}-{max_resolution}",
        flush=True,
    )
    exported = torch.export.export(
        model,
        (example, example_pos),
        dynamic_shapes=dynamic_shapes,
        strict=False,
    )
    print("[compile_dynamic_torchtrt] torch_tensorrt.dynamo.compile", flush=True)
    with torch.no_grad():
        compiled = torch_trt.dynamo.compile(
            exported,
            inputs=inputs,
            enabled_precisions=trt_precisions,
            min_block_size=1,
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    extra_files = _external_pos_extra_files(base_model)
    staged_path = output_path.with_name(output_path.name + ".staged")
    if staged_path.exists():
        staged_path.unlink()
    print(f"[compile_dynamic_torchtrt] save {output_path}", flush=True)
    torch_trt.save(
        compiled,
        str(staged_path),
        output_format="torchscript",
        arg_inputs=[example, example_pos],
        dynamic_shapes=dynamic_shapes,
        pickle_protocol=4,
    )
    staged = torch.jit.load(str(staged_path), map_location="cuda").eval()
    torch.jit.save(staged, str(output_path), _extra_files=extra_files)
    staged_path.unlink()

    validation_cases = _collect_validation_cases(model, base_model, dtype, validate_resolutions)
    loaded_extra = {EXTERNAL_POS_META_NAME: "", EXTERNAL_POS_DATA_NAME: ""}
    loaded = torch.jit.load(str(output_path), map_location="cuda", _extra_files=loaded_extra).eval()
    if not loaded_extra[EXTERNAL_POS_META_NAME] or not loaded_extra[EXTERNAL_POS_DATA_NAME]:
        raise RuntimeError("saved TorchTRT artifact is missing embedded external positional data")
    for resolution, validation_input_cpu, eager_cpu in validation_cases:
        validation_input = validation_input_cpu.to("cuda")
        validation_pos = _make_pos_grid(base_model, resolution, dtype)
        with torch.no_grad():
            loaded_out = loaded(validation_input, validation_pos)
        for eager_tensor, loaded_tensor in zip(eager_cpu, loaded_out):
            if torch.isnan(loaded_tensor).any():
                raise RuntimeError(f"NaN output at validation resolution {resolution}")
            if loaded_tensor.shape[-2:] != (resolution, resolution):
                raise RuntimeError(f"loaded TorchTRT shape mismatch at {resolution}: {loaded_tensor.shape}")
            max_abs = (eager_tensor - loaded_tensor.detach().float().cpu()).abs().max().item()
            print(
                f"[compile_dynamic_torchtrt] validated {resolution} max_abs={max_abs:.6g}",
                flush=True,
            )
        del validation_input, validation_pos, loaded_out
        gc.collect()
        torch.cuda.empty_cache()

    print(
        f"[compile_dynamic_torchtrt] saved {output_path} "
        f"({output_path.stat().st_size / 1e6:.1f} MB)",
        flush=True,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--precision", choices=("fp16", "fp32"), default="fp16")
    parser.add_argument(
        "--enabled-precisions",
        choices=("fp16", "fp32"),
        nargs="*",
        help="TensorRT tactic precisions. Defaults to --precision.",
    )
    parser.add_argument("--min-resolution", type=int, default=512)
    parser.add_argument("--opt-resolution", type=int, default=1024)
    parser.add_argument("--max-resolution", type=int, default=2048)
    parser.add_argument(
        "--validate-resolutions",
        type=int,
        nargs="*",
        default=list(DEFAULT_VALIDATE_RESOLUTIONS),
    )
    parser.add_argument(
        "--repo-path",
        type=Path,
        help="Local nikopueringer/CorridorKey checkout. Omit to clone the pinned upstream.",
    )
    parser.add_argument("--force", action="store_true", help="Overwrite --output when it exists.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.output.exists() and not args.force:
        print(f"[compile_dynamic_torchtrt] {args.output} exists; pass --force", file=sys.stderr)
        return 2

    repo_to_clean: Path | None = None
    if args.repo_path:
        repo_path = args.repo_path.resolve()
    else:
        repo_path = Path(tempfile.mkdtemp(prefix="corridorkey_dynamic_trt_"))
        clone_upstream_repo(repo_path)
        repo_to_clean = repo_path

    if not (repo_path / "CorridorKeyModule").exists():
        print(f"[compile_dynamic_torchtrt] missing CorridorKeyModule under {repo_path}", file=sys.stderr)
        return 1

    sys.path.insert(0, str(repo_path))
    try:
        enabled_precisions = args.enabled_precisions or [args.precision]
        compile_dynamic_torchtrt(
            args.checkpoint.resolve(),
            args.output.resolve(),
            args.precision,
            enabled_precisions,
            args.min_resolution,
            args.opt_resolution,
            args.max_resolution,
            args.validate_resolutions,
        )
    finally:
        if repo_to_clean is not None:
            shutil.rmtree(repo_to_clean, ignore_errors=True)

    print(f"[compile_dynamic_torchtrt] upstream pin {NIKO_UPSTREAM_PIN}", flush=True)
    print(f"[compile_dynamic_torchtrt] repo root {REPO_ROOT}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
