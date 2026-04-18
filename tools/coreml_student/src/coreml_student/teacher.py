"""Load and wrap the CorridorKey GreenFormer teacher checkpoint.

The GreenFormer lives in the public ``nikopueringer/CorridorKey`` repository as
``CorridorKeyModule.core.model_transformer.GreenFormer``. Loading the teacher
for distillation requires three steps that mirror the production inference
path:

1. Clone (or reuse) the upstream repo so the Python class is importable.
2. Instantiate ``GreenFormer`` with the target image size and load the
   ``state_dict`` from ``models/CorridorKey.pth``. When the positional
   embedding grid in the checkpoint does not match the requested image size
   we resize it via bicubic interpolation, matching the production loader.
3. Put the network in ``eval()`` mode and freeze parameters. Distillation
   only needs forward outputs from the teacher, never a gradient.

Public entry point for the rest of the tool is :func:`load_teacher`. The
module is also runnable as ``python -m coreml_student.teacher --probe``
which performs a single forward pass at 512x512 and prints output shapes;
this is the first health check before spending hours on training runs.
"""

from __future__ import annotations

import argparse
import math
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

import torch
import torch.nn.functional as F


TEACHER_REPO_URL = "https://github.com/nikopueringer/CorridorKey.git"
DEFAULT_ENCODER_NAME = "hiera_base_plus_224.mae_in1k_ft_in1k"


@dataclass
class TeacherOutputs:
    """Canonical forward outputs from the GreenFormer teacher.

    Both tensors are ``(B, C, H, W)`` NCHW. The alpha tensor is a single-
    channel mask in ``[0, 1]``; the foreground is RGB premultiplied-ready
    in the same range as the input RGB (``[0, 1]`` after ImageNet
    normalization is inverted by the runtime). Downstream distillation
    losses operate on these two tensors directly.
    """

    alpha: torch.Tensor
    foreground: torch.Tensor


class GreenFormerTeacher:
    """Thin wrapper that mirrors the production GreenFormer forward shape.

    Instances are callable via ``teacher(x)`` and return ``TeacherOutputs``.
    All parameters are frozen in ``__init__`` and the module is set to
    ``eval()`` to avoid BatchNorm / Dropout drift across distillation steps.
    """

    def __init__(self, module: torch.nn.Module) -> None:
        self._module = module
        self._module.eval()
        for parameter in self._module.parameters():
            parameter.requires_grad_(False)

    @property
    def module(self) -> torch.nn.Module:
        return self._module

    def to(self, device: torch.device | str) -> "GreenFormerTeacher":
        self._module.to(device)
        return self

    def __call__(self, x: torch.Tensor) -> TeacherOutputs:
        with torch.no_grad():
            raw = self._module(x)
        alpha = raw["alpha"]
        foreground = raw.get("fg")
        if foreground is None:
            # Fall back to a straight-through RGB when the teacher config
            # does not emit a foreground map. Distillation on the alpha
            # channel alone stays valid in that case.
            foreground = x[:, :3]
        return TeacherOutputs(alpha=alpha, foreground=foreground)


def clone_upstream_repo(target_dir: Path | None = None) -> Path:
    """Clone the upstream CorridorKey repository for class imports.

    When ``target_dir`` is supplied the caller takes ownership of the
    checkout (suitable for persistent development environments). When
    omitted a fresh tempdir is created so the checkout is scoped to the
    process lifetime.
    """
    if target_dir is None:
        target_dir = Path(tempfile.mkdtemp(prefix="corridorkey_teacher_"))
    target_dir = Path(target_dir)
    if not (target_dir / ".git").exists():
        subprocess.run(
            [
                "git",
                "clone",
                "--depth",
                "1",
                TEACHER_REPO_URL,
                str(target_dir),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    return target_dir


def _resize_positional_embedding(
    value: torch.Tensor, target_shape: torch.Size
) -> torch.Tensor:
    """Bicubic-resize a flat positional embedding to ``target_shape``.

    Embeddings are stored as ``(1, grid*grid, channels)``. The production
    inference path reshapes to ``(1, channels, grid, grid)``, interpolates,
    and flattens back. Replicating the same transform here guarantees the
    teacher's numerical behavior matches production at non-default
    resolutions.
    """
    n_src = value.shape[1]
    channels = value.shape[2]
    grid_src = int(math.sqrt(n_src))
    grid_dst = int(math.sqrt(target_shape[1]))
    v_img = value.permute(0, 2, 1).view(1, channels, grid_src, grid_src)
    v_resized = F.interpolate(
        v_img, size=(grid_dst, grid_dst), mode="bicubic", align_corners=False
    )
    return v_resized.flatten(2).transpose(1, 2)


def load_teacher(
    checkpoint_path: Path,
    img_size: int,
    repo_path: Path | None = None,
    device: torch.device | str = "cpu",
) -> GreenFormerTeacher:
    """Load the GreenFormer teacher for a specific input resolution.

    The upstream class definition is imported dynamically because the
    dependency is a git checkout rather than a pip-installable package.
    Callers that plan to invoke :func:`load_teacher` at multiple
    resolutions within the same process should pass a persistent
    ``repo_path`` so the clone is reused.
    """
    if repo_path is None:
        repo_path = clone_upstream_repo()
    repo_path = Path(repo_path)
    if not (repo_path / "CorridorKeyModule").exists():
        raise FileNotFoundError(
            f"CorridorKeyModule not found under {repo_path}. "
            "Clone github.com/nikopueringer/CorridorKey first."
        )
    if str(repo_path) not in sys.path:
        sys.path.insert(0, str(repo_path))
    from CorridorKeyModule.core.model_transformer import GreenFormer  # type: ignore

    module = GreenFormer(
        encoder_name=DEFAULT_ENCODER_NAME,
        img_size=img_size,
        use_refiner=True,
    )
    checkpoint = torch.load(str(checkpoint_path), map_location="cpu", weights_only=True)
    state_dict = checkpoint.get("state_dict", checkpoint)

    remapped: dict[str, torch.Tensor] = {}
    module_state = module.state_dict()
    for key, tensor in state_dict.items():
        if key.startswith("_orig_mod."):
            key = key[len("_orig_mod."):]
        if "pos_embed" in key and key in module_state:
            target = module_state[key]
            if tensor.shape != target.shape:
                tensor = _resize_positional_embedding(tensor, target.shape)
        remapped[key] = tensor

    missing, unexpected = module.load_state_dict(remapped, strict=False)
    if missing:
        print(f"[teacher] missing state_dict keys: {missing}")
    if unexpected:
        print(f"[teacher] unexpected state_dict keys: {unexpected}")

    module.to(device)
    return GreenFormerTeacher(module)


def iter_parameters(teacher: GreenFormerTeacher) -> Iterator[torch.nn.Parameter]:
    return teacher.module.parameters()


def _probe(args: argparse.Namespace) -> int:
    """Single-forward-pass sanity check used by ``python -m``."""
    device = torch.device(args.device)
    teacher = load_teacher(
        checkpoint_path=Path(args.ckpt),
        img_size=args.img_size,
        repo_path=Path(args.repo_path) if args.repo_path else None,
        device=device,
    )
    dummy = torch.zeros(1, 4, args.img_size, args.img_size, device=device)
    outputs = teacher(dummy)
    print(
        f"[teacher] probe alpha={tuple(outputs.alpha.shape)} "
        f"foreground={tuple(outputs.foreground.shape)} "
        f"dtype={outputs.alpha.dtype} device={outputs.alpha.device}"
    )
    return 0


def _cli(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Inspect the GreenFormer teacher.")
    parser.add_argument("--probe", action="store_true", help="Run a single forward pass and print output shapes.")
    parser.add_argument("--ckpt", default="models/CorridorKey.pth")
    parser.add_argument("--img-size", type=int, default=512)
    parser.add_argument("--repo-path", default=None)
    parser.add_argument("--device", default="cpu")
    args = parser.parse_args(argv)

    if args.probe:
        return _probe(args)
    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(_cli())
