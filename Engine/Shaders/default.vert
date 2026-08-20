// default.vert — strip out the scene UBO until SceneUBO is implemented
#version 450

layout(push_constant) uniform Push {
    mat4 transform;
    vec4 baseColorFactor; // unused here — read by default.frag, shared block so offsets line up
} push;

layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=3) in vec4 inColor;

layout(location=0) out vec4 outColor;
layout(location=1) out vec2 outUV;
// Cheap fog distance: for a standard perspective matrix, clip.w equals the
// view-space -z (camera distance along view axis) — free to derive, no
// separate model matrix or camera position needed in the push constant.
layout(location=2) out float outViewDist;

void main() {
    gl_Position = push.transform * vec4(inPosition, 1.0);
    outColor    = inColor;
    outUV       = inUV;
    outViewDist = gl_Position.w;
}
