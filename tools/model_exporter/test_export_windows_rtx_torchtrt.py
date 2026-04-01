import unittest
from pathlib import Path

from export_windows_rtx_torchtrt import (
    PACKAGED_WINDOWS_RTX_RESOLUTIONS,
    build_compile_kwargs,
    default_torch_profiles,
    torch_torchscript_artifact_name,
)
from export_windows_rtx_onnx import load_profiles, select_profiles


class ExportWindowsRtxTorchTensorRtTests(unittest.TestCase):
    def test_default_profiles_only_include_packaged_windows_rtx_rungs(self):
        profiles_path = Path(__file__).with_name("windows_rtx_deploy_profiles.json")
        profiles = load_profiles(profiles_path)

        selected = default_torch_profiles(profiles)

        self.assertEqual([profile["resolution"] for profile in selected], [512, 1024, 1536, 2048])
        self.assertTrue(
            all(profile["resolution"] in PACKAGED_WINDOWS_RTX_RESOLUTIONS for profile in selected)
        )

    def test_torch_torchscript_artifact_name_uses_profile_artifact(self):
        profile = {
            "resolution": 1024,
            "artifacts": {
                "runtime_onnx": "corridorkey_fp16_1024.onnx",
                "torch_torchscript": "corridorkey_torchtrt_fp16_1024.ts",
            },
        }

        self.assertEqual(
            torch_torchscript_artifact_name(profile),
            "corridorkey_torchtrt_fp16_1024.ts",
        )

    def test_torch_torchscript_artifact_name_derives_from_runtime_onnx(self):
        profile = {
            "resolution": 1536,
            "artifacts": {
                "runtime_onnx": "corridorkey_fp16_1536.onnx",
            },
        }

        self.assertEqual(
            torch_torchscript_artifact_name(profile),
            "corridorkey_torchtrt_fp16_1536.ts",
        )

    def test_compile_kwargs_match_windows_rtx_best_effort_policy(self):
        profile = {
            "resolution": 2048,
            "backend_config": {
                "common_config": {
                    "max_workspace_size": 8589934592,
                }
            },
        }
        cache_root = Path("cache-root")

        kwargs = build_compile_kwargs(profile, cache_root)

        self.assertEqual(kwargs["ir"], "dynamo")
        self.assertEqual(kwargs["enabled_precisions"], {"fp16"})
        self.assertFalse(kwargs["use_explicit_typing"])
        self.assertFalse(kwargs["use_python_runtime"])
        self.assertTrue(kwargs["require_full_compilation"])
        self.assertEqual(kwargs["min_block_size"], 1)
        self.assertEqual(kwargs["optimization_level"], 5)
        self.assertFalse(kwargs["use_fast_partitioner"])
        self.assertTrue(kwargs["hardware_compatible"])
        self.assertEqual(kwargs["workspace_size"], 8589934592)
        self.assertEqual(kwargs["num_avg_timing_iters"], 8)
        self.assertTrue(kwargs["cache_built_engines"])
        self.assertTrue(kwargs["reuse_cached_engines"])
        self.assertTrue(kwargs["engine_cache_dir"].endswith("torchtrt_2048\\engines"))
        self.assertTrue(kwargs["timing_cache_path"].endswith("torchtrt_2048\\timing_cache.bin"))
        self.assertTrue(kwargs["immutable_weights"])

    def test_explicit_profile_selection_still_supports_non_packaged_reference_rungs(self):
        profiles_path = Path(__file__).with_name("windows_rtx_deploy_profiles.json")
        profiles = load_profiles(profiles_path)

        selected = select_profiles(profiles, ["trt_dynamic_768"])

        self.assertEqual(len(selected), 1)
        self.assertEqual(torch_torchscript_artifact_name(selected[0]), "corridorkey_torchtrt_fp16_768.ts")


if __name__ == "__main__":
    unittest.main()
