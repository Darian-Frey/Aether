# Wraps a shader source file in a C++ raw string literal.
#   cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DSYMBOL=<name> -P EmbedShader.cmake
# The delimiter AETHERSHADER must not appear in any shader.
file(READ "${INPUT}" CONTENT)
get_filename_component(BASENAME "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
"// Generated from shaders/${BASENAME} by cmake/EmbedShader.cmake. Do not edit.
namespace aether::shaders {
extern const char* const ${SYMBOL};
const char* const ${SYMBOL} = R\"AETHERSHADER(${CONTENT})AETHERSHADER\";
}
")
