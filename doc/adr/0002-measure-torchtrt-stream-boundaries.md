# ADR-0002: Measure TorchTRT Stream Boundaries

**Status:** accepted
**Date:** 2026-05-09
**Deciders:** Runtime maintainers

## Context

Resolve manual renders on the Windows RTX TorchTRT branch show frame times around
1.8 seconds while the model replay GPU event is usually around 0.28 to 0.33
seconds. Boundary telemetry must distinguish model replay from the stream work
that prepares the input and joins CUDA streams. The latest Resolve evidence
isolates the dominant wait at `torchtrt_input_ready_wait`, before CUDA Graph
input copy and replay.

The current evidence says the visible `torchtrt_forward` time is not enough to
identify model cost. It combines CUDA Graph replay, stream dependencies, input
readiness waits, static input copy, post-processing dependencies, and host-side
RPC timing. Harness runs with the same model and source passthrough parameters
remain much faster than Resolve-driven renders, so the decision must account for
host integration and stream scheduling, not only model execution.

The repository rules require render hot-path changes to be measured against the
optimization baseline. The team workflow also requires grounding in official
documentation, relevant open-source examples, internal examples, and repository
history before selecting a fix path.

## Decision

We will treat the TorchTRT Resolve hot path as a measured stream-bound pipeline.
Before changing execution topology, we will instrument the boundaries between
input preparation, external CUDA event waits, static graph input copy, CUDA Graph
replay, GPU elapsed time, queue wait, output materialization, readback, and OFX
writeback.

We will only accept a performance fix after Resolve logs and the benchmark
harness both show where the time moved. A fix that only improves the harness, or
only relabels the same hidden wait under another stage, is not sufficient.

## Consequences

The next implementation work must add diagnostic stages before speculative
stream changes.

The render summary must preserve enough pinned stages to answer whether a frame
is model-bound, stream-wait-bound, input-prep-bound, post-process-bound, or
host-write-bound from logs alone.

The branch can compare alternatives such as replay on the current stream,
replay on a capture stream, CUDA Graph disabled, and source passthrough variants
with the same telemetry shape.

When the measured wait is at a producer stream boundary, the fix should target
that producer stream rather than relabeling model replay or moving a blocking
sync to another stage.

This adds short-term instrumentation work before optimization. It also makes
some logs more verbose, but that verbosity is load-bearing until the regression
is closed.

## Alternatives Considered

* Optimize the model artifact first - rejected because the GPU replay event is
  much lower than the wall-clock forward time.
* Trust the benchmark harness alone - rejected because the harness is fast under
  the same model and source passthrough parameters while Resolve is slow.
* Keep a single `torchtrt_forward` timing - rejected because it hides stream
  waits and makes queue-bound frames look model-bound.
* Disable CUDA Graph by default immediately - rejected because boundary telemetry
  now separates replay from its upstream input-ready dependency.
