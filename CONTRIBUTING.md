# Cross-platform guidelines

This project targets both Windows and macOS from one codebase. Please follow these rules when editing:

- **Build targets**
  - Windows: `build.bat` (Release) or `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` then `cmake --build build --config Release`.
  - macOS: install Vulkan SDK with MoltenVK (includes `glslangValidator`), SDL2, glfw3, zstd. Set `MC_MAC_DEPS_ROOT=/path/to/prefix` (or `FATPREFIX`) if not in default locations. Configure with `cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release [-DMC_BUILD_UNIVERSAL=ON]`, then `cmake --build build-mac --config Release`. Run `build-mac/MotionCam Player.app` (bundles MoltenVK/Vulkan/SDL2/glfw/zstd; ICD patched).

- **Dependencies**
  - Decoder: keep as a submodule at `deps/motioncam-decoder`; do not modify its sources here.
  - Windows uses prebuilt externals in `external/` (SDL2/glfw/ImGui/GLM/VMA).
  - macOS uses `find_package` for SDL2/glfw with hints from `MC_MAC_DEPS_ROOT` or common Homebrew paths; CMake bundles dylibs into the app.

- **Resource paths**
  - `g_AppBasePath` resolves to the exe dir on Windows and `Contents/Resources` on macOS. Shaders live in `shaders_spv`, assets in `assets`.

- **Portability rules**
  - Guard Windows-specific code with `#ifdef _WIN32`; prefer portable C++/Vulkan paths.
  - Avoid MSVC-only intrinsics/flags unless you add a Clang/mac-safe alternative.
  - Keep CMake platform-aware: preserve mac bundling steps (dylib copy + MoltenVK ICD patch) and Windows DLL copies.
  - After significant changes, at least run CMake configure on macOS to catch portability issues early.

- **Current branches**
  - `main`, `F01-Jan2026-Changes` (cross-platform work).
