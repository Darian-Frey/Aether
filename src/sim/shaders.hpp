// Shader sources embedded at build time from shaders/ (see
// cmake/EmbedShader.cmake). The files in shaders/ are the versioned source;
// these symbols are the build's copy of them.

#pragma once

namespace aether::shaders {

extern const char* const kHashGlsl;
extern const char* const kLutStepComp;
extern const char* const kContinuousStepComp;

}  // namespace aether::shaders
