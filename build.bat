@echo off
setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "GENERATOR=Visual Studio 17 2022"
set "ARCH=x64"
set "BUILD_DIR=build"
set "DECODER_DIR=deps\motioncam-decoder"
set "DECODER_URL=https://github.com/mirsadm/motioncam-decoder.git"

echo ======================================
echo MotionCam Player build
echo Config: %CONFIG%
echo Generator: %GENERATOR% (%ARCH%)
echo ======================================

if not exist "%DECODER_DIR%\lib\Decoder.cpp" (
    echo motioncam-decoder not found; fetching from %DECODER_URL% ...
    if not exist "deps" mkdir "deps"
    git clone --depth 1 "%DECODER_URL%" "%DECODER_DIR%"
    if errorlevel 1 (
        echo ERROR: Failed to fetch motioncam-decoder.
        exit /b 1
    )
)

cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A %ARCH%
if errorlevel 1 (
    echo ERROR: CMake configure failed.
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo Build complete. Binary should be at "%BUILD_DIR%\bin\%CONFIG%\MotionCam Player.exe"
exit /b 0
