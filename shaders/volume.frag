#version 430
// Volume pass (F-019, SPEC §13): front-to-back voxel traversal through the
// 3D state texture with colour and opacity from the palette. Reads only.
//
// Amanatides-Woo DDA: every voxel along the ray is visited exactly once,
// so cells render as cubes and the loop is bounded by W + H + D.

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform usampler3D stateTex;
uniform sampler2D  paletteTex;
uniform vec2  frameSize;
uniform vec4  viewport;      // x, y (top-left, y down), w, h
uniform vec3  camPos;
uniform vec3  camForward;
uniform vec3  camRight;
uniform vec3  camUp;
uniform float tanHalfFov;
uniform vec3  gridSize;      // cells
uniform vec3  clipMin;       // cells, inclusive
uniform vec3  clipMax;       // cells, exclusive
uniform float opacity;       // multiplies palette alpha
uniform int   maxSteps;
uniform vec4  background;

const float FACE_SHADE[3] = float[3](0.80, 1.00, 0.62);   // x, y, z faces

void main() {
    vec2 px = vec2(gl_FragCoord.x, frameSize.y - gl_FragCoord.y) - viewport.xy;
    if (px.x < 0.0 || px.y < 0.0 || px.x >= viewport.z || px.y >= viewport.w) discard;

    vec2 ndc = vec2(px.x / viewport.z * 2.0 - 1.0, 1.0 - px.y / viewport.w * 2.0);
    float aspect = viewport.z / viewport.w;
    vec3 dir = normalize(camForward + camRight * (ndc.x * tanHalfFov * aspect) + camUp * (ndc.y * tanHalfFov));

    // Ray vs the clip box (slab method).
    vec3 invDir = 1.0 / dir;
    vec3 t0 = (clipMin - camPos) * invDir;
    vec3 t1 = (clipMax - camPos) * invDir;
    vec3 tNear = min(t0, t1);
    vec3 tFar  = max(t0, t1);
    float tEnter = max(max(tNear.x, tNear.y), tNear.z);
    float tExit  = min(min(tFar.x, tFar.y), tFar.z);
    if (tExit <= max(tEnter, 0.0)) { finalColor = background; return; }
    tEnter = max(tEnter, 0.0);

    // Which face we entered through decides the first shade.
    int face = (tNear.x >= tNear.y && tNear.x >= tNear.z) ? 0 : (tNear.y >= tNear.z ? 1 : 2);
    if (tEnter == 0.0) face = 1;   // camera inside the box: no face

    vec3 p = camPos + dir * (tEnter + 1e-4);
    ivec3 voxel = ivec3(floor(p));
    ivec3 stepDir = ivec3(sign(dir));
    vec3 nextBoundary = vec3(voxel) + vec3(greaterThan(dir, vec3(0.0)));
    vec3 tMax = (nextBoundary - camPos) * invDir;
    vec3 tDelta = abs(invDir);

    ivec3 lo = ivec3(floor(clipMin));
    ivec3 hi = ivec3(ceil(clipMax)) - ivec3(1);
    vec4 accum = vec4(0.0);

    for (int i = 0; i < maxSteps; ++i) {
        if (any(lessThan(voxel, lo)) || any(greaterThan(voxel, hi))) break;
        uint s = texelFetch(stateTex, voxel, 0).r;
        vec4 c = texelFetch(paletteTex, ivec2(int(s), 0), 0);
        float a = clamp(c.a * opacity, 0.0, 1.0);
        if (a > 0.0) {
            vec3 shaded = c.rgb * FACE_SHADE[face];
            accum.rgb += (1.0 - accum.a) * a * shaded;
            accum.a   += (1.0 - accum.a) * a;
            if (accum.a > 0.985) break;
        }
        // Advance to the next voxel along the nearest boundary.
        if (tMax.x < tMax.y && tMax.x < tMax.z)      { voxel.x += stepDir.x; tMax.x += tDelta.x; face = 0; }
        else if (tMax.y < tMax.z)                    { voxel.y += stepDir.y; tMax.y += tDelta.y; face = 1; }
        else                                         { voxel.z += stepDir.z; tMax.z += tDelta.z; face = 2; }
    }
    finalColor = vec4(accum.rgb + (1.0 - accum.a) * background.rgb, 1.0);
}
