# ADR-0004: Own The TorchTRT Work Stream

## Status

accepted

## Context

Resolve manual renders still show TorchTRT frame time above the ONNX `main`
baseline even after the GPU-prep readiness wait was removed. The current logs
for package `0.8.5-win.1-59-g7f5514a` show
`torchtrt_input_ready_wait_ms=0`, `gpu_prepare_wait_over_device_ms=0`, and
model replay around 273 to 362 ms, but
`torchtrt_cuda_graph_input_copy_queue_wait` remains around 840 to 1249 ms in
the slow frames.

Repository history shows that the last fixes moved the wait from
`gpu_prepare_wait_over_device` to `torchtrt_input_ready_wait`, then to
`torchtrt_cuda_graph_input_copy_queue_wait`. That means the stream boundary was
not owned end to end.

Official CUDA documentation defines default-stream synchronization as a
separate behavior from nonblocking streams, and documents that stream priority
is a scheduler hint, not a preemption or memory-copy guarantee. Official
PyTorch C++ CUDA stream documentation recommends `CUDAStreamGuard` for making a
stream current inside a scope, and the PyTorch CUDAStream implementation creates
pooled streams with `cudaStreamNonBlocking`.

The previous ADR selected the PyTorch current stream, but the implementation
queried `getCurrentCUDAStream()` without first installing a guard. PyTorch's
thread-local current stream initializes to the default stream, represented by a
null CUDA stream handle. Accepting that handle fixed the CPU fallback, but put
the prepared input and CUDA Graph static input copy on the default stream under
Resolve.

## Decision

TorchTRT sessions own an internal work stream from the PyTorch high-priority
stream pool. The prepared-input path exposes that work stream to GPU input
preparation, then installs a `CUDAStreamGuard` for the same work stream before
wrapping, casting, copying into the CUDA Graph static input, forwarding, GPU
post-processing, and output materialization setup.

The CUDA Graph replay stream remains separate. The copy/readback stream remains
separate. Cross-stream ordering stays explicit through events. The work stream
choice is telemetry-visible through `torchtrt_work_stream_guard`.

The primary fix must not return to the `main` host roundtrip. A host-roundtrip
path remains a diagnostic fallback only if Resolve logs prove the owned work
stream does not remove the static-input copy queue wait.

## Consequences

The expected Resolve signature is:

- `torchtrt_work_stream_guard_ms` is present in render summaries.
- `torchtrt_input_ready_wait_ms` stays at zero.
- `gpu_prepare_wait_over_device_ms` stays at zero.
- `torchtrt_input_copy_queue_wait_ms` no longer dominates frame time.
- `torchtrt_replay_gpu_ms` remains in the same model-replay range.

If the queue wait remains high, the next investigation is not another
current-stream relabel. The next step is a measured A/B between CUDA Graph on
and off, and a measured main-style host-roundtrip diagnostic, with the same
Resolve settings and source-passthrough parameters.

## Alternatives

Keep using the PyTorch default current stream. Rejected because the Resolve logs
show the wait after default-stream fallback was accepted and host readiness sync
was removed.

Use stream priority alone. Rejected because CUDA documents stream priority as a
hint and not a memory-copy preemption guarantee.

Disable CUDA Graph first. Rejected as the immediate fix because the dominant
wait is measured in the static-input copy before replay, while replay GPU time
is stable.

Return to the `main` host-roundtrip input path. Rejected as the primary fix
because it discards the branch's device-input optimization, but retained as a
diagnostic fallback.
