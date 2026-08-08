#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location=0) in vec4 inColor;
layout(location=1) in vec2 inUV;

layout(location=0) out vec4 fragColor;

void main() {
    vec4 c = texture(tex, inUV) * inColor;
    if (c.a < 0.01) discard;
    fragColor = c;
}
