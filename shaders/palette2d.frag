#version 430
// 2D palette pass (SPEC §13). Maps each framebuffer pixel to a cell through
// the view transform, fetches the state, and looks it up in the palette.
// Reads only; the simulation textures are never written from here.

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform usampler2D stateTex;
uniform sampler2D  paletteTex;
uniform vec2  frameSize;    // framebuffer size in pixels
uniform vec4  viewport;     // x, y (top-left, y down), w, h in pixels
uniform vec2  origin;       // cell coordinate at the viewport's top-left pixel
uniform float zoom;         // pixels per cell
uniform vec2  gridSize;     // cells
uniform int   states;
uniform int   ageShade;
uniform vec4  background;

void main() {
    // gl_FragCoord is bottom-left origin; the viewport is given top-left.
    vec2 px = vec2(gl_FragCoord.x, frameSize.y - gl_FragCoord.y) - viewport.xy;
    if (px.x < 0.0 || px.y < 0.0 || px.x >= viewport.z || px.y >= viewport.w) discard;

    vec2 cell = origin + px / zoom;
    if (cell.x < 0.0 || cell.y < 0.0 || cell.x >= gridSize.x || cell.y >= gridSize.y) {
        finalColor = background;
        return;
    }
    uint s = texelFetch(stateTex, ivec2(floor(cell)), 0).r;
    vec4 c = texelFetch(paletteTex, ivec2(int(s), 0), 0);
    if (ageShade == 1 && s >= 1u && states > 2) {
        float t = float(s - 1u) / float(states - 1);
        c.rgb *= mix(1.0, 0.3, t);
    }
    finalColor = c;
}
