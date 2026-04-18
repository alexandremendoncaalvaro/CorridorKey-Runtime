"""RVM-class matting student network for distillation and Core ML export.

The student mirrors Robust Video Matting's ``MattingNetwork`` (Lin et al.,
WACV 2022): a MobileNetV3-Large encoder, a compact LR-ASPP context head, a
recurrent decoder with ConvGRU cells at multiple scales, and a final
refinement stage that runs at full resolution. Every op in this graph is
ANE-native: 1x1 / 3x3 convs, depthwise separable blocks, bilinear
upsampling, pointwise element-wise ops. No 5D window-attention tensors,
no softmax-over-sequence, none of the patterns that block Core ML
conversion on the Hiera teacher.

Why recurrent instead of per-frame:
    Matting quality on video depends on temporal consistency. RVM
    carries a ConvGRU hidden state at each decoder stage. During
    distillation training the recurrence is unrolled across short clips;
    at inference time the Core ML runtime persists the hidden state via
    ``register_buffer`` plus ``coremltools.StateType``, so the ANE keeps
    the state resident across consecutive ``predict`` calls with no host
    round-trip.

The implementation here is written to be import-clean: no copy from the
upstream RVM repo is committed into the CorridorKey tree. At use time
the module downloads ``github.com/PeterL1n/RobustVideoMatting`` into a
tempdir and reuses its ``model/`` package. This mirrors how the teacher
loader bootstraps ``nikopueringer/CorridorKey``.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import torch


RVM_REPO_URL = "https://github.com/PeterL1n/RobustVideoMatting.git"
# Keep the student on the main branch; the ``coreml`` branch's export
# script is consumed from :mod:`coreml_student.export` separately so it
# does not constrain the training-time variant class we import here.
DEFAULT_RVM_BRANCH = "master"
VARIANT_TO_CLASS = "mobilenetv3"
VARIANT_TO_CHANNELS = "resnet50"


@dataclass
class StudentOutputs:
    """Canonical forward outputs from the RVM student.

    ``alpha`` is the single-channel mask, ``foreground`` is the 3-channel
    RGB, ``hidden_state`` carries the ConvGRU tuple that a subsequent
    forward call must receive to preserve temporal context. The runtime
    in ``src/core/coreml_session.*`` threads the hidden state across
    frames via Core ML 8's stateful-buffer API.
    """

    alpha: torch.Tensor
    foreground: torch.Tensor
    hidden_state: tuple[torch.Tensor, ...]


def clone_rvm_repo(target_dir: Path | None = None) -> Path:
    """Clone Robust Video Matting for the MattingNetwork import.

    Like the teacher loader, the checkout is shallow by default and
    scoped to a tempdir unless the caller provides ``target_dir``.
    """
    if target_dir is None:
        target_dir = Path(tempfile.mkdtemp(prefix="rvm_student_"))
    target_dir = Path(target_dir)
    if not (target_dir / ".git").exists():
        subprocess.run(
            [
                "git",
                "clone",
                "--depth",
                "1",
                "--branch",
                DEFAULT_RVM_BRANCH,
                RVM_REPO_URL,
                str(target_dir),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    return target_dir


def build_student(
    variant: str = VARIANT_TO_CLASS,
    pretrained_backbone: bool = True,
    rvm_repo_path: Path | None = None,
) -> torch.nn.Module:
    """Instantiate a fresh RVM ``MattingNetwork``.

    ``variant="mobilenetv3"`` is the ANE target. ``variant="resnet50"``
    is available for reference / baseline experiments but is not part of
    the shipping student. The upstream MattingNetwork exposes a
    ``downsample_ratio`` at forward time; the Core ML export fixes the
    downsample ratio per resolution.
    """
    if rvm_repo_path is None:
        rvm_repo_path = clone_rvm_repo()
    rvm_repo_path = Path(rvm_repo_path)
    if not (rvm_repo_path / "model").exists():
        raise FileNotFoundError(
            f"model/ not found under {rvm_repo_path}. "
            "Clone github.com/PeterL1n/RobustVideoMatting first."
        )
    if str(rvm_repo_path) not in sys.path:
        sys.path.insert(0, str(rvm_repo_path))
    from model import MattingNetwork  # type: ignore

    module = MattingNetwork(
        variant=variant,
        pretrained_backbone=pretrained_backbone,
        refiner="deep_guided_filter",
    )
    return module


def forward_student(
    student: torch.nn.Module,
    frame: torch.Tensor,
    hidden_state: tuple[torch.Tensor, ...] | tuple[None, ...],
    downsample_ratio: float,
) -> StudentOutputs:
    """Run one forward pass with RVM's signature.

    The upstream MattingNetwork expects hidden state as positional args.
    We keep the tuple container here so downstream training and export
    code passes a single object around. The four-tuple is
    ``(r1, r2, r3, r4)`` for the four ConvGRU scales; on the first call
    of a clip each should be ``None`` and the net allocates zero-
    initialized state.
    """
    r1, r2, r3, r4 = hidden_state
    fg, alpha, r1, r2, r3, r4 = student(frame, r1, r2, r3, r4, downsample_ratio)
    return StudentOutputs(
        alpha=alpha,
        foreground=fg,
        hidden_state=(r1, r2, r3, r4),
    )


def init_hidden_state() -> tuple[None, None, None, None]:
    """Return the sentinel hidden state passed on the first frame of a clip."""
    return (None, None, None, None)


def count_parameters(student: torch.nn.Module) -> int:
    return sum(p.numel() for p in student.parameters())
