import tempfile
import unittest
from pathlib import Path

import onnx
from onnx import TensorProto, helper

from prepare_windows_rtx_backend_artifacts import prepare_profile_artifacts


def build_test_model(path):
    input_value = helper.make_tensor_value_info("input_rgb_hint", TensorProto.FLOAT, [1, 4, 8, 8])
    output_value = helper.make_tensor_value_info("alpha", TensorProto.FLOAT, [1, 4, 8, 8])
    bias = helper.make_tensor(
        "bias",
        TensorProto.FLOAT,
        [1],
        [1.0],
    )
    node = helper.make_node("Add", ["input_rgb_hint", "bias"], ["alpha"])
    graph = helper.make_graph([node], "prepare-test", [input_value], [output_value], [bias])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 16)])
    onnx.save(model, path)


def count_initializer_types(model_path):
    model = onnx.load(model_path)
    counts = {}
    for initializer in model.graph.initializer:
        counts[initializer.data_type] = counts.get(initializer.data_type, 0) + 1
    return counts


class PrepareWindowsRtxBackendArtifactsTests(unittest.TestCase):
    def make_profile(self, fp16_mode):
        return {
            "name": "trt_static_512",
            "resolution": 512,
            "onnx_config": {
                "shape_policy": "static",
                "dtype_policy": "fp32_export",
            },
            "backend_config": {
                "type": "tensorrt_rtx",
                "common_config": {
                    "fp16_mode": fp16_mode,
                },
            },
            "artifacts": {
                "raw_onnx": "corridorkey_fp32_512.onnx",
                "runtime_onnx": "corridorkey_fp16_512.onnx",
            },
        }

    def test_fp16_mode_materializes_distinct_runtime_artifact(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            raw_path = temp_path / "raw.onnx"
            output_dir = temp_path / "prepared"
            output_dir.mkdir(parents=True, exist_ok=True)
            build_test_model(raw_path)

            prepared = prepare_profile_artifacts(self.make_profile(True), raw_path, output_dir)

            raw_types = count_initializer_types(Path(prepared["raw_onnx"]))
            runtime_types = count_initializer_types(Path(prepared["runtime_onnx"]))

            self.assertIn(TensorProto.FLOAT, raw_types)
            self.assertIn(TensorProto.FLOAT16, runtime_types)
            self.assertNotEqual(
                Path(prepared["raw_onnx"]).read_bytes(),
                Path(prepared["runtime_onnx"]).read_bytes(),
            )

    def test_fp32_mode_preserves_simplified_runtime_artifact(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            raw_path = temp_path / "raw.onnx"
            output_dir = temp_path / "prepared"
            output_dir.mkdir(parents=True, exist_ok=True)
            build_test_model(raw_path)

            prepared = prepare_profile_artifacts(self.make_profile(False), raw_path, output_dir)

            self.assertEqual(
                Path(prepared["raw_onnx"]).read_bytes(),
                Path(prepared["runtime_onnx"]).read_bytes(),
            )


if __name__ == "__main__":
    unittest.main()
