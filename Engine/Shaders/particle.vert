#version 450

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 camRight; // World-space right vector (xyz)
    vec4 camUp; // World-space up vector (xyz)
} push;

// Binding 0 — Standard mesh quad positions in [0, 1] range
layout(location = 0) in vec3 inPosition;

// Binding 1 — Per-particle instance data (Dust::Particle, GPU/CPU written)
layout(location = 4) in vec3 instPosition;
layout(location = 5) in float instSize;
layout(location = 6) in vec4 instColor;
layout(location = 7) in float instLife;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;

void main() {
    // Collapse dead particles to zero size without pipeline branching
    float alive = step(0.0001, instLife);

    // Center quad from [0, 1] down to [-0.5, 0.5]
    vec2 offset = inPosition.xy - vec2(0.5);

    // Orient quad facing camera using world space camera vectors
    vec3 worldPos = instPosition +
            (push.camRight.xyz * offset.x + push.camUp.xyz * offset.y) * instSize * alive;

    gl_Position = push.viewProj * vec4(worldPos, 1.0);

    outColor = instColor;
    outUV = inPosition.xy; // Derive UV directly from position [0, 1]
}
