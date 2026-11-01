#ifndef VX_SETTINGS_H
#define VX_SETTINGS_H

#include "lib/typedefs.h"
#include "lib/reflect.h"

HZ_REFLECT()
typedef enum {
  VX_PRESET_LOW,
  VX_PRESET_MEDIUM,
  VX_PRESET_HIGH,
  VX_PRESET_ULTRA,
  VX_PRESET_CUSTOM,
} VxPreset;

HZ_REFLECT()
typedef enum {
  VX_LIGHT_MODE_COLOR,
  VX_LIGHT_MODE_WHITE,
  VX_LIGHT_MODE_RETRO,
  VX_LIGHT_MODE_TRIPPY,
} VxLightMode;

HZ_REFLECT()
typedef enum {
  VX_WEATHER_AUTO,
  VX_WEATHER_CLEAR,
  VX_WEATHER_RAIN,
} VxWeatherMode;

// every user-facing option
HZ_REFLECT()
typedef struct VxSettings {
  VxPreset preset;
  u32 render_distance;
  f32 render_scale;
  b32 shadows;
  u32 shadow_res;
  f32 shadow_distance;
  b32 soft_shadows;
  b32 ssao;
  b32 volumetrics;
  u32 clouds;
  VxLightMode lighting;
  b32 taa;
  b32 bloom;
  f32 fov;
  f32 sensitivity;
  b32 bobbing;
  b32 screen_shake;
  f32 day_length;
  f32 cloud_coverage;
  VxWeatherMode weather;
  f32 volume;
  // the character: indices into the puppet's palettes
  u32 skin_tone;
  u32 hair_color;
  u32 hair_style;
  u32 eye_color;
  u32 shirt_color;
  u32 undershirt_color;
  u32 pants_color;
  u32 shoe_color;
} VxSettings;

#endif
