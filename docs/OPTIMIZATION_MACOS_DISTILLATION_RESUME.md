# macOS Optimization — Distillation Track Resume Guide

Purpose of this document: capture everything needed to resume the ANE
performance project on macOS without re-doing research. The project is
parked by product decision to prioritize shipping v0.7.5 with the
already-landed wins (ProRes VideoToolbox encoder, enriched telemetry,
byte-budgeted frame cache, Hugging Face artifact hosting). The
distillation track is the only path left with authoritative evidence
for a dramatic performance gain on Apple Silicon, but it is a
multi-week commitment that should be resumed in a dedicated session.

## The Problem We Are Solving

The macOS runtime runs the GreenFormer matting model via MLX on the
Metal GPU. Inside DaVinci Resolve this path measures 5.5 seconds per
frame with σ ≈ 5 s at 1080p / 1024 bridge, versus 275 ms for the same
stage in a standalone synthetic benchmark. The 20x penalty is the
documented Metal-scheduler behaviour: when the macOS foreground
application (Resolve) keeps the GPU busy with interactive work, long
MLX command buffers get preempted or killed. The upstream MLX
maintainers closed the mitigation request as `wontfix` because the
scheduler decision lives in macOS, not MLX. MLX issue #3267 is the
definitive statement.

## Why Distillation to RVM-Class Student

Five alternatives were evaluated with citations in the commit history
of this branch and in the agent research logs under
`.claude/plans/` in the maintainer's workspace. Three of them are dead
ends and two are conditional:

1. Direct PyTorch → Core ML of the Hiera teacher. Confirmed blocked:
   coremltools 8.3 crashes on the Hiera 5D SDPA tensor with
   `TypeError: only 0-dimensional arrays can be converted to Python
   scalars` inside the MIL frontend. Both `torch.jit.trace` and
   `torch.export` fail identically. Apple's own
   `ml-vision-transformers-ane` reference repo does not cover Hiera.
   SAM 2 (which uses a Hiera encoder) has a published Core ML package,
   but community measurement on M3 MacBook Pro showed ~4 s per frame —
   the package is not ANE-resident in practice.
2. ONNX Runtime with CoreML Execution Provider. Unvalidated for
   Hiera-class transformers; the CoreML EP documented fallback
   behaviour for int8 and many transformer ops means a material
   fraction of the graph would run on CPU. No production reference
   project uses this exact combination for video matting on M-series.
3. Direct MPSGraph execution. Technically the only API that exposes
   the ANE, but `MPSGraphDeviceType.ANE` does not exist — ANE
   placement is a compiler decision, not user-selectable. No
   documented placement rules. Multi-month R&D with no safety net.
4. Hiera BC1S structural surgery. Mathematically weight-preserving,
   but 3–6 weeks of senior ML engineering, no publicly published Hiera
   recipe (Apple covered ViT and MOAT, not Hiera), and compiler
   placement on ANE remains non-contractual even after the rewrite.
5. **Distillation to a MobileNetV3-Large + LR-ASPP + ConvGRU student
   (RVM architecture)**. Every component has published production
   deployment on Apple Silicon ANE: RVM's `coreml` branch ships FP16
   `.mlmodel` artifacts; Apple Final Cut Pro Magnetic Mask explicitly
   uses ANE; Photoroom ships ANE segmentation at 27 FPS on iPhone 15
   Pro; Topaz Video AI advertises ANE; MobileNetV3 runs convolutions
   at roughly 3x the throughput of matmul on M4 ANE.

The selected path is option 5. Expected per-frame latency after
distillation, Core ML export, and runtime wiring: 10–25 ms at 1080p,
sub-100 ms at 4K, with σ collapsed because the ANE is isolated from
Resolve's Metal workload. Quality retention target: alpha MAE vs
teacher within 5e-3 on a 50-frame holdout.

## Work Already Landed

- `tools/coreml_student/` — uv-managed Python package with torch 2.5.1
  and coremltools 8.3 pinned for deterministic Core ML exports. README
  documents every design decision.
- `src/coreml_student/teacher.py` — loads the GreenFormer teacher from
  `models/CorridorKey.pth` by cloning the upstream
  `nikopueringer/CorridorKey` repo for its Python class definitions.
  Handles positional-embedding bicubic resize for non-default
  resolutions. Runnable as `python -m coreml_student.teacher --probe`
  to confirm the checkpoint round-trips through a forward pass; the
  commit history shows this probe was verified at 512x512 against the
  real checkpoint.
- `src/coreml_student/student.py` — instantiates the upstream RVM
  `MattingNetwork` with `variant="mobilenetv3"` by cloning
  `PeterL1n/RobustVideoMatting`. Exposes a hidden-state tuple that the
  training loop threads across clip frames and the runtime will later
  persist via Core ML stateful buffers.
- `src/coreml_student/data.py` — defines the canonical on-disk clip
  schema (metadata.txt + frames/alpha/foreground tensor directories)
  that the future pseudo-label generator will populate and the trainer
  will consume. Includes a strict validator with explicit error
  messages.
- `src/coreml_student/loss.py` — Matches the Robust Video Matting
  composite loss from the WACV 2022 paper: L1 plus five-level
  Laplacian pyramid on alpha, L1 on foreground, plus temporal
  consistency L2 on frame-to-frame deltas. `MattingLossConfig` exposes
  the per-term weights for experiment sweeps. Laplacian level count
  auto-clamps for small crops so unit tests run on 16x16 tensors.
- `tests/` — 15 passing pytest cases covering teacher output
  containers, positional-embedding resize math, CLI wiring, clip
  validator acceptance and rejection cases, Laplacian identity-on-
  match, Laplacian positive-on-diff, temporal consistency zero-on-
  match, shape-mismatch rejection, and composite-loss key contract.

All of the above is usable as-is when the project resumes. No
infrastructure should need rewriting.

## Work Still To Do

### 1. Pseudo-label generator (~1 day code)

One script that walks `assets/video_samples/` (and any additional
video folders the maintainer points it at), decodes frames at the
target resolution, runs the GreenFormer teacher across each clip, and
writes the alpha + foreground outputs as `.pt` tensors alongside the
frames using the canonical schema in `coreml_student.data`. This is
slice 0.7.5-5b remaining.

Output directory target:
`models/distillation_dataset/<clip_id>/{metadata.txt, frames/*.pt,
alpha/*.pt, foreground/*.pt}`.

### 2. Training loop with checkpointing (~1 day code)

`coreml_student.train` entry point that loads the schema-validated
dataset, instantiates the RVM student, unrolls the ConvGRU across
short clips (3-frame at first, ramping to 8-frame per the RVM
curriculum), minimizes `matting_distillation_loss`, checkpoints every
N epochs to `models/student_checkpoints/step_<N>.pt`, and optionally
writes TensorBoard events. This is slice 0.7.5-5b remaining part two.

### 3. Distillation training (multi-day wall-clock)

**Recommended compute**: the maintainer's NVIDIA RTX 3080 Windows
workstation. PyTorch training on CUDA will complete a usable
distillation pass in roughly 24–48 hours, versus 10–20 days on the
Mac Mini M4 via the PyTorch MPS backend (MPS is functional but lacks
the FP16 tensor-core path CUDA has; an RTX 3080 at FP16 is an order
of magnitude faster than M4 MPS for this workload). Cloud rental is
unnecessary given the local RTX 3080.

Training strategy, three tiers by quality-effort trade-off:

- **Tier 1 — targeted overfit (fastest, demo-grade)**: teacher pseudo-
  labels generated on only the `assets/video_samples/` clips plus
  whatever 30–60 minutes of green-screen footage the maintainer feeds
  in. Train the student until alpha MAE on those specific clips
  matches the teacher. Generalization will be poor. Useful as proof
  the runtime pipeline works end to end. ~6–12 h on RTX 3080.
- **Tier 2 — focused domain (recommended starting point)**: add the
  public VideoMatte240K subset (roughly 50–80 GB download). Train the
  student against teacher pseudo-labels on this combined set. ~24–48
  h on RTX 3080. Production-grade for the green-screen workflow.
- **Tier 3 — full RVM reproduction**: match the upstream RVM training
  corpus (VideoMatte240K + Distinctions-646 + AIM-500 + COCO +
  YouTubeVIS + Supervisely Person + 14.6 GB video backgrounds + 8K
  image backgrounds). Expected 3–5 days on a single RTX 3080. Only
  needed if tier 2 quality regresses on edge cases beyond acceptance.

Pick tier 2 at resume time unless the maintainer has evidence tier 1
is acceptable.

### 4. Core ML export and quality parity (~1 day)

`coreml_student.export` runs the RVM-published export script on the
trained student checkpoint, producing `.mlpackage` artifacts per
resolution (1280x720 and 1920x1080 are the RVM-published defaults;
extend to 3840x2160 at `downsample_ratio=0.125` for 4K timelines).
`coremltools.StateType` on the ConvGRU hidden buffers.
`coreml_student.validate_parity` compares student output against
teacher output on a 50-frame holdout and reports MAE, SSIM, Grad,
Conn, and dtSSD. Acceptance: MAE < 5e-3, SSIM > 0.995.

Upload the produced `.mlpackage` files to the existing Hugging Face
repo `alexandrealvaro/corridorkey-models` using `hf upload` to keep
the runtime's single source of truth.

### 5. C++ Runtime (`CoreMLSession`) and packaging (~3–5 days)

New files:
- `src/core/coreml_session.hpp` — PIMPL class mirroring
  `MlxSession`'s `create`, `infer`, `infer_tile`, `model_resolution`
  API so `InferenceSession` routes by backend without broader
  refactoring.
- `src/core/coreml_session.mm` — Objective-C++ implementation.
  `MLModelConfiguration.computeUnits = .cpuAndNeuralEngine`
  (intentionally not `.all` per Apple's guidance for apps running
  alongside a GPU host). Compiled `.mlmodelc` cached at
  `~/Library/Caches/CorridorKey/coreml/<sha>/` to skip per-session
  specialization. ConvGRU hidden state threaded via Core ML 8's
  stateful-buffer API (`MLModel.prediction(from:usingState:)`). Input
  via `MLFeatureValue(pixelBuffer:)` for zero-copy handoff from the
  IOSurface pool the existing packaging already owns.

Existing files to edit:
- `src/core/inference_session.cpp:~1011` — new `Backend::CoreML`
  branch parallel to the MLX branch, gated by the same packaged-model
  detection the MLX path uses.
- `src/app/runtime_contracts.cpp:538-645` — new optimization profile
  `apple-silicon-coreml-ane` and a new preset `mac-coreml-balanced`
  that becomes the default when the `.mlpackage` artifacts ship.
  Existing `mac-balanced` preset stays as MLX fallback.
- `scripts/package_mac.sh`, `scripts/package_ofx_mac.sh`,
  `scripts/validate_mac_release.sh` — include `.mlpackage` files in
  the CLI and OFX bundles; validator checks for presence and
  loadability.

OFX exception-safety rules from `docs/GUIDELINES.md` §3 apply: all
entry points wrapped in top-level `try/catch(...)`, exceptions
translated to `kOfxStatFailed`, no heap allocation inside per-frame
Metal Core ML code paths.

Tests:
- `tests/integration/test_coreml_session.cpp` — alpha MAE vs MLX
  baseline within 1e-3 on the same 50-frame input.
- `tests/unit/test_coreml_session_stub.cpp` — runs on non-macOS CI
  with Core ML stubbed out to keep the cross-platform build green.

### 6. Release and measurement

- `scripts/benchmark_mac_m4.sh` sweep before and after the slice,
  committed under `dist/benchmarks/bench_m4_v0.7.X.json`.
- Expected per-frame result: 10–25 ms at 1080p, 80–150 ms at 4K
  with the async pipeline that should also be on the roadmap at
  that point.
- `scripts/release_pipeline_macos.sh` produces notarized `.dmg` and
  `.zip`; upload to the existing GitHub release via `gh release
  upload` on whichever tag is the current public target.

## Training Compute Notes

- RTX 3080: 10 GB GDDR6X, 29.77 TFLOPS FP32, ~119 TFLOPS FP16 via
  tensor cores. This matter's student will train inside the 10 GB
  budget at batch size 4 clips of 8 frames at 512x512 (activations
  plus optimizer state fit). Ample headroom for larger batch sizes at
  reduced resolution crops.
- PyTorch on RTX 3080 uses the same `torch==2.5.1` pin documented in
  `tools/coreml_student/pyproject.toml` but swap the CPU wheel for
  the CUDA wheel at setup time. The upstream `uv sync` resolves via
  the `cu121` index when `TORCH_INDEX_URL` is set. Exact command:
  `uv pip install --index-url https://download.pytorch.org/whl/cu121
  torch==2.5.1 torchvision==0.20.1`.
- Weights produced on CUDA transfer back to macOS unchanged because
  PyTorch serializes device-agnostic tensors. Checkpoint files can
  move between machines with scp or via the Hugging Face repo.

## Acceptance Gates Before Declaring The Track Done

1. Distilled student trained to alpha MAE < 5e-3 vs teacher on a
   50-frame holdout, SSIM > 0.995.
2. Core ML export ANE residency >= 80% per Xcode Performance Report,
   documented in this file or a successor.
3. `CoreMLSession` runtime passes full unit + integration + e2e +
   regression ctest presets.
4. In DaVinci Resolve 19 on Mac Mini M4 16 GB, 1080p timeline with a
   chroma-key clip: sustained under 500 ms per frame wall time
   measured via the enriched `event=render_frame_details` log from
   slice 0.7.5-2b.
5. No quality regression user-visible on a side-by-side comparison
   with the current 0.7.5 MLX path on a 50-frame holdout.

Only when all five gates pass do we bump the release version and
ship. Until then the MLX path stays the macOS default.

## References Condensed

- MLX scheduler contention with foreground app: [ml-explore/mlx #3267](https://github.com/ml-explore/mlx/issues/3267)
- Apple "deploy on ANE": [developer.apple.com/documentation/coreml/mlcomputeunits/cpuandneuralengine](https://developer.apple.com/documentation/coreml/mlcomputeunits/cpuandneuralengine)
- Apple Final Cut Pro Magnetic Mask: [apple.com support guide](https://support.apple.com/guide/final-cut-pro/add-magnetic-masks-ver1d67e3a53/mac)
- M4 ANE reverse-engineering: [maderix part 1](https://maderix.substack.com/p/inside-the-m4-apple-neural-engine), [part 2](https://maderix.substack.com/p/inside-the-m4-apple-neural-engine-615)
- Robust Video Matting: [github.com/PeterL1n/RobustVideoMatting](https://github.com/PeterL1n/RobustVideoMatting), [coreml branch](https://github.com/PeterL1n/RobustVideoMatting/tree/coreml), [paper arXiv 2108.11515](https://arxiv.org/abs/2108.11515)
- Self-distillation portrait matting: [SDNet WACV 2024](https://openaccess.thecvf.com/content/WACV2024/papers/Li_SDNet_An_Extremely_Efficient_Portrait_Matting_Model_via_Self-Distillation_WACV_2024_paper.pdf)
- Apple MobileCLIP distillation reference: [machinelearning.apple.com/research/mobileclip](https://machinelearning.apple.com/research/mobileclip)
- Photoroom Core ML benchmarks: [2022](https://www.photoroom.com/inside-photoroom/core-ml-performance-2022), [2023](https://www.photoroom.com/inside-photoroom/core-ml-performance-benchmark-2023-edition)
- coremltools stateful models: [apple.github.io/coremltools/docs-guides/source/stateful-models.html](https://apple.github.io/coremltools/docs-guides/source/stateful-models.html)
- VideoMatte240K dataset: [paperswithcode.com/dataset/videomatte240k](https://paperswithcode.com/dataset/videomatte240k)

## Out Of Scope

- Training a student from scratch without a teacher. The teacher is
  the quality anchor and the data efficiency lever.
- Serving the Hiera teacher directly on ANE through coremltools.
  Confirmed blocked.
- Running MLX on any Apple ANE device. MLX is GPU-only by design.
