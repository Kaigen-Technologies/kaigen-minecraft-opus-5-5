#ifndef WC_SAVE_H
#define WC_SAVE_H

#include "lib/typedefs.h"
#include "lib/reflect.h"
#include "lib/blob_asset.h"

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

// every user-facing option; persisted as WcSettingsSave
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

HZ_REFLECT()
typedef struct WcSettingsSave {
  BlobAssetHeader header;
  WcSettings settings;
} WcSettingsSave;

HZ_REFLECT()
typedef struct WcSaveEdit {
  u16 index;
  u8 id;
  u8 pad;
} WcSaveEdit;

HZ_REFLECT()
typedef struct WcSaveChunk {
  i32 cx;
  i32 cz;
  BlobArray(WcSaveEdit) edits;
} WcSaveChunk;

// world seed, player, clock, hotbar and every player edit
HZ_REFLECT()
typedef struct WcWorldSave {
  BlobAssetHeader header;
  u32 seed;
  b32 has_player;
  f64 px;
  f64 py;
  f64 pz;
  f32 yaw;
  f32 pitch;
  b32 flying;
  f32 day_time;
  BlobArray(u8) hotbar;
  BlobArray(WcSaveChunk) chunks;
} WcWorldSave;

#endif
