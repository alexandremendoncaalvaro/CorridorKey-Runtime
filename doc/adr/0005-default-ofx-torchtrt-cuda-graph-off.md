# ADR-0005: Default OFX TorchTRT CUDA Graph Off

## Status

accepted

## Context

CorridorKey's Windows RTX package must render predictably in DaVinci Resolve.
The TorchTRT model replay itself is not the current dominant cost. The selected
Resolve window for package `0.8.5-win.1-72-g4b72798` used a single plugin PID
and backend renders only. It recorded `cuda_graph_env=1`,
`torchtrt_cuda_graph_env=unset`, `torchtrt_input_ready_wait=0`,
`gpu_prepare_wait_over_device=0`, and
`torchtrt_cuda_graph_input_copy_queue_wait` averaging about 1055 ms while the
measured input copy stayed around 0.10 ms and graph replay GPU time averaged
about 294 ms.

The automated OFX RPC harness with the same Green 2048 source-passthrough shape
does not reproduce that queue wait. Graph-on averaged about 490 ms after the
latest instrumentation, with static-input-copy queue wait around 6.8 ms. A real
Resolve graph-off diagnostic removed the static-input-copy wait completely, but
still exposed a separate Resolve-only direct-forward wall-time gap. Therefore
there are two distinct facts: CUDA Graph is not the whole Resolve performance
problem, and the default graph-on topology is still adding a proven
Resolve-specific wait.

Official ONNX Runtime CUDA Graph documentation describes CUDA Graph support as
a constrained opt-in provider option. It requires stable input and output
addresses, I/O binding, and copying fresh inputs into the captured input
address before replay. PyTorch CUDA semantics document the same address-stable
replay model for CUDA Graphs. Those constraints support keeping graph capture
available, but they do not require enabling it by default in the OFX host.

## Decision

The OFX runtime server defaults CUDA Graph capture off when neither
`CORRIDORKEY_TRT_CUDA_GRAPH` nor `CORRIDORKEY_TORCHTRT_CUDA_GRAPH` is set by the
caller.

Explicit opt-in remains supported. `CORRIDORKEY_TRT_CUDA_GRAPH=1` enables the
generic TRT graph path, including ONNX Runtime graph capture where applicable.
`CORRIDORKEY_TORCHTRT_CUDA_GRAPH=1` enables only the TorchTRT graph path. The
diagnostic launcher remains the preferred way to run measured graph-on,
graph-off, and host-roundtrip Resolve comparisons.

This decision does not promote the host-roundtrip diagnostic path. The normal
TorchTRT path continues to use device input, the owned PyTorch work stream, GPU
source passthrough, GPU despill, and direct output materialization where those
paths are available.

## Consequences

The expected Resolve signature for the default OFX package is:

- `server_start` records `cuda_graph_env=0` unless the user explicitly opts in.
- `torchtrt_cuda_graph_input_copy_queue_wait_ms` is absent or zero by default.
- `torchtrt_cuda_graph_fallback_not_enabled` and `torchtrt_forward_direct` are
  present for the TorchTRT path by default.
- `gpu_prepare_wait_over_device_ms` and `torchtrt_input_ready_wait_ms` remain
  zero.

The remaining Resolve-only slowness must be investigated as a separate
direct-forward wall-time and peripheral pipeline problem. This ADR only removes
the proven graph static-input-copy default regression.

## Alternatives

Keep CUDA Graph on by default. Rejected because the latest single-PID Resolve
logs show about one second of queue wait before a sub-millisecond static input
copy, while the automated harness does not reproduce that wait.

Use host-roundtrip as the default. Rejected because the diagnostic run removed
the device-input boundary but shifted work back to CPU input preparation and
CPU post-processing, making end-to-end Resolve latency worse.

Keep graph-on for TorchTRT but graph-off for ONNX Runtime. Rejected for the OFX
default because the failing Resolve evidence is specifically the TorchTRT graph
static-input-copy wait.
