# ORT/TensorRT RTX session-reuse state growth

## Business context

Reusing the same ONNX Runtime session across consecutive `Run()` calls on
the TensorRT RTX execution provider produces monotonically growing
inference latency. Rebuilding the session between render sequences (the
v0.7.3 behavior, restored for v0.7.5-4) keeps frame times flat at the
cost of a one-off `prepare_session` (~700 ms) per sequence. Removing
that mitigation depends on fixing the underlying growth.

## Symptom

Same session reused across 16 consecutive renders (Windows RTX, 4K input,
target resolution 2048, I/O binding enabled):

| Frame | `ort_run` (ms) |
|---|---|
| 1 | 374 |
| 5 | 380 |
| 8 | 605 |
| 12 | 2170 |
| 16 | 5391 |

Pre/post-processing stages remained stable; the growth is entirely inside
`m_session.Run(bound_io_state->binding)`
([src/core/inference_session.cpp:1659 / 1920](../src/core/inference_session.cpp)).

## Reproduction

1. Build a TensorRT RTX bundle without the `end_sequence_render`
   `release_session` mitigation.
2. Apply CorridorKey to a 4K clip in DaVinci Resolve at Maximum (2048).
3. Render 16 or more consecutive preview frames without closing the node.
4. Inspect `%LOCALAPPDATA%\CorridorKey\Logs\ofx_runtime_server_<version>.log`
   and compare `render_frame_details ... stages=ort_run:*` across frames.

## Hypotheses to investigate

- TensorRT RTX context pool or workspace allocator not recycling between
  `Run()` calls. Explore `trt_context_memory_sharing_enable`,
  `trt_cuda_graph_enable`, and related provider options at
  `append_tensorrt_rtx_execution_provider`
  ([src/core/inference_session.cpp:544](../src/core/inference_session.cpp)).
- IoBinding output bindings. Output tensors are bound once in
  `ensure_bound_io_state`
  ([src/core/inference_session.cpp:784-796](../src/core/inference_session.cpp))
  and never cleared. A periodic `ClearBoundOutputs` followed by a rebind
  may force the EP to flush cached context state.
- CUDA memory arena fragmentation inside ORT. Consider disabling the
  arena allocator (`session.use_env_allocators=0`) for the TRT path and
  measuring the delta.

## What mitigation is currently in place

`src/plugins/ofx/ofx_instance.cpp::end_sequence_render` releases the
out-of-process runtime session at the end of each render sequence. The
comment there links back to this document.

## What must hold before the mitigation is removed

- A reproducer that shows `ort_run` stable across 100+ renders on the
  same session.
- A ctest case in `tests/gpu/` that asserts flat latency across N runs
  (variance ≤ 20%).
- Updated `docs/RELEASE_GUIDELINES.md` acceptance criteria aligned with
  the new steady-state.
