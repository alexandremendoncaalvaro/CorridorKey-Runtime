"""Data pipeline for teacher-driven distillation.

The distillation dataset is built by running the GreenFormer teacher over
unlabeled green-screen clips and caching its alpha and foreground outputs
to disk as per-frame tensors. Training then consumes the cached tensors
directly so every training epoch is teacher-forward-free.

This module intentionally stays minimal for slice 0.7.5-5a: it validates
the dataset schema and exposes iterators that subsequent slices (0.7.5-5b
loss + training loop) can consume without further changes. Pseudo-label
generation and frame decoding land in follow-up slices together with the
training entry point.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass
class ClipSample:
    """A single teacher-labeled clip ready for consumption by the trainer.

    ``frames_path`` points to a directory of RGB frames decoded from the
    source video. ``alpha_path`` and ``foreground_path`` contain the
    teacher's pseudo-labels for the same frame ordering. Tensors are
    persisted as ``.pt`` files to keep the dtype stable across restarts.
    """

    frames_path: Path
    alpha_path: Path
    foreground_path: Path
    clip_id: str
    frame_count: int
    width: int
    height: int


def validate_clip_directory(clip_dir: Path) -> ClipSample:
    """Assert that a clip directory follows the expected layout.

    Expected layout::

        <clip_dir>/
          metadata.txt           frame_count, width, height, clip_id
          frames/*.pt            RGBA source frames (C,H,W float32)
          alpha/*.pt             teacher alpha labels (1,H,W float32)
          foreground/*.pt        teacher foreground labels (3,H,W float32)

    Any deviation raises ``FileNotFoundError`` with a message that
    identifies the missing component so callers can report which clip
    failed validation.
    """
    clip_dir = Path(clip_dir)
    metadata_path = clip_dir / "metadata.txt"
    if not metadata_path.exists():
        raise FileNotFoundError(f"metadata.txt missing under {clip_dir}")

    metadata_items: dict[str, str] = {}
    for line in metadata_path.read_text().splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        metadata_items[key.strip()] = value.strip()

    required_keys = {"clip_id", "frame_count", "width", "height"}
    missing_keys = required_keys - metadata_items.keys()
    if missing_keys:
        raise FileNotFoundError(
            f"metadata.txt under {clip_dir} is missing keys: {sorted(missing_keys)}"
        )

    frames_path = clip_dir / "frames"
    alpha_path = clip_dir / "alpha"
    foreground_path = clip_dir / "foreground"
    for subdir in (frames_path, alpha_path, foreground_path):
        if not subdir.is_dir():
            raise FileNotFoundError(f"expected directory not found: {subdir}")

    return ClipSample(
        frames_path=frames_path,
        alpha_path=alpha_path,
        foreground_path=foreground_path,
        clip_id=metadata_items["clip_id"],
        frame_count=int(metadata_items["frame_count"]),
        width=int(metadata_items["width"]),
        height=int(metadata_items["height"]),
    )
