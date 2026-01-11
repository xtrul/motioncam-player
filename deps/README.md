# External dependencies

The MotionCam decoder is no longer vendored in this repository. Instead it is expected at `deps/motioncam-decoder` as a git submodule pointing to `https://github.com/mirsadm/motioncam-decoder`.

Setup options:
1) If this repo is under git, run `git submodule update --init --recursive`.
2) Without git submodules, clone manually: `git clone https://github.com/mirsadm/motioncam-decoder.git deps/motioncam-decoder`.

CMake will fail early if the decoder sources are missing from that path.
