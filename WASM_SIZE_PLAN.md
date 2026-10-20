# WebCraft wasm size plan

Goal: make the published `game.wasm.br` as small as possible without hacks. Today it is 241,228 bytes brotli (1,220,442 raw), built with `hz\hzbuild wasm --publish`.

This file is the source of truth. Mark each step `[done]` when it is finished, and update it when a design decision changes. Every step is engine work (`hz/build`, `hz/src`); the owner approved all of it on 2026-10-20.

## Measured

Measured on a rebuild of the publish wasm that matched the shipped file within 0.4%. The scripts are in the session scratchpad.

| Build | Brotli |
|---|---|
| Shipped (`-O3`) | 240.0 KB |
| `-Oz` everywhere | 187.8 KB |
| `-Oz`, type registry off | 163.0 KB |
| `wasm-opt -Oz --converge` on top of `-Oz` | −2.9 KB |
| WGSL: one frequency-ranked name table plus whitespace | −4.8 KB |
| WGSL: whitespace only | −2.0 KB |

Rejected:
- per-shader renaming: the file grows 0.9 KB;
- hash-derived names: −0.9 KB at 2 characters, +2.5 KB at 3.

## Steps

### Step 0 — Recompile when compile flags change [done]
- Today `module_needs_recompile` (`hz/build/build.c`) checks only file times and dependencies. A changed define or flag in `hzproject.hzt` leaves old objects in place.
- Fix: store a 64-bit hash of each object's final compile command in its dependency blob (`DependenciesFile`, field appended). Recompile when the hash differs.
- Check:
  - change a flag: the affected objects recompile;
  - build again without changes: nothing recompiles.
- Result: adding `defines 1 HZ_STEP0_PROBE=1` recompiled 72 modules, removing it recompiled 72 again, and a third build with no change compiled none.

### Step 1 — `wasm_optimize` project setting [done]
- `HzProjectConfig` (`hz/src/lib/project_config.h`) gains `StringBlob wasm_optimize`.
  - Append it after `modules`: an older hzbuild drops every field after one it doesn't know.
  - Mirror it in `hz/build/project_config_typeinfo.h`.
- Values:
  - `speed`: the default, today's `-O3`;
  - `size`: `-Oz`;
  - any other value: the build fails with an error naming the field.
- It applies to wasm release and publish, not debug, and not native platforms.
- hzbuild puts the flag in a new `BuildOverrides.opt_flags`. The clang compile command appends it after the release/publish set, so it wins.
- Link flags are unchanged: LTO takes the level from each function's compile attributes (`--lto-O3` and `--lto-O2` gave identical bytes).
- WebCraft's `hzproject.hzt` sets `wasm_optimize size`.
- Check:
  - the wasm release run and both publish bundles work (see Verification);
  - record the publish `game.wasm.br` size.
- Result:
  - `wasm_optimize tiny` fails with "config: wasm_optimize must be speed or size, got 'tiny'" (exit 1, nothing compiled);
  - with `size`, the wasm release `game.wasm` is 934,850 bytes raw;
  - publish `game.wasm.br`: **189,956** (multi-threaded) and **186,914** (single-threaded);
  - the release run reached quit at frame 460 with the world on screen;
  - both publish bundles booted to the menu (16 and 1 lanes).

### Step 2 — `type_registry` project setting [done]
- `HzProjectConfig` gains `StringBlob type_registry`, appended after `wasm_optimize` and mirrored by hand.
- Values:
  - `on`: the default, today's behaviour;
  - `off`;
  - any other value: the build fails.
- `hz_init` (`hz/src/entrypoint/hz_engine.c`) no longer calls `type_table_register_engine()` or `type_table_finalize()` itself. It calls the app's generated registration, written by `hz/build/codegen_reflect.c`:
  - when `on`, it registers the engine types, then the app's, then finalizes;
  - when `off`, it only marks the registry disabled.
- So no reference to the engine type tables or lookup arrays remains when `off`, and the linker drops them. This also works with a precompiled engine archive.
- `hz/src/lib/reflect.c`: when disabled, `type_table_get`, `type_table_find_by_typehash` and `type_table_dense_entries` assert in dev builds with a message naming the `type_registry off` setting.
- Unchanged: blob loading (compile-time `TYPE_HASH`), `fmt_struct`, the test entrypoint and the exporter (they register for themselves).
- WebCraft's `hzproject.hzt` sets `type_registry off`.
- Check:
  - the WebCraft wasm release run and both publish bundles work;
  - the native WebCraft build runs;
  - reflection/type-table tests in `hz_tests` pass, run filtered;
  - record the size.
- Result:
  - publish `game.wasm.br`: **164,790** (multi-threaded, −25,166 vs Step 1) and **161,698** (single-threaded);
  - the native D3D12 debug build ran to quit with the world on screen and no asserts;
  - the wasm release run reached quit at frame 521;
  - both publish bundles booted to the menu;
  - `hz_tests` filter `reflect`: 2/2 passed;
  - a temporary probe (`type_table_get("WcSettings")` in `app_init`, since removed) asserted with "type_table_get(WcSettings) with the type registry disabled: this path needs reflection, set type_registry on in hzproject.hzt".

### Step 3 — Native `wasm-opt` in publish [done]
- New pinned toolchain component: Binaryen `version_133` release binaries for Windows and macOS, checked by sha512, installed by `hzbuild install`, resolved by `toolchain_wasm_opt()`. Linux has no pinned toolchain, so it uses `wasm-opt` from PATH (see Decisions).
- If it's missing, publish fails with an error telling the user to run `hzbuild install`.
- `build_wasm_publish`:
  1. link without `--strip-all`, so the feature list stays in the module;
  2. run `wasm-opt -Oz --converge --strip-debug --strip-producers --strip-target-features`;
  3. then the existing license section and brotli.
- This applies to both the multi-threaded and single-threaded (`wasm ios`) bundles.
- Check:
  - both publish bundles boot;
  - the output has no name or target-features section;
  - record the size.
- Result:
  - `hzbuild install` downloaded binaryen 133 (sha512 ok), installed only `bin/wasm-opt.exe`, removed the tarball and wrote `binaryen 133` to the manifest. A second run logged "binaryen present, skipping".
  - Before the install, `wasm --publish` stopped in 2 s with "toolchain: binaryen 133 (wasm-opt) is not installed — run `hzbuild install`", exit 1.
  - `wasm-opt`: multi-threaded 774,752 → 737,243 bytes raw, single-threaded 756,750 → 720,049.
  - Publish `game.wasm.br`: **160,959** (multi-threaded) / **157,853** (single-threaded), from 164,790 / 161,698.
  - Sections in the shipped module: type, import, function, table, global, export, start, element, datacount, code, data, `custom:hzlic`. No name, producers or target_features section. The start section is the linker's own (also present in the release build).
  - Both publish bundles booted to the menu over the world (16 lanes / 1 lane); the only error line is the known favicon 404.
  - `toolchain_pins.json` passes the drift gate: "pin-drift gate: 10 pins + keys + members found in build/toolchain.c".

### Step 4 — WGSL minification at publish [done]
- New `hz/build/shader_wgsl_minify.c`, run by `build_wasm_publish` before compiling. It tokenises every generated WGSL stage and builds **one** name table across all stages, ranked by how often each name appears. It writes `<shader>_<stage>_webgpu_min.h`.
- `hz/build/shader_codegen.c` makes each shader header include the `_min` header under `HZ_PUBLISH`. If the file is missing, compilation fails; there is no fallback.
- Dev and release builds keep readable WGSL.
- What gets renamed: only names the shader declares (variables, functions, structs, members, parameters).
- Never renamed:
  - entry points (`main`, `vs`, `fs`);
  - swizzle-shaped names;
  - members of built-in result structs (`fract`, `whole`, `exp`, `old_value`, `exchanged`).
- Never generated as a new name:
  - WGSL keywords, reserved words and predeclared names;
  - any token already in the shader;
  - anything containing `f16`, or shaped like `vecNh`/`matNxMh` (the runtime f16 patcher, `gpu_webgpu.c:851`, rewrites those).
- Whitespace and comments are stripped.
- The pass checks the renaming is one-to-one, and that renaming back reproduces the original token stream exactly. It fails the build otherwise.
- Check:
  - both publish bundles boot in Chromium with no shader-module or pipeline errors in the log;
  - record the size.
- Result:
  - "wgsl minify: 247 stages, 1823862 -> 1043578 bytes, 3945 of 4685 names renamed"; every stage passed the in-build round trip.
  - Publish `game.wasm.br`: **158,217** (multi-threaded) / **154,747** (single-threaded), from 160,959 / 157,853.
  - Both publish bundles booted to the menu over the world (16 lanes / 1 lane) with no shader-module or pipeline errors; the only error line is the known favicon 404.
  - All 247 minified stages, plus the 3 f16 stages run through the f32 fallback on a device without `shader-f16`, compile in Chromium's WebGPU with zero errors, the same as the originals (a scratchpad Playwright check, not shipped).
  - Negative probe: with the separator rule broken on purpose, the publish failed with "wgsl minify: blit_fs_webgpu: 4433 tokens after minify, 4601 before" and stopped (probe removed).

## Verification (each step)
1. **Rebuild hzbuild** after changing `hz/build` (`hz/scripts/windows/hzbuild.ps1`).
2. **Builds:** `hz\hzbuild`, `hz\hzbuild wasm`, `hz\hzbuild wasm --publish` and `hz\hzbuild wasm ios --publish`, each with no line matching "error".
3. **Wasm release run:** `hz\hzbuild wasm --release --commands "0.1:wc_seed:12345;0.2:wc_play;8:screenshot;9:quit"`. It must reach `quit` with no error or assert lines, and the screenshot must show the world.
4. **Publish boot:** both publish bundles go through `hz/web/tests/screenshot.ts`. Each must reach the menu over the world, with no error lines apart from the known favicon 404.
5. **Filtered `hz_tests`** for the code the step touched.

## Final result (2026-10-20, after the last code change)
| Publish `game.wasm.br` | Multi-threaded | Single-threaded (`wasm ios`) |
|---|---|---|
| Before (shipped) | 241,228 | — |
| Step 1 (`-Oz`) | 189,956 | 186,914 |
| Step 2 (type registry off) | 164,790 | 161,698 |
| Step 3 (`wasm-opt`) | 160,959 | 157,853 |
| Step 4 (WGSL minify) | 158,217 | 154,747 |
| Final rebuild | **158,290** (−82,938, −34.4%) | **154,851** |

Step 0 changes no output. The final rebuild differs from the Step 4 row by about 100 bytes with no source change; I did not investigate the cause.

Final checks: hzbuild, `hz\hzbuild`, `hz\hzbuild wasm`, `wasm --publish` and `wasm ios --publish` all built with zero error lines. The wasm release run reached quit at frame 483 with the world on screen. Both publish bundles booted to the menu over the world. `hz_tests --filter reflect`: 2/2 passed. The only 404 in any browser run is `/favicon.ico` (confirmed by logging the request at the server).

**Untested:** the `build_sdk` change (the wasm publish SDK libs compile against the minified WGSL) compiles but was not run. `hzbuild sdk` builds and packs every SDK lib, which is outside this plan's checks.

## Decisions and engine changes
- **Step 0, hash scope:** the hash covers the whole compile command (compiler path, sysroot, includes, defines, flags, the dependency output and object paths) and, for generated unity modules, the list of files in the unity. A file added to a unity with an older modification time now triggers a recompile too.
- **Step 0, structure:** each compile worker is split into a command builder and a runner. The scheduler builds the command once, hashes it, decides, and hands it to the job, so it isn't built twice.
- **Step 0, old blobs:** dependency blobs written before this change are shorter or carry no hash, so they compare unequal and the object recompiles once. Nothing silently reads past the end of the file (length check).
- **Step 0, files:**
  - `hz/build/build.h`: `ModuleCompileJob.cmd`/`cmd_hash`, and `DependenciesFile.cmd_hash` (appended).
  - `hz/build/build.c`: `module_dep_path`, `module_compile_cmd_msvc`/`_clang`, `module_compile_job_prepare`, the hash check in `module_needs_recompile`, the hash written by `write_dependencies_blob`.

- **Step 1, scope:** the flag reaches the project's compile units and, in a source checkout, the engine's. A precompiled SDK drop ships its engine archive already built, so only the project's own code follows the setting there; the drop decides its own engine flags.
- **Step 1, validation:** an unknown value is an error on every platform, not only wasm, so a typo shows up on the first build of any kind.
- **Step 1, link:** `-Oz` is a compile flag only (`BuildOverrides.release_opt_flags`, appended after the release/publish set in both the clang and msvc compile commands). The link keeps `--lto-O3`, because LTO honours each function's `optsize`/`minsize` attribute.
- **Step 1, files:**
  - `hz/src/lib/project_config.h`: `wasm_optimize`, appended after `modules`.
  - `hz/build/project_config_typeinfo.h`: mirror entry; field count 34.
  - `hz/build/build.h`: `BuildOverrides.release_opt_flags`.
  - `hz/build/build.c`: both compile commands append it.
  - `hz/build/hzbuild.c`: `WASM_SIZE_OPT_FLAGS`, plus validation and wiring in `load_project_config`.
  - `hzproject.hzt`: `wasm_optimize size`.

- **Step 2, shape:**
  - The plan's wording ("hz_init calls the app's generated registration") is kept, but through a NEW generated function, `app_type_registry_init`. `app_register_types` keeps its meaning, app table only, because the exporter, `hub_tests.c` and `cubes_tests.c` call it next to their own engine registration.
  - The setting reaches the generated code as the define `HZ_TYPE_REGISTRY_OFF=1` rather than through codegen. The generated header is the same whatever the setting, and the define's command-line change recompiles through Step 0. Codegen skips regenerating the type tables when no header changed, so a codegen-time switch would go stale.
- **Step 2, codegen cache:** `CODEGEN_CACHE_VERSION` 5 → 6, so every project regenerates `types_app.gen.h` with the new function on its next build. Without that, an untouched project would fail to link `app_type_registry_init`.
- **Step 2, diagnostics lookup:** `type_table_find_by_typehash` returns NULL when the registry is disabled instead of asserting. Its only runtime callers name a type in an error message and already fall back to `<unknown>`; asserting there would turn a readable stale-asset error into a crash. `type_table_get` and `type_table_dense_entries` assert, and so do `type_table_register` after disable and `type_table_disable` after registration.
- **Step 2, `types_dump`:** it includes `types_app.gen.h` only for the table data. It now defines `HZ_TYPES_NO_REGISTER_FN`, as `test_main.c` does. Otherwise the new function's reference to `type_table_register_engine` (defined in the engine's assets object) pulls the engine entrypoint into the tool, and the link fails with duplicate `main` and missing `app_*`.
- **Step 2, Windows test build (pre-existing):** `hz_tests` logic tests did not compile on MSVC. MSVC's `atomic_u32_read_acquire` returns a signed `long`, and `/WX` turns its comparison with a `u32` into an error (C4018).
  - Fixed at the two call sites with the `(u32)` cast `concurrent_arr_len` already uses (`test_lane_claim.c`, 2 lines; `test_os_sync.c:487`).
  - **For the owner:** the whole MSVC `atomic_u32_*` family in `src/lib/thread_context.h` returns signed `long`, while clang returns `u32`. I left that unchanged; it's a library-wide change outside this task.
- **Step 2, files:**
  - `hz/src/lib/reflect.h`/`.c`: `type_table_disable`, the disabled flag, asserts.
  - `hz/src/entrypoint/hz_engine.c`: `hz_init` calls `app_type_registry_init`; `HZ_TEST` stub keeps the old behaviour.
  - `hz/src/app/app.h`: declaration.
  - `hz/build/codegen_reflect.c`/`.h`: emit the function; cache version 6.
  - `hz/src/lib/project_config.h`, `hz/build/project_config_typeinfo.h`: `type_registry` (field count 35).
  - `hz/build/hzbuild.c`: validation and define.
  - `hz/src/types_dump/types_dump.c`: `HZ_TYPES_NO_REGISTER_FN`.
  - `hz/hz_tests/tests/logic/test_lane_claim.c`, `test_os_sync.c`: casts.
  - `hzproject.hzt`: `type_registry off`.

- **Step 3, checksum:** sha512, not the plan's sha256. Every toolchain artifact and the pins drift gate use sha512; the sha256 values published with the release were also checked against the downloads.
- **Step 3, Linux:** the Linux section of `toolchain.c` is a stub with no pinned toolchain for any component, so `toolchain_wasm_opt()` there runs `wasm-opt --version` from PATH and fails with an error if it isn't found. Windows and macOS resolve only the pinned absolute path, never PATH, so publish output is deterministic.
- **Step 3, what's installed:** Windows extracts only `bin/wasm-opt.exe` (self-contained, 10 MB, of a 111 MB archive). macOS extracts `bin/wasm-opt` and `lib/libbinaryen.dylib`, which it links against. Install checks the pin by running `--version` and matching "(version_133)"; a different installed version is removed and reinstalled.
- **Step 3, mirror:** `toolchain_pins.json` gets two binaryen artifacts with `trim: true` and their members, but empty mirror key and sha, so clients fetch upstream from GitHub (the drift gate warns "no mirror artifact yet"). `set_id` tc-3 → tc-4. **For the owner:** run the trim/ship ops to populate the mirror.
- **Step 3, LICENSE:** the binaryen release archive contains no LICENSE file (Apache-2.0). Nothing ships with the game: `wasm-opt` is a build tool only.
- **Step 3, failure handling:** `wasm-opt` writes `game.wasm.opt`; on a non-zero exit or a missing output, publish logs the tool's output, deletes the partial file and fails. There is no fallback to the unoptimised module.
- **Step 3, lane race fixed (pre-existing):** `build_wasm_publish` set `*out_error` (that is, `state.build_error`) on the main lane before its first `lane_sync`, while other lanes could still be reading `state.build_error` at the call site. Those lanes skipped the function and the main lane waited forever at its barrier (seen: 4 hangs in 23 runs right after an hzbuild rebuild; a probe showed 8 lanes at 31 syncs and 8 at 30). The preflight result is now a lane-shared local, published to `*out_error` only after the sync: 0 hangs in 20 runs. **For the owner:** `build_windows_publish`, `build_macos_publish` and `build_ios_publish` set `*out_error` the same way on a license-gate failure (drop installs only). I left them unchanged: not part of this plan, and two can't be tested here.
- **Step 3, host shaders for the publish exporter (pre-existing):** `publish_cook_prepare` built the exporter (a host D3D12/Metal binary) without compiling the host backend's shaders. So `wasm --publish` failed after any `hzbuild clean` (which `hzbuild.ps1` runs after every rebuild) with "Cannot open include file: 'common_inner_reflect_d3d12.h'". It had only worked when an earlier native build had left those headers behind. The host-shader step from `dev_cook_assets` is now a shared function, `build_exporter_host_shaders`, called by both; verified by publishing straight after a clean.
- **Step 3, files:**
  - `hz/build/toolchain.c`: `TC_BINARYEN_*` pins (Windows, macOS), `tc_wasm_opt_matches_pin`, the install steps and manifest lines, `toolchain_wasm_opt` (Windows, macOS, Linux).
  - `hz/build/toolchain.h`: `toolchain_wasm_opt` declaration; includes `lib/memory.h`.
  - `hz/build/hzbuild_wasm.h`: no `--strip-all` in the publish link flags (wasm and wasm-ios).
  - `hz/build/hzbuild.c`: preflight in `build_wasm_publish`, the `wasm-opt` pass, `build_exporter_host_shaders`.
  - `hz/hz-backend/toolchain_pins.json`: binaryen artifacts, set tc-4.

- **Step 4, where the `_min` headers live:** in the project's `out/wasm/wgsl_min` (the SDK pack: `out/sdk-build/wgsl_min`), added to the publish compile's include dirs, not in the shared engine `src/generated/shaders`. Two projects publishing against one engine tree can't overwrite each other's headers, and engine and project shaders resolve from the same place. Stale `_min` headers there are removed; unchanged ones are not rewritten.
- **Step 4, selection point:** `codegen_emit_webgpu_stage_includes` writes the `#if defined(WASM) && defined(HZ_PUBLISH)` choice. Both the umbrella header and the per-backend reflect header call it, because the reflect header also includes the stage headers. Linux's WebGPU headers are untouched (no `WASM`).
- **Step 4, code length (bug found):** the reflect header wrote each stage's length as a literal from compile time (`.vs_code_len = 6912`). With the shorter `_min` arrays, the runtime read past each array into the next shader, and Chrome rejected the modules. For the WebGPU target the length is now `sizeof(<stage>_webgpu)`, which follows whichever header was included. Other backends keep the literal.
- **Step 4, shader cache:** `SHADER_CACHE_VERSION` 9 → 12 (10 and 11 were intermediate formats cached on this machine), so every project regenerates its shader headers on the next build.
- **Step 4, what is renamed:** a name is renamed only if it is declared somewhere (after var/let/const/override/fn/struct/alias, or before `:`). It is never renamed if:
  - it appears after `@`, inside attribute or `diagnostic(...)` arguments, inside an enable/requires directive or a `var<...>` template, or is an entry point;
  - it is a keyword, reserved word or predeclared name, a builtin-value / interpolation / enumerant name, a builtin result member, swizzle-shaped, or starts with `_`.
  These rules apply across all stages: a name pinned anywhere keeps its name everywhere. New names are `[a-zA-Z][a-zA-Z0-9]*`, shortest first, and never equal any identifier in any stage or a reserved name, never contain `f16` and are never swizzle-shaped.
- **Step 4, checks in the build (hard failures):** a strict reader for the generated header format (the length line must match the decoded bytes); non-ASCII bytes and unterminated comments rejected; the minified text is lexed again and every token must map back through the inverse table to the original; `enable f16;` must survive exactly when present; the same stage name in two dirs is rejected.
- **Step 4, ranking (measured, plan figure corrected):** ranking only the stages that actually ship would save about 0.7 KB more (single-threaded module: −3,629 vs −2,904 bytes for all-stages ranking; whitespace only −2,351). But which stages ship is only known after the LTO link, so it would cost a second full compile and link per publish. The table ranks all generated stages; ties break by name, so the output doesn't depend on directory order. The plan's −4.8 KB was not reproduced on this baseline even with shipped-only ranking.
- **Step 4, files:**
  - `hz/build/shader_wgsl_minify.c`/`.h`: new; tokenizer, classification, rename table, emit and round trip.
  - `hz/build/shader_compile_glsl.c`/`.h`: `format_bytecode_header` split out of `write_bytecode_header`.
  - `hz/build/shader_codegen.c`: `codegen_emit_webgpu_stage_includes`; `sizeof` lengths for WebGPU.
  - `hz/build/shader_compiler.h`: `SHADER_CACHE_VERSION` 12.
  - `hz/build/hzbuild.c`: includes; `WASM_WGSL_MIN_DIR`; `wasm_publish_minify_wgsl` (fails hard without a shader compiler); called in `build_wasm_publish` after the cook prepare; `build_sdk` minifies once and adds the dir for the wasm publish libs.

## Not in this plan
- **The `-Oz` speed benchmark (D3D12 and wasm).** The owner runs it when the machine is free. If a hot path regresses, the fallback is `-O3` with `minsize` on cold code (measured 213.9 KB).
- **Smaller items:** the GPU executor's unused paths, fast trig, and leaving dev commands out of publish.
