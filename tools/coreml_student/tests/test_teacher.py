"""Structural tests for the teacher loader.

These tests avoid touching the 380 MB checkpoint and the upstream
CorridorKey repo so they can run on any machine without a Hugging Face
token or an internet connection. They validate the pieces that the rest
of the pipeline depends on: the ``TeacherOutputs`` container, the
positional embedding resize math, and the CLI argument wiring.
"""

from __future__ import annotations

import math

import pytest
import torch

from coreml_student.teacher import (
    TeacherOutputs,
    _resize_positional_embedding,
    _cli,
)


def test_teacher_outputs_is_a_dataclass_with_expected_fields() -> None:
    outputs = TeacherOutputs(
        alpha=torch.zeros(1, 1, 2, 2),
        foreground=torch.zeros(1, 3, 2, 2),
    )
    assert outputs.alpha.shape == (1, 1, 2, 2)
    assert outputs.foreground.shape == (1, 3, 2, 2)


def test_resize_positional_embedding_matches_target_grid() -> None:
    # Synthetic positional embedding at a 4x4 grid with 8 channels.
    src_grid = 4
    channels = 8
    embedding = torch.randn(1, src_grid * src_grid, channels)
    # Target a larger 8x8 grid: bicubic interpolation should resize cleanly.
    dst_grid = 8
    target_shape = torch.Size([1, dst_grid * dst_grid, channels])

    resized = _resize_positional_embedding(embedding, target_shape)

    assert resized.shape == target_shape
    # The resize preserves zero-mean energy when the source is zero.
    zero_embedding = torch.zeros_like(embedding)
    resized_zero = _resize_positional_embedding(zero_embedding, target_shape)
    assert torch.allclose(resized_zero, torch.zeros(target_shape))


def test_resize_positional_embedding_is_idempotent_at_matching_grid() -> None:
    grid = 4
    channels = 6
    embedding = torch.randn(1, grid * grid, channels)
    resized = _resize_positional_embedding(embedding, embedding.shape)

    assert resized.shape == embedding.shape
    assert torch.allclose(resized, embedding, atol=1e-5)


def test_cli_without_probe_prints_help(capsys: pytest.CaptureFixture[str]) -> None:
    exit_code = _cli([])
    assert exit_code == 0
    captured = capsys.readouterr()
    assert "--probe" in captured.out
