// 2D palette pass (SPEC §13). Maps each framebuffer pixel to a cell through
// the view transform, fetches the state, and looks it up in the palette.
// Reads only; the simulation textures are never written from here.
//
// Compiled twice, the renderer prepending #version and optionally
// AETHER_F32: a u8 grid is a usampler2D of state indices, a float grid a
// sampler2D of values in [0, 1]. One source, because everything except the
// fetch and the palette lookup is the same — and a sampler of the wrong type
// bound to a live texture unit is undefined even when it goes unread, so the
// two cannot be branches of one program.

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

#ifdef AETHER_F32
uniform sampler2D  stateTex;
#else
uniform usampler2D stateTex;
#endif
uniform sampler2D  paletteTex;
uniform vec2  frameSize;    // framebuffer size in pixels
uniform vec4  viewport;     // x, y (top-left, y down), w, h in pixels
uniform vec2  origin;       // cell coordinate at the viewport's top-left pixel
uniform float zoom;         // pixels per cell
uniform vec2  gridSize;     // cells
uniform int   states;
uniform int   ageShade;
uniform int   decayFrom;    // first state of the ageing tail, or -1
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
#ifdef AETHER_F32
    // A continuous cell holds a value, not an index, so the palette is read as
    // a ramp across the states it was built for and interpolated between
    // entries. `states` is the width of that ramp (SPEC §1, D-020).
    float v = clamp(texelFetch(stateTex, idx, 0).r, 0.0, 1.0);
    float pos = v * float(states - 1);
    int lo = int(floor(pos));
    int hi = min(lo + 1, states - 1);
    vec4 c = mix(texelFetch(paletteTex, ivec2(lo, 0), 0),
                 texelFetch(paletteTex, ivec2(hi, 0), 0), pos - float(lo));
    finalColor = vec4(c.rgb, 1.0);
    return;
#else
    uint s = texelFetch(stateTex, idx, 0).r;
    vec4 c = texelFetch(paletteTex, ivec2(int(s), 0), 0);
    if (ageShade == 1) {
        // With an ageing tail, darken only the tail: a rule whose states are
        // not ages (Wireworld, cyclic) must not be shaded by state index.
        if (decayFrom >= 0 && int(s) >= decayFrom) {
            float t = float(int(s) - decayFrom + 1) / float(states - decayFrom + 1);
            c.rgb *= mix(1.0, 0.3, t);
        } else if (decayFrom < 0 && s >= 1u && states > 2) {
            float t = float(s - 1u) / float(states - 1);
            c.rgb *= mix(1.0, 0.3, t);
        }
    }
    finalColor = vec4(c.rgb, 1.0);   // palette alpha is the 3D opacity; 2D is opaque
#endif
}
