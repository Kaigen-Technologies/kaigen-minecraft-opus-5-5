#include "voxaria/vx_game.h"

// settings presets and their mapping onto the renderer

void vx_settings_apply_preset(VxSettings *s, VxPreset preset) {
  switch (preset) {
  case VX_PRESET_LOW:
    s->render_distance = 6;
    s->render_scale = 0.75f;
    s->shadows = false;
    s->shadow_res = 1024;
    s->shadow_distance = 64;
    s->ssao = false;
    s->soft_shadows = false;
    s->volumetrics = false;
    s->clouds = 1;
    break;
  case VX_PRESET_MEDIUM:
    s->render_distance = 8;
    s->render_scale = 0.85f;
    s->shadows = true;
    s->shadow_res = 1024;
    s->shadow_distance = 96;
    s->ssao = false;
    s->soft_shadows = false;
    s->volumetrics = false;
    s->clouds = 1;
    break;
  case VX_PRESET_HIGH:
    s->render_distance = 12;
    s->render_scale = 1;
    s->shadows = true;
    s->shadow_res = 2048;
    s->shadow_distance = 128;
    s->ssao = false;
    s->soft_shadows = false;
    s->volumetrics = true;
    s->clouds = 2;
    break;
  case VX_PRESET_ULTRA:
    s->render_distance = 16;
    s->render_scale = 1;
    s->shadows = true;
    s->shadow_res = 3072;
    s->shadow_distance = 192;
    s->ssao = true;
    s->soft_shadows = true;
    s->volumetrics = true;
    s->clouds = 2;
    break;
  case VX_PRESET_CUSTOM: return;
  }
  s->taa = true;
  s->bloom = true;
  s->preset = preset;
}

void vx_settings_defaults(VxSettings *s) {
  *s = (VxSettings){.fov = 75, .sensitivity = 1, .bobbing = true, .screen_shake = true, .day_length = 20, .cloud_coverage = 0.62f,
                    .weather = VX_WEATHER_AUTO, .volume = 0.6f, .skin_tone = 1, .hair_color = 1,
                    .eye_color = 12, .shirt_color = 11, .undershirt_color = 6, .pants_color = 1};
  vx_settings_apply_preset(s, VX_PRESET_HIGH);
}

VxRenderSettings vx_settings_render(const VxSettings *s) {
  return (VxRenderSettings){.render_distance = s->render_distance, .render_scale = s->render_scale,
                            .shadows = s->shadows, .shadow_res = s->shadow_res,
                            .shadow_distance = s->shadow_distance, .soft_shadows = s->soft_shadows, .ssao = s->ssao,
                            .volumetrics = s->volumetrics, .clouds = s->clouds, .lighting = s->lighting, .taa = s->taa, .bloom = s->bloom, .fov = s->fov};
}
