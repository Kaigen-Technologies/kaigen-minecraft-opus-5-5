# WebCraft

This demo was written by AI using the Kaigen Engine, a high performance, cross-platform 3D engine written in C.

[Register for the closed beta.](https://kaigen3d.com/)

![WebCraft](preview.png)

A Minecraft-style voxel sandbox: an endless seeded world streamed around the player, block building, a day/night
cycle with a physically based sky, volumetric clouds, weather and water. It runs natively on Windows (Direct3D 12)
and in the browser (WebAssembly + WebGPU) from the same C source.

## The prompt

The first version was created from this prompt:

> /goal Create a playable version of Minecraft that runs in my browser and on Windows. Use gpu.h directly. Add advanced shaders that make it look as real and beautiful as possible.

## Running it

The project needs the Kaigen Engine, which is in closed beta. `hz` is a link to your engine checkout at the project
root (it is not part of this repository).

```
hz\hzbuild run                   # native debug build, then run
hz\hzbuild --release run         # native optimized build
hz\hzbuild wasm --release run    # WebAssembly build, served at http://localhost:3000
```

The first build compiles the shaders for every backend.

## Controls

| Key | Action |
|---|---|
| W A S D | Move (double-tap W or Ctrl to sprint) |
| Space | Jump (double-tap or F to fly) |
| Shift | Sneak / fly down |
| Left / right click | Break / place block |
| Middle click | Pick block |
| 1–9 / wheel | Select hotbar slot |
| E | Block inventory |
| T (hold) | Fast-forward time, Shift+T to rewind |
| F1 / F2 / F3 | Hide HUD / screenshot / debug info |
| Esc | Menu and settings |

## Project files

```
src/webcraft/     the game (C)
shaders/          the game's shaders (one source, compiled for every backend)
hzproject.hzt     project config
preview.png       project thumbnail
```

| File | What it does |
|---|---|
| `wc_game.c` | app entry points, game loop, modes, input, scripted commands |
| `wc_world.c` | chunk storage, streaming, job scheduling across threads |
| `wc_gen.c` | terrain generation: noise, biomes, caves, trees, water |
| `wc_light.c` | sky and block light propagation |
| `wc_mesh.c`, `wc_mesh_store.c` | chunk meshing and GPU mesh pools |
| `wc_render.c` | the renderer: all passes, targets and pipelines |
| `wc_player.c` | first-person physics and flight |
| `wc_entities.c` | particles, fireflies, the held block |
| `wc_textures.c`, `wc_icons.c` | procedural block textures and inventory icons |
| `wc_audio.c` | procedural sound effects and rain |
| `wc_ui.c` | menus, settings, HUD, inventory, stats panel |
| `wc_save.c` | settings and world saves |
| `wc_storage.c` | the player's block edits, kept per chunk |

## Architecture

- **Threads.** The engine runs the app on every core in lock step. World generation, lighting and meshing are jobs
  that all threads take from a shared list in time-budgeted rounds, so streaming shares every core with the frame.
- **Rendering.** A deferred renderer written directly against the engine's GPU layer: GPU culling of chunk quads,
  a g-buffer, cascaded shadow maps (distant cascades cached between frames), SSAO, deferred lighting, refractive
  water with screen-space reflections, volumetric clouds and light shafts, a precomputed-LUT atmosphere,
  temporal anti-aliasing with upscaling, bloom and auto exposure.
- **Content.** Textures, icons and sounds are generated procedurally at startup; there are no art assets.
- **Saves.** Settings, the player and every block edit are saved as engine blob assets in the user's app data
  folder (the browser's private file system on the web).
