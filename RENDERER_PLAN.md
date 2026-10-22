# Renderer evolution plan

What `renderer.h` and the layers under it need so that a game with custom rendering (WebCraft was the test case)
can be built faster and more correctly on top of the engine, instead of writing a renderer from scratch on `gpu.h`.
Every item is judged by one lens: the most performant, most native, most robust implementation.

Status: plan agreed at a high level (2026-09-25). Each item gets its own short design (API, per-platform behavior,
test plan) for owner sign-off before implementation. Revised 2026-09-25 against engine `3b72c5be3` (the WebCraft GPU
follow-up, `docs/webcraft-gpu-followup-plan.md`): code locations below refer to that commit.

## Principles

1. **`gpu.h` is for control.** It is a thin, explicit layer: it reports what the device can do, and each call does
   exactly what it says. It never emulates a missing feature behind a call that looks native.
2. **Higher layers are for ease of use and free performance.** The renderer and the reusable modules read the
   capabilities, pick the best strategy, and own every fallback.
3. **Bindless by default** wherever the platform supports it; WebGPU keeps named per-draw bindings.
4. **Composition.** The renderer is assembled from public building blocks, with no private shortcuts, so the same
   blocks can build a fully custom renderer. Only extract what is complex and reusable enough to earn its own API.
5. **Zero-init is the sensible default** for every new desc field; opting out is explicit.

## Gate for every item

- `../hzbuild release run test --performance --filter renderer` (from `hz_tests`) passes before and after each item,
  with no test slower than its baseline. A reference run is taken on an idle GPU before item 0 starts.
- The baselines a passing run rewrites are reverted unless the item means to move them (an improvement, recorded in
  the item's notes).

## Items, in order

| # | Item | Layer | Size |
|---|---|---|---|
| 0 | Per-backend perf baselines | tests | small |
| 1 | Render targets start in a defined state | gpu | small |
| 2 | Draw sources as full citizens: depth pre-pass, hook points, readiness | renderer | small |
| 3 | Typed bindless (2D array, 3D) and pixel-art sampling | gpu + shader codegen + shader library | medium |
| 4 | Indirect drawing capabilities and multi-draw-indirect | gpu | medium |
| 5 | GPU-driven drawing strategy and the GPU culling module | modules + renderer | medium |
| 6 | Extract the geometry pipeline and CPU culling | modules + gpu | medium–large |
| 7 | Final blit as a material | renderer + shaders | medium |
| 8 | Built-in modules, one at a time | modules | medium each |
| 8b | WebGPU honors `color_write_mask` | gpu | small |
| 9 | Forward, forward+ and deferred | renderer + material system | large, discussed first |

### 0. Per-backend perf baselines (tests)

- Problem: perf baselines are keyed by platform and machine only (`perf_reference/windows/<machine>`), so a Vulkan
  run compares against the D3D12 number and a Vulkan regression stays hidden until it is slower than D3D12.
- Change: one baseline directory per backend. Every existing Vulkan comparison moves, so it is recaptured once.
- Why first: items 4, 5 and 9 are performance work measured on several backends.

### 1. Render targets start in a defined state (gpu)

- Problem: `gpu_get_render_target_attachment` debug-asserts when the target was never drawn to, on D3D12
  (`gpu_d3d12.c:8186`), Metal (`gpu_metal.c:7045`) and WebGPU (`gpu_webgpu.c:4432`). Every app has to prime its
  targets by hand; WebCraft's sky bug came from that code.
- Change: the GPU layer clears every attachment of a render target, color and depth, once on create and on resize,
  then the asserts go. Depth is included because a pass can now use another target's depth (`GpuPassDesc.depth_target`),
  so an unwritten depth can be read too. WebGPU already zero-initializes new textures, so there only the assert goes.
  Opt-out field for targets the caller always fully overwrites before reading.
- Cost: one clear per target per create/resize; nothing per frame.

### 2. Draw sources as full citizens (renderer)

- Draw sources can record into the depth pre-pass. Today the pre-pass only draws the renderer's own meshes and
  forces `depth_write = false` on injected opaque pipelines (`renderer.c:3988-3991`), so custom geometry is missing
  from depth, GTAO and decals.
- More named hook points: after the depth pre-pass, after shadows, before post. Compute already runs in `prepare`,
  `BEFORE_TRANSPARENT` and `AFTER_RESOLVE`; the gap is these other points. Each hook documents what renderer
  resources are readable or writable there.
- Readiness: the renderer skips a draw source's work until its pipelines are compiled, instead of every source
  hand-writing that check (`renderer.c` checks no pipeline state today). The query is `gpu_pipeline_state` /
  `gpu_compute_pipeline_state` (PENDING, READY, FAILED). PENDING skips this frame; FAILED disables the source and
  logs once, so a failed compile never stalls it forever. Async compile now runs on every backend (`compile_async`
  on D3D12, Vulkan and Metal as well as wasm), so pending pipelines are the normal case everywhere.
- Not needed: a frame graph. The GPU layer already tracks resource state and transitions, and the renderer owns
  the frame structure.

### 3. Typed bindless and pixel-art sampling (gpu + shader codegen + shader library)

- Today native bindless has 2D and cube ranges only. A material "texture array" on native is one bindless 2D slot
  per layer (512-slot cap on D3D12), 3D textures have no bindless range, and in a bindless pipeline D3D12 keeps only
  `GPU_TEX_BIND_SINGLE` textures as extra bindings, silently dropping `GPU_TEX_BIND_ARRAY` and `GPU_TEX_BIND_3D`
  (`gpu_d3d12.c:5109`). Vulkan and Metal behavior for the same case is unverified.
- Change: typed bindless ranges for 2D arrays and 3D textures on every backend that has bindless (D3D12 register
  spaces, Metal argument-buffer regions, Vulkan descriptor arrays), keyed by `GpuTextureKind`. Material fields
  `@texture2DArray` / `@texture3D` sampled by index; WebGPU keeps named bindings (it already uses a real
  `texture2DArray`).
- Pixel-art sampling is a shader-library function, not a sampler type. A point-magnification, anisotropic-
  minification sampler is not native: WebGPU rejects `maxAnisotropy > 1` unless all three filters are linear, and
  D3D12 has no filter pairing point magnification with anisotropic minification. The function remaps the UV from its
  screen-space derivatives and samples once with the anisotropic sampler: crisp texels when magnified, anisotropic
  when minified, no hard switch, on every backend. It replaces WebCraft's `texelFetch`/`textureGrad` switch
  (`wc_gbuffer_fs.glsl:34`). The anisotropy level is already a device setting (`GpuPlatformDesc.max_anisotropy`).
- Cost: the reflected bindings in cooked shader assets change, so a full re-cook.

### 4. Indirect drawing capabilities and multi-draw-indirect (gpu)

- Problem: per-draw data is found through the instance index. D3D12's `SV_InstanceID` excludes the start instance,
  so the engine pushes it as a root constant before each draw (`renderer_shaders.glsl:185-196`). Indirect draws
  cannot do that: the command signatures are plain `DRAW_INDEXED` with `MaxCommandCount = 1`
  (`gpu_d3d12.c:1601-1626`). GPU culling therefore cannot feed per-draw data today.
- Capability shape: follow the existing struct-of-bits pattern (`gpu_compute_caps()` / `GpuComputeCaps`), not a
  third style next to it and the single `gpu_supports_*()` functions.
- Capabilities, queried by the caller (names to be settled in the design):
  - multi-draw-indirect, and multi-draw-indirect with a GPU count buffer
  - indirect first instance (non-zero `firstInstance` honored in indirect draws)
  - a per-draw ID or base instance visible to shaders in indirect draws
- Calls do exactly what they say: `gpu_draw_indexed_indirect` stays one indirect draw; a multi-draw call exists
  only where the capability is present and asserts otherwise. No emulation inside the GPU layer.
- Native mapping:

  | Backend | Multi-draw | Per-draw ID in the shader |
  |---|---|---|
  | D3D12 | `ExecuteIndirect` with a count buffer | a root constant carried per command in the command signature (all hardware), or `SV_StartInstanceLocation` on Shader Model 6.8 |
  | Vulkan | `vkCmdDrawIndexedIndirectCount` | `gl_DrawID`, or `firstInstance` via `gl_InstanceIndex` (`drawIndirectFirstInstance`) |
  | Metal | indirect draws, indirect command buffers | base instance (unverified in the engine) |
  | WebGPU | only as Chrome's experimental `chromium-experimental-multi-draw-indirect` (behind a flag) | `firstInstance`, only with the optional `indirect-first-instance` feature |

- D3D12: a command signature that changes root arguments (the per-command root constant) must be created against
  a root signature, so it cannot be one global signature as today. Created per root signature and cached with it.
- Possibly expose command bundles (WebGPU render bundles, D3D12 bundles, Metal indirect command buffers) as their
  own explicit, capability-gated API, since they are how a caller can replay many indirect draws cheaply where
  multi-draw is missing. To evaluate in the design.

### 5. GPU-driven drawing strategy and the GPU culling module (modules + renderer)

- CPU culling stays the default: it is the better choice up to several thousand objects.
- A reusable GPU culling module: input is a buffer of objects with bounds; output is compacted visible lists and
  indirect arguments per view (camera and each shadow cascade), optionally culled against last frame's depth.
- The module and the renderer read item 4's capabilities and pick the strategy per device: multi-draw with a count
  buffer where present; otherwise a loop of single indirect draws with `instanceCount = 0` in culled slots, recorded
  into a command bundle where bundles are available; with a per-draw ID from first instance where supported, else a
  per-draw buffer offset.
- Hybrid is the target shape: the CPU culls coarse groups (chunks, clusters), the GPU culls fine ones.
- Draw sources, and later large renderer instance sets, opt into this path.

### 6. Extract the geometry pipeline and CPU culling (modules)

- Extract, because they are complex and any custom renderer needs them:
  - Geometry pipeline: vertex-pulling format, quantization/compression, geometry pools and streaming upload.
    Streaming builds on the upload ring (engine P5), which today serves only the nine texture upload paths;
    `gpu_update_buffer` does not use it. The ring extends to buffer uploads, together with the open P5 step 2:
    worker lanes fill their own ring regions, since the staging memcpy on the main thread is now the dominant
    upload cost (~5 ms per 64-texture burst on D3D12 and Vulkan), and geometry streaming hits the same cost.
  - CPU culling: the lane-parallel frustum culling the renderer already does.
  - Material and shader system usable without the renderer (codegen, variants, semantic binding). Mostly separate
    already (`material.h`).
- Already standalone modules; confirm their public API: shadows, clustered lights and IBL (`lighting.h`); bloom,
  DoF, GTAO and grading (`post.h`). TAA/TAAU have no public API (`taa.c` / `taau.c`, no header): giving them one is
  new work, not a confirmation.
- Do not extract: bucketing and sorting (renderer policy), the per-draw record layout (tied to the renderer's
  shaders), pass ordering and frame uniforms (renderer glue).

### 7. Final blit as a material (renderer + shaders)

- Today the final pass (tonemap, grading, vignette, sharpening) is fixed and cannot be replaced.
- The default final pass becomes a cooked fullscreen material. The renderer supplies its inputs by semantic, the way
  materials already get textures: HDR color, bloom, exposure, grade LUT, depth, velocity.
- `blit.glsl` and `tonemapping.glsl` become a function library; a custom final pass is a material calling it.

### 8. Built-in modules (modules)

Reusable and fully interchangeable: usable directly by a custom renderer, and driven by the renderer by default.
Each gets its own contract, not just an init-time on/off. Priority:

1. Sky and atmosphere (Hillaire 2020) with dynamic ambient: static skies keep the baked cubemap; dynamic skies
   update a small SH/ambient term only when the sky changes, and spread reflection captures over several frames.
   Sky visibility (caves, overhangs) stays local occlusion, supplied by the app or engine AO/probes.
2. Shadow-cascade caching with app-driven invalidation (WebCraft's largest GPU win).
3. Height fog, aerial perspective and light shafts.
4. Volumetric clouds.
5. Screen-space reflections.
6. Water material with refraction.
7. Auto-exposure feeding the final pass.

### 8b. WebGPU honors `color_write_mask` (gpu)

- Problem: `color_write_mask` is documented D3D12-only and WebGPU ignores it, so output silently differs on web.
- Why before item 9: an app-defined g-buffer, and decals writing only some channels, depend on write masks.
- Change: map it to WebGPU's per-target `writeMask`; confirm Vulkan and Metal honor it.

### 9. Forward, forward+ and deferred (renderer + material system)

- Goal: all three supported well, without a combinatorial explosion of flags.
- Starting principles, to be discussed at the architecture level before any design:
  - The rendering path is the one axis. Effects, shadows and post are modules or runtime settings.
  - Materials describe a surface, not a g-buffer layout; codegen produces one variant per path, and one lighting
    function serves forward and deferred so they cannot drift apart.
  - Games can add surface fields and replace the lighting pass, while the renderer keeps depth, velocity, shadows,
    TAA and post.
- Known constraints: WebGPU's per-pixel color-attachment byte limit (believed 32 bytes by default, to verify per
  backend); the web build now requires `texture-formats-tier1` (engine P2), which changes which formats are
  renderable, so the g-buffer layout is checked against both; deferred rules out MSAA, so it needs TAA.

## Open questions

- Capability names (item 4); the shape follows `gpu_compute_caps()`.
- Whether command bundles are exposed in `gpu.h` (item 4).
- Metal's indirect base-instance behavior (items 4, 5).
- WebGPU's color-attachment byte limit per backend (item 9).
- Vulkan and Metal handling of array/3D textures in bindless pipelines (item 3).

## Sources

- [HLSL Shader Model 6.8](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_8.html)
- [D3D12 indirect drawing spec](https://microsoft.github.io/DirectX-Specs/d3d/IndirectDrawing.html)
- [MS Learn: indirect drawing](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing)
- [MDN: drawIndexedIndirect](https://developer.mozilla.org/en-US/docs/Web/API/GPURenderPassEncoder/drawIndexedIndirect)
- [MDN: render bundle drawIndexedIndirect](https://developer.mozilla.org/en-US/docs/Web/API/GPURenderBundleEncoder/drawIndexedIndirect)
- [Chrome 131: multi-draw-indirect](https://developer.chrome.com/blog/new-in-webgpu-131)
- [Dawn multi-draw-indirect feature](https://dawn.googlesource.com/dawn/+/HEAD/docs/dawn/features/multi_draw_indirect.md)
- [gpuweb multi-draw-indirect investigation](https://github.com/gpuweb/gpuweb/issues/4349)
- [Toji: WebGPU indirect draws](https://toji.dev/webgpu-best-practices/indirect-draws.html)
- [MDN: createSampler (anisotropy requires linear filters)](https://developer.mozilla.org/en-US/docs/Web/API/GPUDevice/createSampler)
- [D3D12_FILTER](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_filter)
- [UE sky lights (time-sliced capture)](https://dev.epicgames.com/documentation/unreal-engine/sky-lights-in-unreal-engine)
- [Hillaire 2020, production sky and atmosphere](https://sebh.github.io/publications/egsr2020.pdf)
- [Unity deferred path](https://docs.unity3d.com/Manual/RenderTech-DeferredShading.html)
