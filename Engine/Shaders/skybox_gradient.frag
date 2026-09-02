// skybox_gradient.frag — a procedural sky, no cubemap images at all. Pairs
// with the stock skybox.vert (same in/out + push block as skybox.frag), so
// DustEngine::loadSkyboxShader() can drop it in wherever a Cubemap would go.
//
// Uses the shared push constants: params.y = horizon softness, params.z/.w
// unused here, sunDir for a cheap sun disc + glow, tint as an overall
// multiply. A game passes its own values through DustEngine::drawSkybox().
#version 450

layout(push_constant) uniform Push {
    mat4 viewRotProj;
    vec4 sunDir;   // xyz = direction the sun's light travels
    vec4 params;   // x = time, y = horizon softness (default ~0.1), zw free
    vec4 tint;     // rgb multiply
} push;

layout(location = 0) in vec3 inDir;

layout(location = 0) out vec4 fragColor;

void main() {
    vec3 dir = normalize(inDir);

    // Vertical gradient: zenith blue -> horizon haze -> ground.
    vec3 zenith  = vec3(0.18, 0.42, 0.78);
    vec3 horizon = vec3(0.75, 0.85, 0.95);
    vec3 ground  = vec3(0.34, 0.32, 0.30);

    float soft = max(push.params.y, 0.02);
    float up   = smoothstep(-soft, soft, dir.y);          // 0 below horizon, 1 above
    float high = pow(clamp(dir.y, 0.0, 1.0), 0.5);

    vec3 sky = mix(horizon, zenith, high);
    vec3 col = mix(ground, sky, up);

    // Sun: sunDir is the travel direction, so the disc is toward -sunDir.
    vec3  toSun  = normalize(-push.sunDir.xyz);
    float d      = max(dot(dir, toSun), 0.0);
    col += vec3(1.0, 0.95, 0.85) * pow(d, 900.0) * 6.0;   // disc
    col += vec3(1.0, 0.85, 0.60) * pow(d, 6.0)   * 0.25;  // glow

    fragColor = vec4(col * push.tint.rgb, 1.0);
}
