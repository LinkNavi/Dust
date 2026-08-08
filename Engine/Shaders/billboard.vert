#version 450

layout(push_constant) uniform Push {
    mat4  viewProj;
    vec4  camRight;   // xyz: world-space right
    vec4  camUp;      // xyz: world-space up
    vec4  position;   // xyz: world position, w: size
    vec4  color;      // rgba tint
} push;

layout(location = 0) in vec3 inPosition; // unit quad xy in [0,1]

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;

void main() {
    vec2 offset = inPosition.xy - vec2(0.5);
    vec3 worldPos = push.position.xyz
        + (push.camRight.xyz * offset.x + push.camUp.xyz * offset.y) * push.position.w;

    gl_Position = push.viewProj * vec4(worldPos, 1.0);
    outColor = push.color;
    outUV    = inPosition.xy;
}
