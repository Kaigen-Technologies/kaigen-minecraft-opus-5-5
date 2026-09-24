#include "webcraft/wc_game.h"

// settings presets and their mapping onto the renderer

void wc_settings_apply_preset(WcSettings *s, WcPreset preset) {
  switch (preset) {
  case WC_PRESET_LOW:
    s->render_distance = 6;
    s->render_scale = 0.75f;
    s->shadows = false;
    s->shadow_res = 1024;
    s->shadow_distance = 64;
    s->ssao = false;
    s->volumetrics = false;
    s->clouds = 1;
    s->ssr = false;
    break;
  case WC_PRESET_MEDIUM:
    s->render_distance = 8;
    s->render_scale = 0.85f;
    s->shadows = true;
    s->shadow_res = 1024;
    s->shadow_distance = 96;
    s->ssao = true;
    s->volumetrics = false;
    s->clouds = 1;
    s->ssr = true;
    break;
  case WC_PRESET_HIGH:
    s->render_distance = 12;
    s->render_scale = 1;
    s->shadows = true;
    s->shadow_res = 2048;
    s->shadow_distance = 128;
    s->ssao = true;
    s->volumetrics = true;
    s->clouds = 2;
    s->ssr = true;
    break;
  case WC_PRESET_ULTRA:
    s->render_distance = 16;
    s->render_scale = 1;
    s->shadows = true;
    s->shadow_res = 3072;
    s->shadow_distance = 192;
    s->ssao = true;
    s->volumetrics = true;
    s->clouds = 2;
    s->ssr = true;
    break;
  case WC_PRESET_CUSTOM: return;
  }
  s->taa = true;
  s->bloom = true;
  s->preset = preset;
}

void wc_settings_defaults(WcSettings *s) {
  *s = (WcSettings){.fov = 75, .sensitivity = 1, .bobbing = true, .day_length = 20, .cloud_coverage = 0.4f,
                    .weather = WC_WEATHER_AUTO, .volume = 0.6f};
  wc_settings_apply_preset(s, WC_PRESET_HIGH);
}

WcRenderSettings wc_settings_render(const WcSettings *s) {
  return (WcRenderSettings){.render_distance = s->render_distance, .render_scale = s->render_scale,
                            .shadows = s->shadows, .shadow_res = s->shadow_res,
                            .shadow_distance = s->shadow_distance, .ssao = s->ssao, .volumetrics = s->volumetrics,
                            .clouds = s->clouds, .ssr = s->ssr, .taa = s->taa, .bloom = s->bloom, .fov = s->fov};
}
