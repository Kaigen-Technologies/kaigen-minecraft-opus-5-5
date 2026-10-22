# WebCraft performance history

Reference notes on how the port's performance changed from the first working port to the optimized state, and what
was done. Sources: `PERF_PLAN.md` and `tools/perf/baseline/` at commit `2632140` (on `bk-branch`), and the result
files in `out/perf/`.

All numbers: RTX 3060 laptop, seed 1337, High preset, browser (wasm + WebGPU) unless marked native, measured
2026-09-23. "First port" is the first working port (`3cd2019`). "Now" is the end of the optimization work on
2026-09-23; later changes are not measured. In the browser the frame rate tops out around 220–345 fps whatever the
GPU does, so GPU time is the better measure of rendering cost there.

## Numbers

### Load and streaming

| Metric | First port | Now | JS original |
|---|---|---|---|
| Load to first frame, cold | 1,473 ms | 636–687 ms | 4,688 ms |
| Load to playable, cold | 2,423 ms | ~1,254 ms | 5,000 ms |
| Load to playable, warm (over 127.0.0.1) | 1,165 ms | 507 ms | — |
| Teleport re-stream, 720p | 285 ms | 139–153 ms | 1,212 ms |
| Teleport re-stream, 1080p | 345 ms | 151–169 ms | 2,089 ms |

### GPU frame time and fps

View A is the spawn point, view C a high overlook.

| Metric | First port | Now | JS original |
|---|---|---|---|
| GPU frame, view A, 720p | 3.63 ms | 2.03–2.15 ms | 5.26 ms |
| GPU frame, view A, 1080p | 5.59 ms | 3.09–3.28 ms | 9.61 ms |
| fps, view A / C, 720p | 243 / 259 | 312 / 323 | 189 / 221 |
| fps, view A / C, 1080p | 171 / 182 | 209–211 / 200–216 | 103 / 118 |

### Native Windows

| Metric | First port | Now |
|---|---|---|
| fps, view A / C, 720p | 305 / 361 | — |
| fps, view A / C, 1080p full screen, uncapped | — | 335 / 349 (median of 3) |
| Load to playable | 1,524 ms | — |

First-port and JS-original numbers are single runs; "now" ranges are the spread of the runs found.

## Optimizations

### Startup and streaming

| Item | What was done | Result (NVIDIA, 720p, browser) |
|---|---|---|
| S3 Streaming rounds | World jobs (generation, lighting, meshing) run in time-budgeted rounds sized by their measured cost; the area around the player gets priority. Later: no rounds at all when nothing is scheduled. | Teleport 285 → 188 ms; warm playable 268 → 170 ms after the first frame; integrated GPU teleport 926 → 273 ms |
| S1 Cloud noise on the GPU | The 3D cloud noise and weather textures were generated on the CPU at startup (534 ms). Now three GPU compute bakes plus mipmaps, with pipelines compiled asynchronously (engine changes E1, E2). | Warm first frame 920 → 408 ms; cold playable 2491 → 1999 ms |
| S2 Parallel icons | Inventory icons drawn on every core instead of only the main thread. | Warm first frame 408 → 368 ms |
| S4 Boot overlap | In the browser, the GPU device request, the module compile and the audio setup overlap instead of running one after another. | Cold worker start ~265 → 95 ms; cold playable 1899 → 1822 ms |
| S5 Pipelines first | Shader pipelines created at the very start of init and handed to the browser immediately (engine `gpu_flush`); effects switch on as their shaders finish compiling. | Cold playable 1569 → 1254 ms; warm playable 539 → 507 ms |

### Rendering

| Item | What was done | Result |
|---|---|---|
| R1 Cached shadow cascades | Cascades centred on the camera and anchored in the world, so turning never invalidates them. The two far cascades stay cached until the camera moves, a nearby mesh changes or the sun steps; the two near ones re-render every frame (animated foliage). | NVIDIA shadow 1.67 → 0.56 ms |
| R1 Shadow face culling | The shadow pass drops cube faces that face the light. | NVIDIA shadow 0.53 → 0.42 ms; 1080p frame 5.59 → 4.33 ms |
| R2 Half-resolution light shafts | The volumetric shaft march runs at half resolution, cycling which pixel of each 2×2 block it marches, with a depth-aware upsample. | NVIDIA 1080p atmos 0.86 → 0.40 ms |
| R2 Clouds, one texel in four | Each frame marches one texel of every 2×2 block; the temporal pass reprojects the other three. | Clouds 0.45 → 0.20 ms |
| R2 Half-resolution SSAO | Ambient occlusion at half resolution with a depth- and normal-aware upsample. | NVIDIA 1080p SSAO 0.42 → 0.14 ms |
| R4a 20-byte quad records | Each quad is one 5-word record instead of 4 vertices; the shader rebuilds the corners. | Mesh memory and uploads 64 → 20 bytes per quad; teleport 173 → 149 ms |
| R5 Face buckets and GPU expansion | Meshes sorted into buckets by face direction; the CPU culls whole buckets per pass (back faces, light-facing faces for shadows); a compute pass expands the result and each pool draws in one batch. | G-buffer 0.37 → 0.28 ms |
| R5b Sky-view cache | Sky lookup tables re-render only when the sun moves ~0.03° or the camera moves 1 m. | Less CPU per frame and less sky GPU time |
| R4c + E3 Shared depth | The scene reuses the g-buffer's depth instead of copying it every frame (engine change E3); copies only while water is on screen. | 720p view A 291 → 321 fps; 1080p 209 → 229 fps |
| R3 TAAU | With TAA on, a render scale below 1 upscales inside the TAA pass. High stays native; Low and Medium upscale. | 1080p at 0.67 scale: GPU 3.09 → 2.37 ms; no fps change on desktop |

### After the plan

- Uncapped native frame rate and an on-screen stats panel (fps, frame, GPU, CPU).
- Sky fix: changing a setting or the window size cleared the sky lookup tables for good. This was also the cause of
  the "tint shift at render scales below 1" the plan logged as untraced.
- Tried and not kept:
  - DLSS was built and measured, then reverted: slower than native on this GPU.
  - The packed g-buffer (R4b) was judged not worth its precision cost.
  - Moving culling fully to the GPU stays open: it needs an engine change.
