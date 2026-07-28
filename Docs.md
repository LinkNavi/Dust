# Dust Engine

Dust is a Vulkan-based game engine library for C++. Link against it and get a window, renderer, ECS, and mesh API — no extra files needed.

---

## Setup

```cpp
#include "DustEngine.hpp"
#include "Log.hpp"

int main() {
    Dust::set_log_level(1); // 0=none 1=info 2=verbose 3=debug

    Dust::DustEngine e;
    e.init("My Game");

    auto& w = e.windows.create({
        .name      = "main",
        .title     = "My Game",
        .width     = 1280,
        .height    = 720,
        .resizable = true,
        .vsync     = true,
    }, e.vulkan);

    w.setClearColor(0.05f, 0.05f, 0.05f);
    w.renderer.init(e.vulkan, w.swapchain);

    e.run([&](float dt) {
        if (!w.renderer.beginFrame(e.vulkan, w)) return;
        w.renderer.beginRendering(e.vulkan, w);
        // draw calls go here
        w.renderer.endRendering();
        w.renderer.endFrame(e.vulkan, w);
    });

    e.shutdown();
}
```

---

## Multiple Windows

```cpp
auto& main = e.windows.create({ .name="main", .title="Game"   }, e.vulkan);
auto& hud  = e.windows.create({ .name="hud",  .title="HUD"    }, e.vulkan);

main.renderer.init(e.vulkan, main.swapchain);
hud.renderer.init(e.vulkan, hud.swapchain);

e.run([&](float dt) {
    for (auto& w : e.windows.windows) {
        if (!w.renderer.beginFrame(e.vulkan, w)) continue;
        w.renderer.beginRendering(e.vulkan, w);
        w.renderer.endRendering();
        w.renderer.endFrame(e.vulkan, w);
    }
});
```

---

## Logging

Logs anything — primitives, strings, or any type with `operator<<`.

```cpp
Dust::set_log_level(1); // must be set before init to see init logs

Dust::log("hello ", 42, " world");           // [info]
Dust::log_verbose("detail: ", someFloat);    // [verbose]
Dust::log_debug("frame: ", frameCount);      // [debug]
Dust::log_err("something broke: ", code);    // [error] always prints
```

For your own structs:
```cpp
std::ostream& operator<<(std::ostream& os, const Vec3& v) {
    return os << "Vec3(" << v.x << ", " << v.y << ", " << v.z << ")";
}
Dust::log("pos: ", myVec3); // works automatically
```

---

## Device Tier

Detected automatically at init — use it to gate optional features.

```cpp
auto& tier = e.vulkan.tier;

Dust::log("Vulkan ", tier.vulkanMajor, ".", tier.vulkanMinor);
Dust::log("Dynamic Rendering: ", tier.dynamicRendering);
Dust::log("Mesh Shaders: ",      tier.meshShaders);
Dust::log("Ray Tracing: ",       tier.raytracing);

if (e.vulkan.supportsVersion(1, 3)) {
    // use 1.3 features
}
```

---

## Entities

```cpp
// createEntity(name, parent) — defaults to root if no parent
Dust::Entity* player = e.createEntity("player");
Dust::Entity* sword  = e.createEntity("sword", player);

// Hierarchy
player->addChild(sword);
sword->setParent(nullptr); // detach

// Components
struct Health { int hp; };
player->add<Health>({ 100 });
Health* h = player->get<Health>();
player->remove<Health>();
```

---

## ECS

Direct access to the registry for systems:

```cpp
struct Transform { float x, y, z; };
struct Velocity  { float dx, dy, dz; };

auto entity = e.ecs.create();
e.ecs.add<Transform>(entity, {0, 0, 0});
e.ecs.add<Velocity>(entity,  {1, 0, 0});

// iterate all entities with both components
e.ecs.view<Transform, Velocity>([](ecs::Entity e, Transform& t, Velocity& v) {
    t.x += v.dx;
    t.y += v.dy;
    t.z += v.dz;
});
```

---

## Mesh API

```cpp
#include "Core/Rendering/Mesh.hpp"

// Built-in helpers
Dust::Mesh tri  = Dust::Mesh::makeTriangle();
Dust::Mesh cube = Dust::Mesh::makeCube();

// Upload to GPU before drawing
tri.upload(e.vulkan);

// Build from scratch
Dust::Mesh m;
auto a = m.addVert({{ 0.0f,  0.5f, 0.0f}, {0,0,1}, {0.5f,0.0f}, {1,0,0,1}});
auto b = m.addVert({{-0.5f, -0.5f, 0.0f}, {0,0,1}, {0.0f,1.0f}, {0,1,0,1}});
auto c = m.addVert({{ 0.5f, -0.5f, 0.0f}, {0,0,1}, {1.0f,1.0f}, {0,0,1,1}});
m.addFace(a, b, c);
m.recalcNormals();
m.upload(e.vulkan);

// Modify at runtime
Dust::Vertex* v = m.getVert(a);
v->position[1] += 0.1f;
m.upload(e.vulkan); // re-upload after changes

// Draw
w.renderer.draw(w.renderer.cmd(), m, w.renderer.defaultPipeline, w.renderer.defaultLayout);

// Cleanup
m.destroy(e.vulkan);
```

---

## Shaders

Default unlit shader is embedded in the engine — no files needed.

For custom shaders:
```cpp
auto shader = Dust::ShaderModule::load(
    e.vulkan.device,
    "shaders/my.vert.spv",
    "shaders/my.frag.spv"
);

Dust::PipelineBuilder pb;
pb.shader      = shader;
pb.cullMode    = VK_CULL_MODE_NONE;
pb.blendEnable = true;
pb.pushConstant = { VK_SHADER_STAGE_VERTEX_BIT, 64 };

VkPipelineLayout myLayout;
VkPipeline       myPipeline;
pb.build(e.vulkan, w.swapchain, myLayout, myPipeline);
shader.destroy(e.vulkan.device);
```

Shader inputs (must match):
```glsl
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=3) in vec4 inColor;

layout(push_constant) uniform Push {
    mat4 transform;
} push;
```

---

## Project Structure

```
YourProject/
├── Engine/
│   ├── include/
│   │   ├── DustEngine.hpp
│   │   ├── Log.hpp
│   │   └── Core/
│   │       ├── Window.hpp
│   │       ├── Systems/
│   │       │   └── Entity.hpp
│   │       └── Rendering/
│   │           ├── VulkanContext.hpp
│   │           ├── Swapchain.hpp
│   │           ├── Renderer.hpp
│   │           ├── FrameData.hpp
│   │           ├── Mesh.hpp
│   │           ├── ShaderModule.hpp
│   │           ├── PipelineBuilder.hpp
│   │           └── DefaultShaders.hpp  ← auto-generated
│   ├── src/...
│   └── Shaders/
│       ├── default.vert
│       └── default.frag
├── ECS/
│   ├── include/DustECS.hpp
│   └── src/DustECS.cpp
└── Runtime/
    └── src/main.cpp
```
