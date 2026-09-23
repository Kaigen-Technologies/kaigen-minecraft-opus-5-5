#include "lib/typedefs.h"
#include "lib/thread_context.h"
#include "lib/hzmath.h"
#include "os/os.h"
#include "rendering/renderer.h"
#include "rendering/camera.h"
#include "rendering/material.h"
#include "app/app.h"
#include "cubes/cubes_types.h"
#include "generated/cube.h"
#include "shaders/standard_static_material.h"
#include "generated/types_app.gen.h"

#define CUBES_MAX_CUBES 16

typedef struct {
  RendererState *renderer;
  Camera camera;
  HzHandleT(RendererSubMesh) cube_mesh;
  HzHandleT(RendererMaterial) cube_material;

  HzHandleT(RendererSubMeshInstance) cubes[CUBES_MAX_CUBES];
  u32 cube_count;
  f32 spin_speed;
  ColorF32 tint;
} AppState;

HZ_APP_API void app_init(AppMemory *memory) {
  if (!is_main_thread()) {
    return;
  }

  AppState *state = memory->state;

  state->spin_speed = 1.0f;
  state->cube_count = 3;
  state->tint = (ColorF32){0.9f, 0.55f, 0.2f, 1.0f};
  {
    TempAllocator temp = tctx_temp_allocator_begin(NULL);
    PlatformFileData settings =
        os_read_file("cooked/cubes_settings.hza", &temp.allocator);
    if (settings.success && settings.buffer_len >= sizeof(CubesSettings)) {
      CubesSettings *hs = (CubesSettings *)settings.buffer;
      state->spin_speed = hs->spin_speed;
      state->cube_count = (u32)hs->cube_count;
      state->tint = hs->tint;
      log_info("cubes: settings loaded (spin %, cubes %, tint % % %)",
               fmt_f32(hs->spin_speed), fmt_i32(hs->cube_count),
               fmt_f32(hs->tint.r), fmt_f32(hs->tint.g), fmt_f32(hs->tint.b));
    } else {
      log_warn("cubes: cooked/cubes_settings.hza missing, using defaults");
    }
    tctx_temp_allocator_end(temp);
  }
  if (state->cube_count > CUBES_MAX_CUBES) {
    state->cube_count = CUBES_MAX_CUBES;
  }

  state->renderer = renderer_init(&(RendererInitDesc){
      .sample_count = 4,
  });

  state->camera = camera_create(&(CameraInitDesc){
      .pos = {0.0f, 3.0f, 14.0f},
      .target = {0.0f, 0.0f, 0.0f},
      .fov_degrees = 45.0f});

  renderer_set_scene_settings(state->renderer,
                              &(RendererSceneSettings){
                                  .ambient = {0.25f, 0.27f, 0.32f, 1.0f},
                                  .ambient_intensity = 1.0f,
                              });
  renderer_set_directional_lights(
      state->renderer,
      &(RendererDirectionalLight){
          .direction = v3_normalize((v3){-0.4f, -1.0f, -0.35f}),
          .color = {1.0f, 1.0f, 1.0f},
          .intensity = 1.0f},
      1);

  state->cube_mesh = renderer_upload_submesh(state->renderer, &CUBE_MESH);

  state->cube_material = renderer_create_material(
      state->renderer, shader_standard_static_material_desc());
  material_set_color4(state->renderer, state->cube_material, "color",
                      state->tint);
  material_set_float(state->renderer, state->cube_material, "metallic", 0.0f);
  material_set_float(state->renderer, state->cube_material, "smoothness",
                     0.35f);

  for (u32 i = 0; i < state->cube_count; i++) {
    state->cubes[i] = renderer_submesh_instantiate(
        state->renderer,
        &(RendererSubMeshInstanceDesc){
            .submesh = state->cube_mesh,
            .material = state->cube_material,
            .transform = {.position = {0.0f, 0.0f, 0.0f},
                          .rotation = quat_identity(),
                          .scale = V3_ONE},
        });
  }

  log_info("cubes: initialized with % cubes", fmt_u32(state->cube_count));
}

HZ_APP_API void app_update_and_render(AppMemory *memory) {
  AppState *state = memory->state;
  RendererState *ri = state->renderer;

  if (is_main_thread()) {
    camera_update(&state->camera, memory->canvas_width, memory->canvas_height);

    f32 spacing = 2.5f;
    f32 offset = (state->cube_count - 1) * spacing * 0.5f;
    for (u32 i = 0; i < state->cube_count; i++) {
      f32 angle = memory->total_time * state->spin_speed + i * 0.5f;
      Transform xf = {
          .position = {i * spacing - offset, 0.0f, 0.0f},
          .rotation = quat_from_euler_xyz((v3){angle * 0.7f, angle, 0.0f}),
          .scale = V3_ONE,
      };
      renderer_submesh_instance_set_transform(ri, state->cubes[i], &xf);
    }
  }

  lane_sync();

  renderer_update(ri, &(RendererBeginFrameDesc){
                          .camera = &state->camera,
                          .clear_color = (ColorF32){0.13f, 0.14f, 0.2f, 1.0f},
                          .time = memory->total_time,
                      });
}

HZ_APP_API size_t app_state_size(void) { return sizeof(AppState); }
