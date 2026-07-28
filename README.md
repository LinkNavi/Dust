# Dust Engine

Dust is a fast, lightweight game engine library for C++ built on Vulkan. Think raylib, but lower-level and built for real games. You get a simple API without sacrificing performance — no hidden overhead, no engine lock-in, just a library you link against and control entirely.

A GUI editor (Unity-style) is planned for a future release.

---

## Why Dust?

- **Fast by default** — designed to run well even on lower-end hardware via tiered Vulkan feature support
- **Simple C++ API** — include it, link it, ship it
- **Powerful mesh tools** — import models or generate geometry from scratch; read and write verts and faces directly at runtime
- **Your build system** — Dust doesn't dictate how you build your project

---

## Getting Started

Install [Zora](link) and add Dust to your project:

```bash
zora add dust
```

Then link against the installed package using your build system of choice.

---

## Features (Planned)

- Vulkan renderer (1.0 baseline, optional 1.3 features)
- ECS (Entity Component System)
- Mesh API — import, generate, and manipulate geometry at runtime
- Audio
- Networking via ENet
- Hylian scripting integration
- GUI editor
