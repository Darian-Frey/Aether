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
uniform int   lattice;      // 0 square, 1 hexagonal (axial storage, pointy-topped)

const float HEX_A = 0.5;
const float HEX_B = 0.86602540378443865;   // sqrt(3)/2

// Nearest hex to fractional axial (q, r): cube rounding, as View2D::hexRound.
ivec2 hexRound(vec2 qr) {
    float x = qr.x, z = qr.y, y = -x - z;
    float rx = round(x), ry = round(y), rz = round(z);
    float dx = abs(rx - x), dy = abs(ry - y), dz = abs(rz - z);
    if (dx > dy && dx > dz)      rx = -ry - rz;
    else if (dy > dz)            ry = -rx - rz;
    else                         rz = -rx - ry;
    return ivec2(int(rx), int(rz));
}

void main() {
    // gl_FragCoord is bottom-left origin; the viewport is given top-left.
    vec2 px = vec2(gl_FragCoord.x, frameSize.y - gl_FragCoord.y) - viewport.xy;
    if (px.x < 0.0 || px.y < 0.0 || px.x >= viewport.z || px.y >= viewport.w) discard;

    vec2 cell = origin + px / zoom;
    ivec2 idx;
    if (lattice == 1) {
        // Cell space is measured from the origin hex's centre.
        vec2 c = cell - vec2(0.5);
        float r = c.y / HEX_B;
        idx = hexRound(vec2(c.x - HEX_A * r, r));
    } else {
        idx = ivec2(floor(cell));
    }
    if (idx.x < 0 || idx.y < 0 || idx.x >= int(gridSize.x) || idx.y >= int(gridSize.y)) {
        finalColor = background;
        return;
    }
    uint s = texelFetch(stateTex, idx, 0).r;
    vec4 c = texelFetch(paletteTex, ivec2(int(s), 0), 0);
    if (ageShade == 1 && s >= 1u && states > 2) {
        float t = float(s - 1u) / float(states - 1);
        c.rgb *= mix(1.0, 0.3, t);
    }
    finalColor = c;
}
