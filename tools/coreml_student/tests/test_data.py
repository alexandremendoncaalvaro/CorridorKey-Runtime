"""Tests for the clip validator.

These tests build synthetic clip directories on disk (pytest ``tmp_path``)
and assert that the validator accepts well-formed layouts and rejects
broken ones with clear error messages.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from coreml_student.data import validate_clip_directory


def _write_metadata(clip_dir: Path, overrides: dict[str, str] | None = None) -> None:
    defaults = {
        "clip_id": "sample_00",
        "frame_count": "8",
        "width": "1920",
        "height": "1080",
    }
    if overrides is not None:
        defaults.update(overrides)
    metadata_lines = [f"{key}={value}" for key, value in defaults.items()]
    (clip_dir / "metadata.txt").write_text("\n".join(metadata_lines) + "\n")


def _scaffold_valid_clip(root: Path) -> Path:
    clip_dir = root / "clip"
    (clip_dir / "frames").mkdir(parents=True)
    (clip_dir / "alpha").mkdir()
    (clip_dir / "foreground").mkdir()
    _write_metadata(clip_dir)
    return clip_dir


def test_validate_clip_directory_accepts_well_formed_layout(tmp_path: Path) -> None:
    clip_dir = _scaffold_valid_clip(tmp_path)
    sample = validate_clip_directory(clip_dir)
    assert sample.clip_id == "sample_00"
    assert sample.frame_count == 8
    assert sample.width == 1920
    assert sample.height == 1080
    assert sample.frames_path == clip_dir / "frames"
    assert sample.alpha_path == clip_dir / "alpha"
    assert sample.foreground_path == clip_dir / "foreground"


def test_validate_clip_directory_rejects_missing_metadata(tmp_path: Path) -> None:
    clip_dir = _scaffold_valid_clip(tmp_path)
    (clip_dir / "metadata.txt").unlink()
    with pytest.raises(FileNotFoundError, match="metadata.txt"):
        validate_clip_directory(clip_dir)


def test_validate_clip_directory_rejects_missing_subdirectory(tmp_path: Path) -> None:
    clip_dir = _scaffold_valid_clip(tmp_path)
    (clip_dir / "alpha").rmdir()
    with pytest.raises(FileNotFoundError, match="alpha"):
        validate_clip_directory(clip_dir)


def test_validate_clip_directory_rejects_incomplete_metadata(tmp_path: Path) -> None:
    clip_dir = _scaffold_valid_clip(tmp_path)
    # Overwrite with a partial metadata block missing ``frame_count``.
    (clip_dir / "metadata.txt").write_text("clip_id=x\nwidth=100\nheight=100\n")
    with pytest.raises(FileNotFoundError, match="frame_count"):
        validate_clip_directory(clip_dir)
