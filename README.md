# SoftRasterizer

A software rasterizer built from scratch in C++, focused on exploring the fundamentals of computer graphics and the rendering pipeline.

## Goals

- Understand the fundamentals of 3D graphics rendering.
- Implement core rasterization algorithms.
- Explore essential rendering techniques step by step.

## Tech Stack

- **Language:** C++
- **Domain:** Computer Graphics / Software Rendering

## Build & Run

The project is built with CMake (C++17) and produces one executable target `SoftRasterizer`
plus one static library `softrasterizer_core` (TGA image I/O).

### Command line

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

The executable is always generated at `build/bin/SoftRasterizer` (`SoftRasterizer.exe` on Windows),
regardless of whether a single-config (Ninja / Makefiles) or multi-config (Visual Studio) generator is used.

Run it from the repository root, because the model and output paths are relative to the working directory:

```bash
./build/bin/SoftRasterizer
./build/bin/SoftRasterizer --width 800 --height 800 --fps 30 --duration 5 --output out/turntable.mp4
```

The renderer rotates the model around the Y axis for one full turn and writes:

- `out/diablo3_pose_turntable.mp4` — H.264 video, 1024×1024 @ 30 fps / 5 s by default
- `out/diablo3_pose_depth.tga` — the last rendered depth frame

| Option | Default | Meaning |
| --- | --- | --- |
| `--mode` | `depth` | `depth` (depth only), `color` (color only), `side` (color + depth side by side), `top` (color above depth) |
| `--width` | `1024` | Render/video width in pixels (must be even) |
| `--height` | `1024` | Render/video height in pixels (must be even) |
| `--fps` | `30` | Frames per second |
| `--duration` | `5` | Video length in seconds (total frames = fps × duration) |
| `--output` | `out/diablo3_pose_turntable.mp4` | Output MP4 path |
| `--ffmpeg` | `tools/ffmpeg/bin/ffmpeg.exe`, otherwise `ffmpeg` on `PATH` | Path to `ffmpeg.exe` |

In `side` mode the video width is doubled, and in `top` mode the height is doubled;
`--width` / `--height` always describe the render resolution of each image.
Color maps use a deterministic per-face color, so triangles keep the same color
in every frame and the video does not flicker.

### FFmpeg dependency

Video encoding lives in `src/video/videoWriter.*`. It does not link the FFmpeg libraries
(the project toolchain is 32-bit MinGW, while official Windows FFmpeg builds are 64-bit);
instead it starts `ffmpeg.exe` as a child process and pipes raw BGR frames to its stdin.

`tools/ffmpeg/` is ignored by git. Download a static Windows build (for example the
gyan.dev “essentials” build) and place `ffmpeg.exe` at `tools/ffmpeg/bin/ffmpeg.exe`,
or point `--ffmpeg` at an existing executable. Without it, rendering stops before the
first frame with a clear error message.

### VS Code

1. Install the extensions `ms-vscode.cpptools` (C/C++) and `ms-vscode.cmake-tools` (CMake Tools).
2. Open the repository root folder as the workspace.
3. `Ctrl+Shift+B` — build (task `cmake: build`, which first runs `cmake: configure`).
4. `F5` — debug with `SoftRasterizer (MinGW + gdb)` from `.vscode/launch.json`.
5. Run without debugging: `Ctrl+Shift+P` → `Tasks: Run Task` → `run: SoftRasterizer`.

Do not use "Run Code" or "C/C++: build and debug active file" for this project: those only compile
the single active file, so the symbols from `include/tgaimage.cpp` are missing at link time.
Only the CMake targets compile and link all required translation units.

## Status

🚧 Under development.

More features and documentation will be added as the project evolves.

## License

To be determined.
