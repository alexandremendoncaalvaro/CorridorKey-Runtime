import unittest
from pathlib import Path

from export_windows_rtx_onnx import (
    build_export_kwargs,
    finalize_export_kwargs,
    load_profiles,
    normalize_device_name,
    select_profiles,
)


class ExportWindowsRtxOnnxTests(unittest.TestCase):
    def make_onnx_config(self, shape_policy):
        return {
            "input_name": "input_rgb_hint",
            "output_names": ["alpha", "fg"],
            "shape_policy": shape_policy,
        }

    def test_dynamic_shape_policy_exports_dynamic_batch_axes(self):
        export_kwargs = build_export_kwargs(self.make_onnx_config("dynamic"))

        self.assertIn("dynamic_axes", export_kwargs)
        self.assertEqual(export_kwargs["dynamic_axes"]["input_rgb_hint"], {0: "batch_size"})
        self.assertEqual(export_kwargs["dynamic_axes"]["alpha"], {0: "batch_size"})
        self.assertEqual(export_kwargs["dynamic_axes"]["fg"], {0: "batch_size"})

    def test_static_shape_policy_omits_dynamic_axes(self):
        export_kwargs = build_export_kwargs(self.make_onnx_config("static"))

        self.assertNotIn("dynamic_axes", export_kwargs)

    def test_unknown_shape_policy_fails(self):
        with self.assertRaises(ValueError):
            build_export_kwargs(self.make_onnx_config("unknown"))

    def test_finalize_export_kwargs_adds_dynamo_only_when_supported(self):
        base_kwargs = {"export_params": True}

        def export_with_dynamo(*args, dynamo=None, **kwargs):
            return None

        def export_without_dynamo(*args, **kwargs):
            return None

        with_dynamo = finalize_export_kwargs(base_kwargs, export_with_dynamo)
        without_dynamo = finalize_export_kwargs(base_kwargs, export_without_dynamo)

        self.assertEqual(with_dynamo["dynamo"], False)
        self.assertNotIn("dynamo", without_dynamo)

    def test_windows_rtx_profiles_use_dynamic_shape_policy(self):
        profiles_path = Path(__file__).with_name("windows_rtx_deploy_profiles.json")
        profiles = load_profiles(profiles_path)

        self.assertEqual(len(profiles), 5)
        for profile in profiles:
            self.assertEqual(profile["onnx_config"]["shape_policy"], "dynamic")
            self.assertTrue(profile["name"].startswith("trt_dynamic_"))

    def test_normalize_device_name_accepts_cpu_and_cuda_aliases(self):
        self.assertEqual(normalize_device_name("cpu"), "cpu")
        self.assertEqual(normalize_device_name("cuda"), "cuda")
        self.assertEqual(normalize_device_name("gpu"), "cuda")

    def test_normalize_device_name_rejects_unknown_device(self):
        with self.assertRaises(ValueError):
            normalize_device_name("directml")

    def test_select_profiles_filters_by_name(self):
        profiles_path = Path(__file__).with_name("windows_rtx_deploy_profiles.json")
        profiles = load_profiles(profiles_path)

        selected = select_profiles(profiles, ["trt_dynamic_1536", "trt_dynamic_2048"])

        self.assertEqual(
            [profile["name"] for profile in selected],
            ["trt_dynamic_1536", "trt_dynamic_2048"],
        )

    def test_select_profiles_rejects_unknown_name(self):
        profiles_path = Path(__file__).with_name("windows_rtx_deploy_profiles.json")
        profiles = load_profiles(profiles_path)

        with self.assertRaises(ValueError):
            select_profiles(profiles, ["trt_dynamic_9999"])


if __name__ == "__main__":
    unittest.main()
