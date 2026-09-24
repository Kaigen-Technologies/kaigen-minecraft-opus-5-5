#ifndef WC_SETTINGS_H
#define WC_SETTINGS_H

#include "lib/typedefs.h"
#include "lib/reflect.h"

HZ_REFLECT()
typedef enum {
  WC_PRESET_LOW,
  WC_PRESET_MEDIUM,
  WC_PRESET_HIGH,
  WC_PRESET_ULTRA,
  WC_PRESET_CUSTOM,
} WcPreset;

HZ_REFLECT()
typedef enum {
  WC_WEATHER_AUTO,
  WC_WEATHER_CLEAR,
  WC_WEATHER_RAIN,
} WcWeatherMode;

// every user-facing option
HZ_REFLECT()
typedef struct WcSettings {
  WcPreset preset;
  u32 render_distance;
  f32 render_scale;
  b32 shadows;
  u32 shadow_res;
  f32 shadow_distance;
  b32 ssao;
  b32 volumetrics;
  u32 clouds;
  b32 ssr;
  b32 taa;
  b32 bloom;
  f32 fov;
  f32 sensitivity;
  b32 bobbing;
  f32 day_length;
  f32 cloud_coverage;
  WcWeatherMode weather;
  f32 volume;
} WcSettings;

#endif
