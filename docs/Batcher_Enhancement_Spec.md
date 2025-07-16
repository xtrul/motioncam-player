# MotionCam Batcher Enhancement Specification

This document describes proposed improvements to the **MotionCam Batcher** add‑on. The goal is to achieve feature parity with the export options available in MotionCam Player and to improve overall usability.

## 1. Encoding Options (CPU & GPU)

### Requirements
- Mirror all export modes from the player:
  - **ProRes** – support both CPU and GPU processing.
  - **DNxHR** – support both CPU and GPU processing.
  - **HEVC** – GPU encoding, with AMD, Intel and Nvidia variants.
- Allow selecting the desired encoder per job so a batch can mix CPU and GPU tasks.
- Provide radio buttons or a dropdown in the batcher UI for choosing the mode.

### Implementation Notes
- The existing batch export uses `App::startBatchConversion` with an `ExportFormat` enum that currently exposes `PRORES`, `DNXHR` and `HEVC`.
- Extend the enum with explicit CPU/GPU variants:
  ```cpp
  enum class ExportFormat {
      PRORES_CPU,
      PRORES_GPU,
      DNXHR_CPU,
      DNXHR_GPU,
      HEVC_AMD,
      HEVC_INTEL,
      HEVC_NVIDIA
  };
  ```
- Update `startBatchConversion` and the GUI code in `GuiRender.cpp` to handle the new values.
- Use the existing `convertCurrentClipToProRes` and `convertCurrentClipToDNxHR` helpers when GPU conversion is requested. CPU paths use `exportCurrentClipToProRes` and `exportCurrentClipToDNxHR`.
- For HEVC variants, map the selection to the appropriate encoder name in `encoding_config.json` (`hevc_gpu.encoder`).

## 2. Clip Preview Window

### Requirements
- Selecting a clip in the batch list should open a resizable preview pane.
- Controls: **Play**, **Pause**, **Stop**, a seek bar with frame skip buttons, and a display of current time vs total duration.
- The pane should be dockable or float above the main window.

### Implementation Notes
- Reuse the existing playback components (`PlaybackController`, `App::performSeek`) to display the clip.
- Add a new ImGui window (e.g. `BatchPreview`) which becomes visible when `m_selectedBatchIndex >= 0`.
- Draw playback controls using ImGui buttons and slider. Use `PlaybackController` to update frames while playing.

## 3. Custom Log File Naming

### Requirements
- Each batch run writes a log file named `batcher_YYYYMMDD_HHMMSS.log`.
- Logs are placed under `logs/batcher` and never overwrite MotionCam Player logs.
- Include the start/end time and status per job plus a summary section.

### Implementation Notes
- On batch start, generate the timestamped filename using `std::chrono` and `std::put_time`.
- Append log lines in `App::m_batchLog` to this file in addition to the on‑screen panel.
- Create the `logs/batcher` folder if it does not exist.
- After `startBatchConversion` finishes, write totals for succeeded/failed jobs and the elapsed time.

## 4. Usability Improvements

### Drag & Drop
- Allow dragging `.mcraw` files or folders onto the file list. Use GLFW drop callbacks to collect paths and append them to `m_fileList`.
- When a folder is dropped, recursively add all `.mcraw` clips.

### Batch Presets
- Introduce a small JSON file storing common export settings (format, output folder, concurrency limits).
- Provide **Save Preset** and **Load Preset** buttons next to the export options.

### Progress Indicators
- Display a progress bar for the current job and an overall bar for the entire batch.
- Estimate ETA based on the average time per completed job.

### Error Handling
- Detect when an export function reports an error (non‑empty `errorMsg`) and mark that job in red in the log panel.
- Continue processing remaining items.

### Output Folder Browser
- Add a **Browse…** button that opens the system directory chooser to fill `m_outputFolder`.

### Multi‑Threading / Queue Control
- Expose numeric inputs for **Max CPU encodes** and **Max GPU encodes**.
- Manage separate queues so at most the configured number of threads are active for each type.

### Notifications
- Optionally show a desktop notification when the batch completes or when any job fails. This can be toggled via a checkbox in the UI.

## 5. File Locations
- GUI updates take place in `src/Gui/GuiRender.cpp` starting around line 190 where the batcher UI is defined.
- Batch processing logic is in `src/App/AppIO.cpp` around `startBatchConversion` (line ~2760).
- Header adjustments go into `include/App/App.h` where the enum and member variables are declared.

## 6. Summary
These enhancements bring the batcher closer to the functionality of the main MotionCam Player while improving the user experience with previews, better logging and progress tracking. The updated design also enables mixing CPU and GPU tasks in one batch and prepares the application for future encoder additions.
