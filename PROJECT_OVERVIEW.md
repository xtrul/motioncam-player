Project Name: MCRAW Player

---

🎯 What It Is:
MCRAW Player is a modern C++ desktop application built for viewing, decoding, and playing `.mcraw` video files (raw video from MotionCam and similar tools). It will use:
- **C++17**
- **Qt 6** (Widgets + OpenGL)
- **OpenGL 4.3+** (compute shader pipeline)
- **GLAD** (OpenGL loader)
- **CMake** (build system)

Initial development is focused on:
- Refactoring from a single monolithic `player.cpp`
- Building a maintainable, scalable architecture
- Creating a user-friendly, real-world usable player before expanding to advanced features

---

### ✅ PHASE 1: Foundation and Architecture

🔧 GOAL: Refactor the entire codebase into a clean Qt + OpenGL app with modular C++ classes, preparing it for future features without becoming unmanageable.

🔨 Refactor `player.cpp` into modules:
- `MainWindow` – handles main UI (QMainWindow)
- `MCRAWViewport` – renders video using QOpenGLWidget
- `Decoder` – wraps motioncam::Decoder API
- `PlaybackController` – manages frame timing & playback modes
- `PlaylistManager` – manages files in folder and navigation
- `Logger` – handles logging to console/file
- `AppSettings` (optional) – remembers last folder, window size, etc.

🧱 Qt/Build Integration:
- Switch from GLFW to Qt 6 (Widgets + OpenGL)
- Integrate GLAD + shaders with QOpenGLWidget
- Setup CMake project with `AUTOUIC`, `AUTOMOC`, etc.
- Add Qt signal/slot connections for keyboard, playback, and file I/O
- Use `QFileDialog` and drag-and-drop support for file loading

🧪 Deliverable:
A modular Qt-based app that renders `.mcraw` content using OpenGL, supports keyboard input and playback controls, and is ready to grow.

---

### ✅ PHASE 2: Core Functional App (Usable MVP)

🎯 GOAL: Make the app usable for real-world work with just the essential features. Not a prototype — a real tool.

📂 File Loading & Playlist:
- [x] Open single `.mcraw` file via `QFileDialog`
- [x] Drag-and-drop support
- [x] Automatically scan folder of opened file for all `.mcraw` files
- [x] Display basic playlist (e.g., QListWidget)
- [x] Click or arrow key to play next/previous video
- [x] Automatically advance after playback or user skip

⏯ Playback Controls:
- [x] Play/Pause toggle
- [x] Timeline slider for seek
- [x] Display current frame or timestamp
- [ ] Loop toggle (optional but useful)
- [ ] Keyboard shortcuts for all major actions

🗑 File Management / Culling:
- [x] Button or key to move current file to a `deleted/` subfolder (acts as “reject”)
- [ ] Optional: undo last delete
- [x] Automatically play next file after move
- [ ] Optional: highlight deleted files in playlist or hide them

🧰 App Polish / Standalone Behavior:
- [x] App has custom icon and branding
- [x] Launchable `.exe` with drag-and-drop over Windows icon
- [ ] File extension `.mcraw` associated with app (Windows registry)
- [x] Clean welcome screen or auto-load last-used folder
- [x] Background image or app name branding when no video is loaded

📦 Build & Portability:
- [x] Build with CMake on Windows
- [ ] Optional: portable zip or installer generation
- [ ] Later: cross-platform builds for Linux/macOS

---

🚫 Deferred (Dream Features):
- DNG/ProRes export
- Custom matrix loader / DCP profiles
- 3D LUT support
- Gamma presets (ACES, LogC, etc.)
- Metadata overlays, waveform/histogram
- Audio/Video sync
- Batch export

These come only after the solid MVP core (Phase 2) is complete and stable.

---

🧠 SUMMARY:
This is a Qt 6 + OpenGL C++17 application called **MCRAW Player**, focused first on doing a small number of things **extremely well**:
- Opening `.mcraw` videos
- Playing and navigating them
- Moving them to a rejected folder
- Looking clean and usable as a standalone tool

Everything else is postponed until the core is polished.
