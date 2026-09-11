// The single include point for direct OpenGL.
//
// rlgl does not wrap everything the engine needs — integer texture formats,
// image binding with explicit formats, memory barriers, memory-info queries —
// so those go through glad's function pointers, which raylib has already
// resolved inside libraylib. Include this header rather than glad directly so
// that direct GL use is greppable as `core/gl.hpp`.

#pragma once

#include <external/glad.h>
