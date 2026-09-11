# Build

How to build Aether from a clean checkout. Written at the first successful build, 2026-09-11, on the target machine (ThinkPad P15 Gen 2i, Ubuntu 24.04, NVIDIA T1200). Where this and [README.md](README.md) §Build requirements disagree, this file is more recent.

## Prerequisites

Ubuntu 24.04 package names. Other distributions need the equivalent X11 and GL development headers for GLFW.

```bash
sudo apt install build-essential cmake ninja-build git \
    libgl1-mesa-dev libx11-dev libxcursor-dev libxinerama-dev libxrandr-dev libxi-dev \
    liblua5.4-dev
```

| Requirement | Minimum | Verified with |
|---|---|---|
| C++ compiler | C++20 (GCC 12+ / Clang 15+) | GCC 13.3.0 |
| CMake | 3.20 | 3.28.3 |
| Ninja | any (optional; Make also works) | 1.11.1 |
| OpenGL | 4.3 core | 4.6 (Intel Mesa 25.2), 4.3 (NVIDIA 595.84) |
| Lua | 5.4 (Phase 4; not yet linked) | 5.4 |

raylib, Dear ImGui and rlImGui are **not** taken from the system. They are fetched and built in-tree by CMake at pinned versions so that the GL backend raylib is compiled against is a property of this project, not of whatever is installed (see [cmake/Dependencies.cmake](cmake/Dependencies.cmake)):

| Dependency | Pin | Why this pin |
|---|---|---|
| raylib | tag `6.0`, `OPENGL_VERSION=4.3` | 6.0 is the release rlImGui binds to; the 4.3 backend is mandatory, since under raylib's default 3.3 backend the compute entry points compile to no-ops |
| Dear ImGui | `v1.92.7` | the version the rlImGui tag states it was bound against |
| rlImGui | tag `Raylib_6_0` | the raylib 6.0 binding |

The first configure clones all three and takes a minute or two on a normal connection. Subsequent configures are instant.

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Omit `-G Ninja` to use Make. `CMAKE_BUILD_TYPE` defaults to Release if unset. The binary is `build/aether`.

## Verify

```bash
./build/aether --gl-check
```

Opens a hidden window, compiles a `#version 430` compute shader, dispatches it over a 4096-element SSBO, reads the result back and checks every element. Exit code 0 on success, 1 on any failure, with the renderer and version strings printed either way. This is the check that D-001 rests on; run it on any new machine before anything else.

Without the flag, `./build/aether` opens the laboratory: a 512² Life grid from random soup. `--rule`, `--size WxH`, `--cpu`, `--seed N` and `--rate G` configure the start; `--frames N --screenshot F` runs N frames headlessly-in-a-window, writes a PNG and exits, which is how the acceptance screenshots were taken.

## Running on the NVIDIA GPU

The target machine is an Optimus laptop. By default the GL context lands on the Intel iGPU. To run on the T1200 — which is where every figure in SPEC §12 is measured — use PRIME render offload:

```bash
__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./build/aether
```

Both GPUs pass `--gl-check` and the full test suite including the CPU/GPU equivalence cases. Throughput differs by more than an order of magnitude: at 1024² Life the T1200 steps at ~3,700 gen/s and the Intel iGPU at ~200; at 256³ the figures are 69 and 4. Benchmarks that do not set these variables are measuring the wrong device. The VRAM guard (`core::queryVram`) gets a real figure from the NVIDIA driver via `GL_NVX_gpu_memory_info`; Intel Mesa exposes nothing, so on the iGPU the guard passes unconditionally.

## Notes

- raylib's GLFW is built for X11 only (`GLFW_BUILD_WAYLAND=OFF`, raylib's default). Under a Wayland session it runs through XWayland.
- `compile_commands.json` is generated in `build/` for editor tooling.
- rlgl does not wrap `glMemoryBarrier`, integer texture formats, or the memory-info extensions. Direct GL goes through [src/core/gl.hpp](src/core/gl.hpp), which includes `external/glad.h` from the fetched raylib source tree; the function-pointer globals are already resolved inside `libraylib`. `aether_core` exports the include path.
