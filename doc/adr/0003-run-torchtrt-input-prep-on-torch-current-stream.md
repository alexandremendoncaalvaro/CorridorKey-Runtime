# ADR-0003: Run TorchTRT Input Prep On The Torch Current Stream

## Status

accepted

## Context

Resolve manual renders on the Windows RTX TorchTRT branch remain much slower
than the automated RPC harness while using the same build label and Green 2048
artifact. The latest Resolve logs show the dominant wait before model replay:
`gpu_prepare_device` is about 7 to 13 ms, but
`gpu_prepare_wait_over_device` is about 834 to 1445 ms and is reflected in
`torchtrt_input_ready_wait`.

The measured model replay remains much lower than the full frame time:
`torchtrt_replay_gpu` is usually about 276 to 289 ms, and replay queue wait is
near zero. The same build in the harness has `gpu_prepare_wait_over_device` at
0 ms, so the open failure is specific to the Resolve-driven producer and
consumer stream boundary.

Official CUDA documentation defines stream waits with `cudaStreamWaitEvent`, but
stream priority is not a correctness or preemption guarantee. Official PyTorch
C++ CUDA stream documentation uses the PyTorch current stream and
`CUDAStreamGuard` as the integration point for custom CUDA work. Official NPP
documentation supports stream contexts, but it does not make a nonblocking
independent NPP stream a required shape for this pipeline. Relevant open-source
PyTorch CUDA extensions use the PyTorch current stream when launching adjacent
custom CUDA work so ordering remains owned by the tensor runtime.

CUDA Runtime documentation also defines the default stream as the stream used
when `0` is passed as a `cudaStream_t`. Therefore a raw CUDA stream handle
cannot double as an availability sentinel: `0` can mean a valid default stream,
not "no stream."

Repository history explains why `main` does not show this exact failure mode:
`main` synchronizes GPU input preparation and downloads the prepared tensor to
host before inference. This branch removed that boundary to keep the TorchTRT
input on device, but introduced an independent GPU-prep stream and event handoff
that Resolve logs now identify as the dominant wait.

## Decision

The TorchTRT prepared-input path will enqueue GPU input preparation on the same
CUDA stream that owns the TorchTRT/PyTorch inference work. NPP operations used by
input preparation will receive an `NppStreamContext` for that stream. The
TorchTRT prepared path must not depend on an independent GPU-prep stream plus
`cudaStreamWaitEvent` to consume the prepared input.

The prepared-input path must also not synchronize the CPU thread just to measure
input readiness. When work is already ordered on the Torch current stream, and
when any cross-stream dependency has been enqueued with `cudaStreamWaitEvent`,
later Torch operations inherit the correct CUDA ordering. Host-side readiness
timing for this boundary is therefore reported as zero rather than forcing a
blocking marker.

The implementation must preserve the device-input optimization. Falling back to
the `main` host roundtrip is allowed only as an explicitly measured diagnostic
or fallback, not as the primary fix.

The change must remain inside `src/core/` internals. Public headers must not
expose CUDA, NPP, or LibTorch types. Any stream parameter crossing internal
class boundaries must use an internal-only abstraction or opaque pointer.
Availability must be represented separately from the opaque stream value so the
CUDA default stream remains a valid current-stream path.

## Consequences

The expected Resolve signature after the change is that
`gpu_prepare_wait_over_device` disappears from the TorchTRT prepared path or
stays near zero, and `torchtrt_input_ready_wait` also stays at zero. Total frame
time must not merely relabel the same 0.8 to 1.4 second wait under another
stage.

The harness must continue to pass because it already has no meaningful
GPU-prep wait. Resolve logs are required to validate the fix because the harness
does not reproduce the failure.

The implementation will reduce cross-stream concurrency in favor of predictable
ordering at the Resolve integration boundary. This is acceptable because the
measured lost time is far larger than the measured input-prep device work.

## Alternatives

Keep the high-priority independent prep stream. Rejected because Resolve logs
after that change still show `gpu_prepare_wait_over_device` around 0.8 to 1.4
seconds.

Disable CUDA Graph by default. Rejected as the first fix because the dominant
wait occurs before replay and replay GPU timing is healthy.

Return to the `main` host-roundtrip input path. Rejected as the primary fix
because it throws away the branch's device-input optimization, although it
remains a valid diagnostic fallback if the current-stream path fails.

Move the wait into `frame_prepare_inputs` with an immediate synchronization.
Rejected as the primary fix because it can hide the same wait under a different
stage unless measured as a temporary A/B diagnostic.
