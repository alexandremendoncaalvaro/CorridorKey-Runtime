## Recommended stack

- **Desktop shell:** Tauri 2
- **Frontend:** React + TypeScript + Vite
- **UI foundation:** shadcn/ui
- **Styling:** Tailwind CSS
- **Local/UI state:** Zustand
- **Async job/server state:** TanStack Query
- **Payload/form validation:** Zod
- **Advanced tables/lists:** TanStack Table
- **Forms:** React Hook Form
- **Charts/metrics (if needed):** Recharts
- **Icons:** Lucide React
- **Drag and drop:** Tauri native support + custom UI layer
- **Notifications:** Sonner
- **Light animations:** Framer Motion
- **Backend runtime:** separate C++ runtime
- **Desktop-backend bridge:** Tauri commands / events / sidecar process

---

## Architectural structure

### 1. Core runtime

Responsible for:

- inference
- model loading
- backend selection
- video pipeline
- auto-hinting
- color pipeline
- output writing

### 2. Application layer

Responsible for:

- jobs
- presets
- hardware profiles
- validation
- backend fallback
- progress tracking
- ETA and execution state
- standardized errors
- execution metadata
- output recipes

### 3. Interface bridge

Responsible for:

- Tauri commands
- progress events
- start/cancel/status job actions
- integration between UI and runtime
- diagnostics and hardware/runtime reporting

### 4. Desktop UI

Responsible for:

- drag and drop
- file selection
- presets
- advanced parameters
- progress tracking
- job history/status
- logs and results display
- guided workflow for non-technical users

---

## Product direction

The frontend must not be treated as “GUI first”.
It must be a **guided workflow layer** on top of a **runtime-first / CLI-first** architecture.

The implementation order is fixed:

1. macOS runtime robustness
2. portable macOS CLI distribution
3. stable sidecar/bridge contract
4. desktop GUI implementation
5. expansion to other architectures

Primary product goal:

- install
- drop file
- choose preset
- process
- export

The frontend should make the runtime feel **obvious**, not merely powerful.
Power must remain available, but through **progressive disclosure**.

---

## UX model

Structure the product as a **step-based pipeline**:

1. **Import**
2. **Alpha Generation / Hinting**
3. **Inference Settings**
4. **Process**
5. **Export**

This flow should be explicit in the UI.
The user must always understand:

- where they are
- what is ready
- what is missing
- what the next action is

---

## Usage modes

### Simple mode

Focused on fast execution for non-technical users:

- input
- preset
- process
- export

### Advanced mode

Focused on technical and power users:

- backend selection
- hinting controls
- despill
- refine controls
- output recipes
- diagnostics
- performance/runtime details

Advanced controls must be isolated from the default flow.

---

## Core visual layout

The UI should follow this conceptual layout:

- **Left:** jobs / projects / queue
- **Center:** large visual preview
- **Right:** contextual controls for the current step
- **Bottom:** timeline / frame markers / outputs / status

This layout should prioritize visual work first, without overwhelming the user with all controls at once.

---

## Main UI components

### Shell

- `AppShell`
- `Sidebar`
- `Topbar`
- `StatusBar`

### Main flow

- `WorkflowStepper`
- `DropZone`
- `InputFileCard`
- `OutputSettingsPanel`
- `PresetSelector`
- `BackendSelector`
- `AdvancedSettingsSheet`
- `RunJobButton`

### Execution

- `JobQueuePanel`
- `JobProgressCard`
- `ProcessingStatus`
- `ETAIndicator`
- `LogsPanel`
- `MetricsPanel`

### Preview and comparison

- `CompareViewer`
- `PreviewModeToggle`
- `FrameMarkerBar`
- `AnnotationStatusBadge`
- `ResultPreviewCard`

### Results

- `OutputFileCard`
- `ExportRecipePanel`
- `OpenOutputButton`
- `ExecutionSummary`

### Configuration

- `HardwareProfileCard`
- `ModelSelector`
- `RuntimeSettingsDialog`
- `SystemDiagnosticsPanel`

---

## App commands/actions

- select input
- define output
- choose preset
- choose backend
- adjust advanced parameters
- generate or import alpha/hints
- annotate frames when required
- start job
- cancel job
- track progress
- inspect logs
- compare input vs output
- export outputs
- open result
- view system diagnostics

---

## Backend contracts

### Minimum commands

- `process`
- `info`
- `doctor`
- `benchmark`
- `models`
- `presets`

### Requirements

- structured output
- event-based progress reporting
- ETA support where possible
- standardized errors
- consistent exit codes
- clear separation between human-readable logs and structured data

### Required machine-readable contract

- `info`, `doctor`, `benchmark`, `models`, and `presets` return a single JSON document
- `process --json` emits NDJSON events with:
  - `job_started`
  - `backend_selected`
  - `progress`
  - `warning`
  - `artifact_written`
  - `completed`
  - `failed`
  - `cancelled`
- backend fallback reasons must be structured, not embedded only in log text
- the GUI reads capabilities, model catalog, and preset catalog from the runtime instead of duplicating them

The frontend must remain a **client** of the application/runtime bridge.
No business logic should be duplicated in the UI.

---

## UX requirements

### Always-visible state

The interface must always expose:

- current job state
- queue state
- progress
- ETA or elapsed processing time
- active backend
- GPU/VRAM/runtime diagnostics when relevant
- current workflow step
- next required action

### Comparison-first preview

The product is visual.
Preview must prioritize:

- before/after comparison
- matte / FG / comp toggles
- annotation visibility
- frame markers for guided review

### Explain disabled actions

Every disabled action must explain:

- why it is disabled
- what is missing
- what the user should do next

### Error UX

Errors must be:

- human-readable
- actionable
- contextual
- free of raw backend noise by default

Advanced logs may still be available in a separate panel.

---

## Reference product inspiration

The frontend should draw inspiration from mature visual tools, without copying them directly.

### DaVinci Resolve / Fusion
Use as reference for:

- workspace structure
- large central preview
- clear inspector/controls separation
- output-oriented visual workflow

### Nuke
Use as reference for:

- viewer-centered workflows
- annotations and frame-by-frame work
- performance-oriented interaction
- shot-oriented organization

### After Effects
Use as reference for:

- guided workflows
- progressive disclosure
- simpler mental models for non-technical users
- refine / clean / spill suppression concepts

### Silhouette
Use as reference for:

- precision-oriented matte workflows
- shot/task organization
- fine control over roto and matte refinement

Target synthesis:

- **Resolve/Nuke** for workspace structure
- **After Effects** for onboarding and guided UX
- **Silhouette** for matte-focused control depth

---

## UX direction

- modern and clean interface
- minimal steps to run
- strong defaults
- clear presets
- strong progress feedback
- comparison-first workflow
- advanced parameters isolated from the main flow
- simple language for end users
- clear state and next-step communication
- architecture ready for future integrations/plugins

---

## Design principle

**Frontend should make the runtime feel obvious, not powerful. Power stays available, but progressive disclosure is mandatory.**
