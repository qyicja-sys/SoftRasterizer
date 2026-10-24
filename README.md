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

Run it from the repository root, because `framebuffer.tga` is written relative to the working directory:

```bash
./build/bin/SoftRasterizer
```

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
