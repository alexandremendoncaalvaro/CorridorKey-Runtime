"""Regression coverage for the distillation loss components.

The loss functions are the only layer that stays in the runtime-
adjacent codebase (training scripts come and go), so they get the most
test surface. Each test uses small synthetic tensors so CI / pre-push
executions stay under one second.
"""

from __future__ import annotations

import pytest
import torch

from coreml_student.loss import (
    MattingLossConfig,
    laplacian_pyramid_loss,
    matting_distillation_loss,
    temporal_consistency_loss,
)


def test_laplacian_pyramid_loss_is_zero_on_identical_tensors() -> None:
    tensor = torch.rand(2, 1, 32, 32)
    loss = laplacian_pyramid_loss(tensor, tensor.clone(), levels=3)
    assert loss.item() == pytest.approx(0.0, abs=1e-6)


def test_laplacian_pyramid_loss_is_positive_on_different_tensors() -> None:
    prediction = torch.zeros(2, 1, 32, 32)
    target = torch.ones(2, 1, 32, 32)
    loss = laplacian_pyramid_loss(prediction, target, levels=3)
    assert loss.item() > 0.0


def test_laplacian_pyramid_loss_rejects_shape_mismatch() -> None:
    prediction = torch.zeros(1, 1, 16, 16)
    target = torch.zeros(1, 1, 8, 8)
    with pytest.raises(ValueError, match="must match"):
        laplacian_pyramid_loss(prediction, target, levels=2)


def test_temporal_consistency_loss_is_zero_when_deltas_match() -> None:
    tensor = torch.rand(1, 4, 1, 8, 8)
    loss = temporal_consistency_loss(tensor, tensor.clone())
    assert loss.item() == pytest.approx(0.0, abs=1e-6)


def test_temporal_consistency_loss_rejects_non_clip_shape() -> None:
    prediction = torch.rand(1, 1, 8, 8)
    target = torch.rand(1, 1, 8, 8)
    with pytest.raises(ValueError, match="expects"):
        temporal_consistency_loss(prediction, target)


def test_matting_distillation_loss_returns_all_components() -> None:
    batch, time, height, width = 1, 3, 16, 16
    student_alpha = torch.rand(batch, time, 1, height, width)
    student_foreground = torch.rand(batch, time, 3, height, width)
    teacher_alpha = torch.rand(batch, time, 1, height, width)
    teacher_foreground = torch.rand(batch, time, 3, height, width)
    config = MattingLossConfig()

    losses = matting_distillation_loss(
        student_alpha=student_alpha,
        student_foreground=student_foreground,
        teacher_alpha=teacher_alpha,
        teacher_foreground=teacher_foreground,
        config=config,
    )

    expected_keys = {
        "total",
        "alpha_l1",
        "alpha_laplacian",
        "alpha_temporal",
        "foreground_l1",
        "foreground_temporal",
    }
    assert expected_keys == set(losses.keys())
    # Only the total should carry a backprop graph; the rest are
    # detached snapshots intended for logging.
    assert losses["total"].requires_grad is False or losses["total"].grad_fn is not None
    for key in expected_keys - {"total"}:
        assert losses[key].requires_grad is False


def test_matting_distillation_loss_is_zero_on_teacher_copy() -> None:
    batch, time, height, width = 1, 2, 16, 16
    teacher_alpha = torch.rand(batch, time, 1, height, width)
    teacher_foreground = torch.rand(batch, time, 3, height, width)

    losses = matting_distillation_loss(
        student_alpha=teacher_alpha.clone(),
        student_foreground=teacher_foreground.clone(),
        teacher_alpha=teacher_alpha,
        teacher_foreground=teacher_foreground,
        config=MattingLossConfig(),
    )
    assert losses["total"].item() == pytest.approx(0.0, abs=1e-5)
