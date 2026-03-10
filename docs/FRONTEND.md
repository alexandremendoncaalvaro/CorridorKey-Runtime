## Recommended stack

* **Desktop shell:** Tauri 2
* **Frontend:** React + TypeScript + Vite
* **UI foundation:** shadcn/ui
* **Styling:** Tailwind CSS
* **Local/UI state:** Zustand
* **Async job/server state:** TanStack Query
* **Payload/form validation:** Zod
* **Advanced tables/lists:** TanStack Table
* **Forms:** React Hook Form
* **Charts/metrics (if needed):** Recharts
* **Icons:** Lucide React
* **Drag and drop:** Tauri native support + custom UI layer
* **Notifications:** Sonner
* **Light animations:** Framer Motion
* **Backend runtime:** separate C++ runtime
* **Desktop-backend bridge:** Tauri commands / events / sidecar process

---

## Architectural structure

### 1. Core runtime

Responsible for:

* inference
* model loading
* backend selection
* video pipeline
* auto-hinting
* color pipeline
* output writing

### 2. Application layer

Responsible for:

* jobs
* presets
* hardware profiles
* validation
* backend fallback
* progress tracking
* standardized errors
* execution metadata

### 3. Interface bridge

Responsible for:

* Tauri commands
* progress events
* start/cancel/status job actions
* integration between UI and runtime

### 4. Desktop UI

Responsible for:

* drag and drop
* file selection
* presets
* advanced parameters
* progress tracking
* job history/status
* logs and results display

---

## Main UI components

### Shell

* `AppShell`
* `Sidebar`
* `Topbar`
* `StatusBar`

### Main flow

* `DropZone`
* `InputFileCard`
* `OutputSettingsPanel`
* `PresetSelector`
* `BackendSelector`
* `AdvancedSettingsSheet`
* `RunJobButton`

### Execution

* `JobQueuePanel`
* `JobProgressCard`
* `ProcessingStatus`
* `LogsPanel`
* `MetricsPanel`

### Results

* `ResultPreviewCard`
* `OutputFileCard`
* `OpenOutputButton`
* `ExecutionSummary`

### Configuration

* `HardwareProfileCard`
* `ModelSelector`
* `RuntimeSettingsDialog`
* `SystemDiagnosticsPanel`

---

## App commands/actions

* select input
* define output
* choose preset
* choose backend
* adjust advanced parameters
* start job
* cancel job
* track progress
* open result
* view logs
* view system diagnostics

---

## Backend contracts

### Minimum commands

* `process`
* `info`
* `doctor`
* `benchmark`
* `models`
* `presets`

### Requirements

* structured output
* event-based progress reporting
* standardized errors
* consistent exit codes
* clear separation between human-readable logs and structured data

---

## UX direction

* modern and clean interface
* minimal steps to run
* strong defaults
* clear presets
* strong progress feedback
* advanced parameters isolated from the main flow
* simple language for end users
* architecture ready for future integrations/plugins
