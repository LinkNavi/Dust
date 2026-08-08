# Contributing to Dust

## Repo Layout

```
Dust/
├── Engine/          # The engine library — link against this
├── ECS/             # Entity/component registry (dependency of Engine)
├── AssetManager/    # Pack file loader (dependency of Engine)
├── DustPacker/      # Offline tool: compiles assets → .pack binaries
├── Runtime/         # Thin executable wrapper (entry point for shipped games)
├── HERO/            # Internal test harness / sandbox project
├── HEROBench/       # Performance benchmarks
└── Models/          # Test assets (not shipped)
```

The workspace is managed by **Zora** (`Zora.toml` at root). Each subdirectory is its own Zora project with its own `Zora.toml`. See `HowToBuild.txt` for build steps.

---

## Engine Layout

```
Engine/
├── include/
│   ├── DustEngine.hpp          # Single public header — users include only this
│   ├── Log.hpp
│   └── Core/
│       ├── Rendering/
│       │   ├── VulkanContext.hpp   # Device, instance, queues
│       │   ├── Swapchain.hpp       # Per-window swap chain
│       │   ├── Renderer.hpp        # Frame loop, pipelines, draw calls
│       │   ├── Mesh.hpp            # CPU/GPU geometry buffer
│       │   ├── Model.hpp           # Multi-submesh scene object
│       │   ├── Texture.hpp         # Image + sampler
│       │   ├── Camera.hpp          # View/projection math
│       │   ├── ParticleSystem.hpp  # CPU sim state + GPU instance buffer
│       │   ├── PipelineBuilder.hpp # Vulkan pipeline construction helpers
│       │   ├── ShaderModule.hpp
│       │   ├── FrameData.hpp       # Per-frame-in-flight sync objects
│       │   └── DefaultShaders.hpp  # Embedded SPIR-V for the default pipeline
│       ├── Systems/
│       │   ├── Entity.hpp          # ECS entity handle + hierarchy helpers
│       │   └── Transform.hpp       # Position/rotation/scale component
│       └── UI/
│           ├── Widget.hpp          # Immediate-mode UI tree node
│           ├── Font.hpp            # MSDF font atlas
│           ├── Anchor.hpp          # Layout anchor math
│           ├── Units.hpp           # vw/vh/px unit types
│           ├── Color.hpp
│           ├── UIShaders.hpp       # Embedded SPIR-V for the UI pipeline
│           └── TextShaders.hpp     # Embedded SPIR-V for the text pipeline
└── src/                            # Mirrors include/ structure
    ├── DustEngine.cpp              # DustEngine convenience API impl
    └── Core/
        ├── Rendering/
        │   ├── VulkanContext.cpp
        │   ├── Swapchain.cpp
        │   ├── Renderer.cpp
        │   ├── Mesh.cpp
        │   ├── Model.cpp
        │   ├── Texture.cpp
        │   ├── Camera.cpp
        │   ├── ParticleSystem.cpp
        │   ├── PipelineBuilder.cpp
        │   └── ShaderModule.cpp
        ├── Systems/
        │   └── Transform.cpp
        ├── UI/
        │   ├── Widget.cpp
        │   └── Font.cpp
        └── Window.cpp
```

Shaders live in `Engine/Shaders/` as GLSL source and are compiled to SPIR-V by `Engine/Scripts/BuildShaders.sh`, which runs automatically before every build.

---

## Rendering Pipeline Overview

| Pipeline | Vert shader | Frag shader | What it draws |
|---|---|---|---|
| `defaultPipeline` | `default.vert` | `default.frag` | Meshes / Models |
| `particlePipeline` | `particle.vert` | `particle.frag` | Billboard particle quads |
| `uiPipeline` | `ui.vert` | `ui.frag` | DustUI rounded rects |
| `textPipeline` | `text.vert` | `text.frag` | MSDF glyph quads |

---

## Particle System — Current State & Known Issues

The particle system is partially implemented. Before contributing to it, read this section.

### What exists

- `ParticleSystem` (CPU path): allocates a persistently mapped instance buffer, simulates particles on the CPU each frame, and `memcpy`s live instances to the GPU.
- `particle.vert` / `particle.frag`: billboard quad shader. Expects a mesh quad in binding 0 (locations 0–3) and per-instance data in binding 1 (locations 4–7).
- `particles.comp`: GPU compute shader that simulates the same particle physics entirely on the GPU using an SSBO. This is the **intended** long-term path.
- `ParticlePushConstants` + `particleLayout` / `particlePipeline` declared in `Renderer.hpp`.
- `drawParticles()` declared in `Renderer.hpp`.

### What is missing / broken

1. **Location mismatch**: `ParticleSystem::getAttributeDescriptions()` emits locations 0–3, but `particle.vert` reads instance data from locations 4–7. These must match.

2. **Dual simulation paths**: Both the CPU `update()` and the compute shader (`particles.comp`) simulate particles. Pick one. The compute path is preferred — it removes the per-frame `memcpy` and scales to 100k+ particles without CPU cost.

3. **`drawParticles()` is not implemented**: The function is declared but has no body in `Renderer.cpp`. The `particlePipeline` is also never built in `Renderer::init()`.

4. **No user-facing API**: `DustEngine` has no `drawParticles()` wrapper, so the system can't be called from user code yet.

### Intended design (compute path)

- The instance buffer is created as `STORAGE_BUFFER | VERTEX_BUFFER` so the compute shader can write to it and the vertex shader can read from it in the same frame (with a pipeline barrier between the compute and graphics passes).
- `emit()` writes new particle data into a small CPU-side spawn buffer.
- Before the graphics pass, dispatch `particles.comp` to update all live particles; a barrier ensures the writes are visible to the vertex shader.
- `drawParticles()` binds the instance buffer and issues an instanced draw — no `memcpy` involved.

---

## Adding a New Subsystem

1. Headers go in `Engine/include/Core/<Category>/`.
2. Source goes in `Engine/src/Core/<Category>/`.
3. If it needs a pipeline: add GLSL to `Engine/Shaders/`, add an embedded SPIR-V header under `include/Core/Rendering/`, build the pipeline in `Renderer::init()`, and add the draw call to `Renderer`.
4. Expose a user-facing wrapper on `DustEngine` in `DustEngine.hpp` / `DustEngine.cpp`.
5. Test it in HERO before opening a PR.

---

## Code Style

- C++17. No exceptions, no RTTI.
- `VK_NULL_HANDLE` checks before any Vulkan destroy call.
- `vmaDestroyBuffer` / `vmaDestroyImage` instead of raw `vkDestroy*` for VMA-allocated resources.
- Prefer `glm` for all math. No raw arrays for vectors/matrices.
- `fprintf(stderr, "dust: ...")` for errors — no exceptions thrown across the public API.
