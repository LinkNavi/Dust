#version 450

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 camRight;  // xyz: world-space right
    vec4 camUp;     // xyz: world-space up
    vec4 fogColor;  // rgb = fog color, a > 0.5 = enabled — matches default.frag's Push block
    vec4 fogParams; // x = start distance, y = end distance, zw unused
} push;

// Binding 0 — unit quad (Mesh::makeQuad), per-vertex
layout(location = 0) in vec3 inPosition; // xy in [0,1], z=0

// Binding 1 — per-particle (Dust::Particle, std430 layout)
layout(location = 4) in vec3  instPos;   // offset  0
layout(location = 5) in float instLife;  // offset 12
layout(location = 6) in vec3  instVel;   // offset 16  (unused in vert, must declare for stride)
layout(location = 7) in float instSize;  // offset 28
// color at offset 32 is passed via location 8 in the billboard shader;
// particle.frag reads location 0 (outColor) set below.

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;
// Same trick as default.vert — clip.w is view-space distance for free.
layout(location = 2) out float outViewDist;

void main() {
    float alive = step(0.0001, instLife);

    vec2 offset = inPosition.xy - vec2(0.5); // center quad

    vec3 worldPos = instPos
        + (push.camRight.xyz * offset.x + push.camUp.xyz * offset.y)
        * instSize * alive;

    gl_Position = push.viewProj * vec4(worldPos, 1.0);

    // Fade by life (life stored raw — shader fades over last second)
    float alpha = clamp(instLife, 0.0, 1.0) * alive;
    outColor = vec4(1.0, 1.0, 1.0, alpha); // tinted white; texture provides color
    outUV    = inPosition.xy;
    outViewDist = gl_Position.w;
}
