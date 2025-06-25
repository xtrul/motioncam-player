================================================================================
Project: motioncam-player-v01
================================================================================

Description: A C++ application using GLFW, GLAD, and the MotionCam SDK to play
             .mcraw video files, performing debayering and rendering via an
             OpenGL compute shader.

--------------------------------------------------------------------------------
Directory Structure & Module Overview:
--------------------------------------------------------------------------------

motioncam-player-v01/
│
├── CMakeLists.txt              -> Main CMake build script for the application.
│
├── include/                    -> Public header files (.h) declaring classes/interfaces.
│   ├── App.h                   -> Main Application class declaration.
│   ├── DecoderWrapper.h        -> Wrapper for MotionCam SDK Decoder.
│   ├── Logger.h                -> Static logging utility declaration.
│   ├── PlaybackController.h    -> Playback state and timing control declaration.
│   └── Renderer.h              -> OpenGL rendering engine declaration.
│
├── src/                        -> Source files (.cpp) implementing classes/functions.
│   ├── App.cpp                 -> Application class implementation (main loop, events).
│   ├── DecoderWrapper.cpp      -> MotionCam SDK wrapper implementation.
│   ├── Logger.cpp              -> Logging implementation.
│   ├── PlaybackController.cpp  -> Playback logic implementation.
│   ├── Renderer.cpp            -> OpenGL rendering implementation (incl. shaders).
│   └── main.cpp                -> Application entry point (main function).
│
├── external/                   -> Third-party libraries source/binaries.
│   ├── glad/                   -> GLAD OpenGL loader files.
│   │   ├── include/glad/glad.h -> GLAD header.
│   │   └── src/glad.c          -> GLAD source.
│   └── glfw/                   -> GLFW library files (location assumed by CMake).
│       └── ...                 -> (GLFW headers & libs).
│
└── motioncam‑decoder/          -> MotionCam SDK (ideally as Git submodule).
    ├── CMakeLists.txt          -> SDK's own build script.
    ├── lib/include/            -> SDK public headers (e.g., motioncam/Decoder.hpp).
    └── src/                    -> SDK source files.

================================================================================
Module Details:
================================================================================

1. Module: main
   ----------
   *   Files: src/main.cpp
   *   Purpose: C++ program entry point.
   *   Responsibilities:
        *   Argument Parsing: Handles command-line args (expects `.mcraw` path).
        *   Validation: Basic checks on the input file path.
        *   Instantiation: Creates the main `App` object.
        *   Execution: Calls `App::run()` to start the player.
        *   Error Handling: Top-level `try...catch` for fatal errors.

2. Module: App
   ---------
   *   Files: include/App.h, src/App.cpp
   *   Purpose: Central application coordinator. Manages state, components, and the main loop.
   *   Responsibilities:
        *   Component Ownership: Creates and manages `DecoderWrapper`, `Renderer`, `PlaybackController`.
        *   Initialization (`initGL`): Sets up GLFW window, OpenGL context (via GLAD), logs driver info, reads initial config (static levels, CFA type from container meta).
        *   Main Loop (`run`): Drives the frame-by-frame process:
            *   Handles GLFW events (`glfwPollEvents`).
            *   Coordinates timing (`PlaybackController::updateTimingAndHandleSleep`).
            *   Loads frame data/metadata (via `DecoderWrapper::getDecoder()`).
            *   Manages frame metadata dump (`m_dumpMetadata`).
            *   Processes frame metadata (`PlaybackController::processFrameMetadata`).
            *   Determines final CFA type (considering `m_cfaOverride`).
            *   Invokes rendering (`Renderer::renderFrame`).
            *   Swaps window buffers (`glfwSwapBuffers`).
            *   Advances frame index (`PlaybackController::advanceFrame`).
            *   Updates window title.
        *   Event Handling (`handleKey`, callbacks): Manages keyboard input (fullscreen, exit, dump, CFA override, delegates playback keys) and window resizing.
        *   State: Tracks `m_isFullscreen`, `m_dumpMetadata`, `m_cfaOverride`, etc.
        *   Cleanup (`cleanup`, `~App`): Ensures proper shutdown.
   *   Dependencies: GLFW, GLAD, Logger, DecoderWrapper, Renderer, PlaybackController.

3. Module: DecoderWrapper
   ---------------------
   *   Files: include/DecoderWrapper.h, src/DecoderWrapper.cpp
   *   Purpose: Interface layer for the MotionCam SDK Decoder.
   *   Responsibilities:
        *   File Handling: Opens and validates `.mcraw` file via `motioncam::Decoder`.
        *   Metadata: Reads and provides access to container-level metadata.
        *   SDK Access: Provides access to the underlying `motioncam::Decoder` instance.
   *   Dependencies: motioncam::Decoder, nlohmann::json, Logger.

4. Module: Logger
   ------------
   *   Files: include/Logger.h, src/Logger.cpp
   *   Purpose: Simple static logging class.
   *   Responsibilities:
        *   Provides `log` function.
        *   Writes timestamped messages to console (`std::cout`) and file (`motioncam_player_compute_log.txt`).
   *   Dependencies: `<iostream>`, `<fstream>`, `<chrono>`, `<iomanip>`.

5. Module: PlaybackController
   --------------------------
   *   Files: include/PlaybackController.h, src/PlaybackController.cpp
   *   Purpose: Manages playback logic, state, and timing.
   *   Responsibilities:
        *   State: Tracks playback mode, target FPS, current frame index, total frames, metrics visibility.
        *   Timing (`updateTimingAndHandleSleep`): Implements `sleep_until` logic for Fixed FPS and Timestamp modes. Uses `std::chrono`.
        *   Timestamp Sync: Handles initialization and *resetting* of timestamp synchronization baseline (`m_firstFrameTimestampNs`, `m_playbackStartTime`) on mode switch (F5) and when looping (`advanceFrame`).
        *   Frame Advancement (`advanceFrame`): Increments frame index, handles looping, triggers timestamp reset.
        *   Input (`handleKey`): Manages playback-related key presses (F1-F5, M).
        *   Metadata (`processFrameMetadata`): Extracts frame timestamps.
        *   FPS Calculation: Computes and stores average FPS.
        *   Status Info (`getWindowStatusString`): Provides base string for window title.
   *   Dependencies: GLFW, nlohmann::json, Logger, `<chrono>`, `<thread>`.

6. Module: Renderer
   ---------------
   *   Files: include/Renderer.h, src/Renderer.cpp
   *   Purpose: Encapsulates all OpenGL rendering, including compute shader pipeline.
   *   Responsibilities:
        *   OpenGL Setup (`initialize`, `cleanup`): Manages GL resources (shaders, programs, VAO, SSBOs, texture).
        *   Shader Management: Contains GLSL source code (Compute for Debayer+Process, VS/FS for Quad display). Compiles/links shaders.
        *   Frame Rendering (`renderFrame`): Executes the render pipeline:
            *   Takes frame data, metadata, levels, and the final `cfaType`.
            *   Resizes GL resources if needed.
            *   Uploads raw data to SSBO.
            *   Sets uniforms for the compute shader.
            *   Dispatches compute shader.
            *   Uses memory barriers.
            *   Draws a textured quad using the output texture.
        *   CFA Helper (`getCfaType`): Static function to convert CFA string to integer type (handles case and misspellings).
   *   Dependencies: GLAD, nlohmann::json, Logger.

7. Module: CMakeLists.txt (Root)
   ---------------------------
   *   File: CMakeLists.txt
   *   Purpose: Top-level build configuration using CMake.
   *   Responsibilities:
        *   Defines project name and C++ standard (17).
        *   Finds required libraries (GLFW).
        *   Builds bundled libraries (GLAD).
        *   Includes and links the `motioncam-decoder` submodule/library.
        *   Specifies include directories.
        *   Compiles all source files from `src/`.
        *   Creates the final executable (`motioncam-player-v01`).
        *   Links all necessary libraries together.

8. Module: external/
   -----------------
   *   Purpose: Directory for third-party dependencies.
   *   Contents: Expected to contain GLAD source and GLFW headers/libraries (or be configured by CMake to find them elsewhere).

9. Module: motioncam-decoder/
   -------------------------
   *   Purpose: Contains the MotionCam SDK library (ideally as a Git submodule).
   *   Contents: Includes the SDK's source, headers (`lib/include/motioncam/Decoder.hpp`), and its own `CMakeLists.txt` to build the `motioncam_decoder` library target.

================================================================================