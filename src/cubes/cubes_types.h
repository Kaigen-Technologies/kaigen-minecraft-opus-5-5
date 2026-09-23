#ifndef H_CUBES_TYPES
#define H_CUBES_TYPES

#include "lib/typedefs.h"
#include "lib/reflect.h"
#include "lib/blob_asset.h"
#include "lib/color.h"

HZ_REFLECT()
typedef struct {
  BlobAssetHeader header;
  f32 spin_speed;
  i32 cube_count;
  ColorF32 tint;
} CubesSettings;

#endif
