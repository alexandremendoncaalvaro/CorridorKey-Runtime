"""Export dynamic CorridorKey GreenFormer checkpoints to TorchScript.

This script owns the Phase A fallback path when Torch-TensorRT cannot
materialize the dynamic Hiera graph. It keeps the existing torch 2.8/cu128
compiler stack and produces one `.ts` artifact per checkpoint that LibTorch C++
can load with `torch::jit::load`.
"""

from __future__ import annotations

import argparse
import gc
import math
import shutil
import sys
import tempfile
from pathlib import Path
from types import MethodType

import torch
import torch.nn.functional as F  # noqa: N812
from timm.models._features import feature_take_indices
from timm.models.hiera import undo_windowing

from compile_torchtrt import NIKO_UPSTREAM_PIN, REPO_ROOT, clone_upstream_repo


DEFAULT_VALIDATE_RESOLUTIONS = (512, 1024, 2048)


def _dynamic_unroll_tokens(x: torch.Tensor, size: list[int], schedule: list[tuple[int, ...]]) -> torch.Tensor:
    _, _, channels = x.shape
    cur_size = list(size)
    for strides in schedule:
        cur_size = [dim // stride for dim, stride in zip(cur_size, strides)]
        new_shape = [x.shape[0]] + sum(
            ([dim, stride] for dim, stride in zip(cur_size, strides)), []
        ) + [channels]
        x = x.view(new_shape)
        dims = len(new_shape)
        permute = [0] + list(range(2, dims - 1, 2)) + list(range(1, dims - 1, 2)) + [dims - 1]
        x = x.permute(permute)
        x = x.flatten(0, len(strides))
    return x.reshape(-1, math.prod(size), channels)


def _reroll_schedule_for_block(model: torch.nn.Module, block_idx: int,
                               token_size: list[int]) -> tuple[list[tuple[int, ...]], list[int]]:
    schedule = list(model.unroll.schedule)
    size = list(token_size)
    for idx in range(model.stage_ends[-1] + 1):
        if idx == block_idx:
            return list(schedule), list(size)
        if idx in model.stage_ends[:model.q_pool]:
            if schedule:
                size = [dim // stride for dim, stride in zip(size, schedule[0])]
            schedule = schedule[1:]
    return list(schedule), list(size)


def _dynamic_reroll_tokens(x: torch.Tensor, schedule: list[tuple[int, ...]],
                           size: list[int], mask: torch.Tensor | None = None) -> torch.Tensor:
    batch, tokens, channels = x.shape
    dims = len(size)
    cur_mu_shape = [1] * dims
    for strides in schedule:
        x = x.view(batch, *strides, tokens // math.prod(strides), *cur_mu_shape, channels)
        shape_rank = len(x.shape)
        permute = (
            [0, 1 + dims]
            + sum(
                (
                    list(pair)
                    for pair in zip(range(1, 1 + dims), range(1 + dims + 1, shape_rank - 1))
                ),
                [],
            )
            + [shape_rank - 1]
        )
        x = x.permute(permute)
        for idx in range(dims):
            cur_mu_shape[idx] *= strides[idx]
        x = x.reshape(batch, -1, *cur_mu_shape, channels)
        tokens = x.shape[1]
    x = x.view(batch, tokens, *cur_mu_shape, channels)
    if mask is not None:
        return x
    return undo_windowing(x, size, cur_mu_shape)


def _dynamic_pos_embed(model: torch.nn.Module, x: torch.Tensor, token_h: int, token_w: int) -> torch.Tensor:
    if model.pos_embed_win is not None:
        raise RuntimeError("dynamic pos_embed_win path is not validated for CorridorKey")
    if model.pos_embed is None:
        raise RuntimeError("dynamic separate positional embedding path is not validated for CorridorKey")

    pos_embed = model.pos_embed
    channels = pos_embed.shape[2]
    src_grid = int(math.sqrt(pos_embed.shape[1]))
    pos_embed = pos_embed.permute(0, 2, 1).view(1, channels, src_grid, src_grid)
    pos_embed = F.interpolate(
        pos_embed,
        size=(token_h, token_w),
        mode="bicubic",
        align_corners=False,
    )
    return x + pos_embed.flatten(2).transpose(1, 2)


def patch_hiera_for_dynamic_shapes(hiera: torch.nn.Module) -> None:
    """Patch timm Hiera state that is normally frozen by constructor img_size."""

    def forward_intermediates(self, x, mask=None, indices=None, norm=False, stop_early=True,
                              output_fmt="NCHW", intermediates_only=False, coarse=True):
        if norm or mask is not None:
            raise RuntimeError("dynamic Hiera supports unmasked, non-normalized features only")
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
        x = _dynamic_pos_embed(self, x, token_h, token_w)
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

    def forward_features(self, x, mask=None, return_intermediates=False):
        if mask is not None:
            raise RuntimeError("dynamic masked Hiera path is not supported")
        token_h = x.shape[-2] // self.patch_stride[0]
        token_w = x.shape[-1] // self.patch_stride[1]
        token_size = [token_h, token_w]

        x = self.patch_embed(x, mask=None)
        x = _dynamic_pos_embed(self, x, token_h, token_w)
        x = _dynamic_unroll_tokens(x, token_size, list(self.unroll.schedule))

        intermediates = []
        for block_idx, block in enumerate(self.blocks):
            x = block(x)
            if return_intermediates and block_idx in self.stage_ends:
                schedule, size = _reroll_schedule_for_block(self, block_idx, token_size)
                intermediates.append(_dynamic_reroll_tokens(x, schedule, size))
        if return_intermediates:
            return x, intermediates
        return x

    hiera.forward_intermediates = MethodType(forward_intermediates, hiera)
    hiera.forward_features = MethodType(forward_features, hiera)


class TupleWrapper(torch.nn.Module):
    def __init__(self, model: torch.nn.Module) -> None:
        super().__init__()
        self.model = model

    def forward(self, x: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        out = self.model(x)
        return out["alpha"], out.get("fg", x[:, :3, :, :])


def _load_dynamic_model(checkpoint_path: Path, dtype: torch.dtype) -> torch.nn.Module:
    from CorridorKeyModule.core.model_transformer import GreenFormer

    model = GreenFormer(
        encoder_name="hiera_base_plus_224.mae_in1k_ft_in1k",
        img_size=512,
        use_refiner=True,
    )
    patch_hiera_for_dynamic_shapes(model.encoder.model)

    checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=True)
    state_dict = checkpoint.get("state_dict", checkpoint)
    model_state = model.state_dict()
    adapted_state = {}
    for key, value in state_dict.items():
        if key.startswith("_orig_mod."):
            key = key[10:]
        if "pos_embed" in key and key in model_state and value.shape != model_state[key].shape:
            tokens = value.shape[1]
            channels = value.shape[2]
            src_grid = int(math.sqrt(tokens))
            dst_grid = int(math.sqrt(model_state[key].shape[1]))
            value = value.permute(0, 2, 1).view(1, channels, src_grid, src_grid)
            value = F.interpolate(
                value,
                size=(dst_grid, dst_grid),
                mode="bicubic",
                align_corners=False,
            ).flatten(2).transpose(1, 2)
        adapted_state[key] = value

    missing, unexpected = model.load_state_dict(adapted_state, strict=False)
    if missing or unexpected:
        raise RuntimeError(
            "state_dict mismatch "
            f"missing={missing[:3]} unexpected={unexpected[:3]}; "
            "use an upstream checkout matching the checkpoint"
        )

    wrapped = TupleWrapper(model).eval()
    if dtype == torch.float16:
        wrapped = wrapped.half()
    return wrapped.cuda().eval()


def export_dynamic_torchscript(checkpoint_path: Path, output_path: Path, precision: str,
                               trace_resolution: int,
                               validate_resolutions: list[int]) -> None:
    if not torch.cuda.is_available():
        raise RuntimeError("CUDA device required to export dynamic TorchScript")

    dtype = torch.float16 if precision == "fp16" else torch.float32
    model = _load_dynamic_model(checkpoint_path, dtype)
    example = torch.rand((1, 4, trace_resolution, trace_resolution), device="cuda", dtype=dtype)

    print(
        "[compile_dynamic_torchscript] tracing "
        f"{checkpoint_path.name} precision={precision} trace={trace_resolution}",
        flush=True,
    )
    with torch.no_grad():
        traced = torch.jit.trace(model, example, strict=False)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    torch.jit.save(traced, str(output_path))
    del traced
    torch.cuda.empty_cache()

    loaded = torch.jit.load(str(output_path), map_location="cuda").eval()
    for resolution in validate_resolutions:
        validation_input = torch.rand((1, 4, resolution, resolution), device="cuda", dtype=dtype)
        with torch.no_grad():
            eager_out = model(validation_input)
            loaded_out = loaded(validation_input)
        for eager_tensor, loaded_tensor in zip(eager_out, loaded_out):
            if torch.isnan(loaded_tensor).any():
                raise RuntimeError(f"NaN output at validation resolution {resolution}")
            max_abs = (eager_tensor.float() - loaded_tensor.float()).abs().max().item()
            if max_abs != 0.0:
                raise RuntimeError(f"loaded TorchScript differs from eager at {resolution}: {max_abs}")
        print(f"[compile_dynamic_torchscript] validated {resolution}", flush=True)
        del validation_input, eager_out, loaded_out
        gc.collect()
        torch.cuda.empty_cache()

    print(
        f"[compile_dynamic_torchscript] saved {output_path} "
        f"({output_path.stat().st_size / 1e6:.1f} MB)",
        flush=True,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--precision", choices=("fp16", "fp32"), default="fp16")
    parser.add_argument("--trace-resolution", type=int, default=512)
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
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite --output when it already exists.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.output.exists() and not args.force:
        print(f"[compile_dynamic_torchscript] {args.output} exists; pass --force", file=sys.stderr)
        return 2

    repo_to_clean: Path | None = None
    if args.repo_path:
        repo_path = args.repo_path.resolve()
    else:
        repo_path = Path(tempfile.mkdtemp(prefix="corridorkey_dynamic_ts_"))
        clone_upstream_repo(repo_path)
        repo_to_clean = repo_path

    if not (repo_path / "CorridorKeyModule").exists():
        print(f"[compile_dynamic_torchscript] missing CorridorKeyModule under {repo_path}", file=sys.stderr)
        return 1

    sys.path.insert(0, str(repo_path))
    try:
        export_dynamic_torchscript(
            args.checkpoint.resolve(),
            args.output.resolve(),
            args.precision,
            args.trace_resolution,
            args.validate_resolutions,
        )
    finally:
        if repo_to_clean is not None:
            shutil.rmtree(repo_to_clean, ignore_errors=True)
    print(f"[compile_dynamic_torchscript] upstream pin {NIKO_UPSTREAM_PIN}", flush=True)
    print(f"[compile_dynamic_torchscript] repo root {REPO_ROOT}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
