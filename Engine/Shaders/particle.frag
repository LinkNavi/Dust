#version 450

// Descriptor Set 0, Binding 0 (or set 1 if push constants / frame data are set 0)
layout(set = 0, binding = 0) uniform sampler2D particleTexture;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec4 fragColor;

void main() {
    // Sample texture RGBA using the procedural UVs passed from vertex shader
    vec4 texColor = texture(particleTexture, inUV);

    // Tint texture with particle color (allows fading out via alpha)
    vec4 finalColor = inColor * texColor;

    // Discard nearly transparent pixels to save bandwidth / avoid blending artifacts
    if (finalColor.a <= 0.001) {
        discard;
    }

    fragColor = finalColor;
}
