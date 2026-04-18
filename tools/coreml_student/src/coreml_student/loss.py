"""Distillation losses for alpha matting + foreground regression.

Mirrors the composite loss from Robust Video Matting (Lin et al.,
WACV 2022): L1 plus a five-level Laplacian pyramid on alpha, and L1 on
the foreground; a temporal consistency term drives the ConvGRU decoder
toward smooth frame-to-frame outputs. The teacher replaces the human-
labeled ground truth, so every term compares student outputs against
``TeacherOutputs`` from :mod:`coreml_student.teacher`.

The Laplacian pyramid weighting follows the reference paper (weights of
5 on the pyramid term and 5 on temporal consistency, scaling L1 at 1).
These values are exposed in :class:`MattingLossConfig` so experiments
can sweep without touching the module logic.
"""

from __future__ import annotations

from dataclasses import dataclass

import torch
import torch.nn.functional as F


@dataclass
class MattingLossConfig:
    alpha_l1_weight: float = 1.0
    alpha_laplacian_weight: float = 5.0
    alpha_temporal_weight: float = 5.0
    foreground_l1_weight: float = 1.0
    foreground_temporal_weight: float = 5.0
    laplacian_levels: int = 5


def _gaussian_kernel(channels: int, sigma: float = 1.0) -> torch.Tensor:
    """Build a 5x5 Gaussian kernel for pyramid downsampling.

    The RVM paper uses the classical Burt & Adelson (1983) pyramid with a
    5-tap [1, 4, 6, 4, 1] separable kernel. Constructing it explicitly
    avoids a torchvision dependency and keeps the op list Core-ML-safe
    in case a future slice decides to export the loss for on-device
    fine-tuning.
    """
    coefficients = torch.tensor([1.0, 4.0, 6.0, 4.0, 1.0]) / 16.0
    kernel_2d = coefficients.unsqueeze(0) * coefficients.unsqueeze(1)
    kernel = kernel_2d.unsqueeze(0).unsqueeze(0)
    return kernel.repeat(channels, 1, 1, 1)


def _downsample(x: torch.Tensor, kernel: torch.Tensor) -> torch.Tensor:
    padded = F.pad(x, pad=(2, 2, 2, 2), mode="reflect")
    filtered = F.conv2d(padded, kernel, stride=2, groups=x.shape[1])
    return filtered


def _upsample(x: torch.Tensor, target_size: tuple[int, int], kernel: torch.Tensor) -> torch.Tensor:
    upsampled = F.interpolate(x, size=target_size, mode="bilinear", align_corners=False)
    padded = F.pad(upsampled, pad=(2, 2, 2, 2), mode="reflect")
    filtered = F.conv2d(padded, kernel, groups=x.shape[1])
    return filtered


def laplacian_pyramid_loss(
    prediction: torch.Tensor,
    target: torch.Tensor,
    levels: int,
) -> torch.Tensor:
    """L1 distance summed over a Laplacian pyramid.

    The Laplacian pyramid captures matte boundary detail at multiple
    frequencies, so minimizing the per-level L1 pushes the student toward
    the teacher's edge behavior without over-emphasizing raw pixel
    values.

    The level count is automatically clamped to whatever the input
    spatial size can support: the 5x5 Gaussian kernel needs a 2-pixel
    reflective pad on each side, so levels stop being added once either
    side of the pyramid would drop to 8 pixels or fewer. This keeps the
    loss usable on small crops and inside unit tests without dropping
    real-world resolutions where all five levels fit.
    """
    if prediction.shape != target.shape:
        raise ValueError(
            f"prediction shape {prediction.shape} must match target shape {target.shape}"
        )
    channels = prediction.shape[1]
    kernel = _gaussian_kernel(channels).to(prediction.device, dtype=prediction.dtype)

    current_prediction = prediction
    current_target = target
    total = prediction.new_zeros(())
    min_side = 8  # two kernel pads per side plus headroom

    effective_levels = 0
    for _ in range(levels):
        height, width = current_prediction.shape[-2:]
        if height < min_side or width < min_side:
            break
        down_prediction = _downsample(current_prediction, kernel)
        down_target = _downsample(current_target, kernel)
        up_prediction = _upsample(
            down_prediction, current_prediction.shape[-2:], kernel
        )
        up_target = _upsample(down_target, current_target.shape[-2:], kernel)
        laplacian_prediction = current_prediction - up_prediction
        laplacian_target = current_target - up_target
        total = total + F.l1_loss(laplacian_prediction, laplacian_target)
        current_prediction = down_prediction
        current_target = down_target
        effective_levels += 1

    total = total + F.l1_loss(current_prediction, current_target)
    return total


def temporal_consistency_loss(prediction: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
    """L2 distance between frame-to-frame differences.

    ``prediction`` and ``target`` are ``(B, T, C, H, W)`` where ``T`` is
    the clip length. The temporal term penalizes the student's delta
    drifting from the teacher's delta, so the ConvGRU hidden state
    learns to produce stable alpha across consecutive frames even when
    the per-frame L1 alone would accept small jitter.
    """
    if prediction.shape != target.shape:
        raise ValueError(
            f"prediction shape {prediction.shape} must match target shape {target.shape}"
        )
    if prediction.dim() != 5:
        raise ValueError(
            "temporal_consistency_loss expects (B, T, C, H, W); got "
            f"{tuple(prediction.shape)}"
        )
    prediction_delta = prediction[:, 1:] - prediction[:, :-1]
    target_delta = target[:, 1:] - target[:, :-1]
    return F.mse_loss(prediction_delta, target_delta)


def matting_distillation_loss(
    student_alpha: torch.Tensor,
    student_foreground: torch.Tensor,
    teacher_alpha: torch.Tensor,
    teacher_foreground: torch.Tensor,
    config: MattingLossConfig,
) -> dict[str, torch.Tensor]:
    """Compose the per-clip distillation loss.

    All tensors are ``(B, T, C, H, W)``. Components are returned
    individually so the training loop can log them separately in
    addition to the aggregated total. The scalar ``total`` field drives
    backprop.
    """
    if student_alpha.dim() != 5:
        raise ValueError(
            "matting_distillation_loss expects (B, T, C, H, W); got "
            f"{tuple(student_alpha.shape)}"
        )
    # Flatten (B, T) to (B*T) for the per-frame L1 and Laplacian terms so
    # downstream convolutions operate in the usual 4D layout.
    batch, time = student_alpha.shape[:2]

    def flatten(tensor: torch.Tensor) -> torch.Tensor:
        return tensor.reshape(batch * time, *tensor.shape[2:])

    alpha_l1 = F.l1_loss(flatten(student_alpha), flatten(teacher_alpha))
    alpha_laplacian = laplacian_pyramid_loss(
        flatten(student_alpha), flatten(teacher_alpha), config.laplacian_levels
    )
    alpha_temporal = temporal_consistency_loss(student_alpha, teacher_alpha)
    foreground_l1 = F.l1_loss(flatten(student_foreground), flatten(teacher_foreground))
    foreground_temporal = temporal_consistency_loss(student_foreground, teacher_foreground)

    total = (
        config.alpha_l1_weight * alpha_l1
        + config.alpha_laplacian_weight * alpha_laplacian
        + config.alpha_temporal_weight * alpha_temporal
        + config.foreground_l1_weight * foreground_l1
        + config.foreground_temporal_weight * foreground_temporal
    )

    return {
        "total": total,
        "alpha_l1": alpha_l1.detach(),
        "alpha_laplacian": alpha_laplacian.detach(),
        "alpha_temporal": alpha_temporal.detach(),
        "foreground_l1": foreground_l1.detach(),
        "foreground_temporal": foreground_temporal.detach(),
    }
