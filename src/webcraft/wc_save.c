#include "webcraft/wc_game.h"

// settings presets and blob-asset persistence of settings, world meta and player edits

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

hz_internal void wc_fill_header(BlobAssetHeader *h, u64 type_hash, u32 size) {
  h->magic = ASSET_MAGIC;
  h->version = ASSET_VERSION;
  h->asset_type_hash = type_hash;
  h->asset_size = size;
}

hz_internal b32 wc_blob_valid(const u8 *buf, u32 len, u64 type_hash) {
  if (!buf || len < sizeof(BlobAssetHeader)) return false;
  const BlobAssetHeader *h = (const BlobAssetHeader *)buf;
  return h->magic == ASSET_MAGIC && h->version == ASSET_VERSION && h->asset_type_hash == type_hash &&
         h->asset_size == len;
}

// write beside the target and rename over it, so a crash never leaves a torn save
hz_internal b32 wc_write_atomic(const char *path, u8 *data, u32 size) {
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  String tmp_path = str_format(&tmp.allocator, "%.tmp", fmt_cstr(path));
  b32 ok = os_write_file(tmp_path.value, data, size) && os_rename(tmp_path.value, path);
  if (!ok) log_warn("webcraft: could not write %", fmt_cstr(path));
  tctx_temp_allocator_end(tmp);
  return ok;
}

b32 wc_settings_write(const WcSettings *s, const char *path) {
  BlobAssetBuilder bb = blob_builder_begin();
  WcSettingsSave *root = blob_builder_alloc(&bb, WcSettingsSave);
  root->settings = *s;
  wc_fill_header(&root->header, TYPE_HASH(WcSettingsSave), bb.size);
  // the builder rewinds its own temp arena in finish: the output lives in the other one
  TempAllocator tmp = tctx_temp_allocator_begin(bb.temp.arena);
  BlobBuilderResult(u8) out;
  blob_builder_finish(&bb, u8, &tmp.allocator, &out);
  b32 ok = wc_write_atomic(path, out.data, out.size);
  tctx_temp_allocator_end(tmp);
  return ok;
}

b32 wc_settings_read(const u8 *buf, u32 len, WcSettings *out) {
  if (!wc_blob_valid(buf, len, TYPE_HASH(WcSettingsSave))) return false;
  WcSettings s = ((const WcSettingsSave *)buf)->settings;
  // reject values no build of this game could have written
  if (s.render_distance < 4 || s.render_distance > WC_MAX_RENDER_DISTANCE) return false;
  if (!(s.render_scale >= 0.5f && s.render_scale <= 1.0f)) return false;
  if (s.shadow_res < 1024 || s.shadow_res > 4096) return false;
  if (s.clouds > 2 || s.preset > WC_PRESET_CUSTOM || s.weather > WC_WEATHER_RAIN) return false;
  *out = s;
  return true;
}

b32 wc_world_save_write(const char *path, const WcWorldMeta *meta, const WcEditStore *edits) {
  BlobAssetBuilder bb = blob_builder_begin();
  WcWorldSave *root = blob_builder_alloc(&bb, WcWorldSave);
  root->seed = meta->seed;
  root->has_player = meta->has_player;
  root->px = meta->pos.x;
  root->py = meta->pos.y;
  root->pz = meta->pos.z;
  root->yaw = meta->yaw;
  root->pitch = meta->pitch;
  root->flying = meta->flying;
  root->day_time = meta->day_time;
  u8 *hb = blob_builder_array(&bb, &root->hotbar, u8, WC_HOTBAR_SLOTS);
  mem_cpy(hb, meta->hotbar, WC_HOTBAR_SLOTS);
  u32 n = 0;
  for (u32 i = 0; i < edits->chunk_count; i++)
    if (edits->chunks[i].count) n++;
  WcSaveChunk *chunks = blob_builder_array(&bb, &root->chunks, WcSaveChunk, n);
  u32 k = 0;
  for (u32 i = 0; i < edits->chunk_count; i++) {
    const WcChunkEdits *ce = &edits->chunks[i];
    if (!ce->count) continue;
    chunks[k].cx = ce->cx;
    chunks[k].cz = ce->cz;
    WcSaveEdit *e = blob_builder_array(&bb, &chunks[k].edits, WcSaveEdit, ce->count);
    for (u32 j = 0; j < ce->count; j++) e[j] = (WcSaveEdit){.index = ce->items[j].index, .id = ce->items[j].id};
    k++;
  }
  wc_fill_header(&root->header, TYPE_HASH(WcWorldSave), bb.size);
  // the builder rewinds its own temp arena in finish: the output lives in the other one
  TempAllocator tmp = tctx_temp_allocator_begin(bb.temp.arena);
  BlobBuilderResult(u8) out;
  blob_builder_finish(&bb, u8, &tmp.allocator, &out);
  b32 ok = wc_write_atomic(path, out.data, out.size);
  tctx_temp_allocator_end(tmp);
  return ok;
}

static_assert(WC_CHUNK_VOLUME == 65536, "WcSaveEdit.index is a u16 that must address exactly one chunk");

b32 wc_world_save_read(const u8 *buf, u32 len, WcWorldMeta *meta, u8 *hotbar_out, WcEditStore *edits) {
  if (!wc_blob_valid(buf, len, TYPE_HASH(WcWorldSave))) return false;
  WcWorldSave *save = (WcWorldSave *)buf;
  if (blob_array_len(&save->hotbar) != WC_HOTBAR_SLOTS) return false;
  *meta = (WcWorldMeta){.seed = save->seed, .has_player = save->has_player, .pos = {save->px, save->py, save->pz},
                        .yaw = save->yaw, .pitch = save->pitch, .flying = save->flying, .day_time = save->day_time};
  const u8 *hb = (const u8 *)blob_array_get_void(&save->hotbar);
  for (u32 i = 0; i < WC_HOTBAR_SLOTS; i++) hotbar_out[i] = hb[i] < B_COUNT && hb[i] != B_AIR ? hb[i] : B_STONE;
  wc_edits_clear(edits);
  u32 nc = blob_array_len(&save->chunks);
  WcSaveChunk *chunks = (WcSaveChunk *)blob_array_get_void(&save->chunks);
  for (u32 i = 0; i < nc; i++) {
    u32 ne = blob_array_len(&chunks[i].edits);
    const WcSaveEdit *e = (const WcSaveEdit *)blob_array_get_void(&chunks[i].edits);
    for (u32 j = 0; j < ne; j++) {
      if (e[j].id >= B_COUNT) continue;
      wc_edits_record(edits, chunks[i].cx, chunks[i].cz, e[j].index, e[j].id);
    }
  }
  edits->dirty = false;
  return true;
}
