// skybox.frag — the default skybox: sample a cubemap by view direction.
// This is the fragment stage swapped out for a procedural sky — a custom
// shader pairs with skybox.vert and keeps this exact in/out + push-constant
// interface (see Engine/Shaders/skybox_gradient.frag for a working example,
// and DustEngine::loadSkyboxShader).
#version 450

layout(set = 0, binding = 0) uniform samplerCube skyboxTex;

layout(push_constant) uniform Push {
    mat4 viewRotProj;
    vec4 sunDir;
    vec4 params;
    vec4 tint;
} push;

layout(location = 0) in vec3 inDir;

layout(location = 0) out vec4 fragColor;

void main() {
    vec3 c = texture(skyboxTex, normalize(inDir)).rgb * push.tint.rgb;
    fragColor = vec4(c, 1.0);
}
