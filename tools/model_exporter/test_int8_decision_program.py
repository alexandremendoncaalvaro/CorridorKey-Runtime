import json
import tempfile
import unittest
from pathlib import Path

from int8_decision_program import (
    GPU_INT8_SPEEDUP_THRESHOLD,
    REQUIRED_CORPUS_CATEGORIES,
    build_decision_summary,
    compute_speedup,
    load_visual_corpus_cases,
    summarize_numeric_drift,
)


def write_image_stub(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"stub")


class Int8DecisionProgramTests(unittest.TestCase):
    def test_visual_corpus_manifest_requires_all_categories(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            image_path = temp_path / "sample.png"
            write_image_stub(image_path)

            manifest_path = temp_path / "corpus.json"
            manifest_path.write_text(
                json.dumps(
                    {
                        "version": 1,
                        "cases": [
                            {
                                "id": "hair_only",
                                "category": "hair",
                                "input": "sample.png",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            with self.assertRaises(ValueError):
                load_visual_corpus_cases(manifest_path)

    def test_visual_corpus_manifest_resolves_relative_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            for category in REQUIRED_CORPUS_CATEGORIES:
                write_image_stub(temp_path / f"{category}.png")

            manifest_path = temp_path / "corpus.json"
            manifest_path.write_text(
                json.dumps(
                    {
                        "version": 1,
                        "cases": [
                            {
                                "id": f"{category}_case",
                                "category": category,
                                "input": f"{category}.png",
                            }
                            for category in REQUIRED_CORPUS_CATEGORIES
                        ],
                    }
                ),
                encoding="utf-8",
            )

            cases = load_visual_corpus_cases(manifest_path)

            self.assertEqual(len(cases), len(REQUIRED_CORPUS_CATEGORIES))
            self.assertTrue(all(case.input_path.is_absolute() for case in cases))

    def test_compute_speedup_returns_zero_for_invalid_latencies(self) -> None:
        self.assertEqual(compute_speedup(0.0, 3.0), 0.0)
        self.assertEqual(compute_speedup(3.0, 0.0), 0.0)

    def test_summarize_numeric_drift_flattens_all_candidate_reports(self) -> None:
        summary = summarize_numeric_drift(
            [
                {
                    "numeric_drift": [
                        {
                            "alpha_mean_abs_diff": 0.01,
                            "alpha_max_abs_diff": 0.03,
                            "fg_mean_abs_diff": 0.02,
                            "fg_max_abs_diff": 0.04,
                        }
                    ]
                },
                {
                    "numeric_drift": [
                        {
                            "alpha_mean_abs_diff": 0.02,
                            "alpha_max_abs_diff": 0.06,
                            "fg_mean_abs_diff": 0.01,
                            "fg_max_abs_diff": 0.05,
                        }
                    ]
                },
            ]
        )

        self.assertAlmostEqual(summary["alpha_mean_abs_diff"], 0.015)
        self.assertEqual(summary["alpha_max_abs_diff"], 0.06)
        self.assertAlmostEqual(summary["fg_mean_abs_diff"], 0.015)
        self.assertEqual(summary["fg_max_abs_diff"], 0.05)

    def test_decision_summary_holds_when_speedup_gate_is_not_met(self) -> None:
        fp16_cli_reports = [
            {
                "steady_state_avg_latency_ms": 18.0,
            }
        ]
        gpu_int8_reports = [
            {
                "resolution": 1024,
                "benchmark": {
                    "steady_state_avg_latency_ms": 12.0,
                },
                "numeric_drift": [],
            }
        ]

        decision = build_decision_summary(fp16_cli_reports, [], gpu_int8_reports, [])

        self.assertEqual(decision["status"], "pending_visual_review")
        self.assertLess(decision["gpu_int8_speedups"]["1024"], GPU_INT8_SPEEDUP_THRESHOLD)
        self.assertTrue(decision["reasons"])


if __name__ == "__main__":
    unittest.main()
