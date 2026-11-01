#include "voxaria/vx_game.h"

// world loot: what pots and generated chests hold, by depth and biome, and the stars that land at night

hz_internal u32 vx_lt_rgba(u32 rgb, f32 a) { return ((rgb >> 16) & 255) | (rgb & 0xFF00) | ((rgb & 255) << 16) | (u32)(a * 255.0f) << 24; }

force_inline b32 vx_lt_night(const VxGame *g) { return g->day_time > 0.56f && g->day_time < 0.94f; }

// a pot breaks into clay sherds and spills coins, sometimes torches, arrows, glowsticks or a potion; the deeper, the richer
void vx_loot_pot(VxGame *g, i32 x, i32 y, i32 z) {
  Random rng = random_create(vx_hash3i(x, y, z, g->seed ^ 0x9077u));
  VxV3d at = {x + 0.5, y + 0.4, z + 0.5};
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  u32 *clay = ALLOC_ARRAY_NO_ZERO(&tmp.allocator, u32, 3);
  clay[0] = 0xC8774E, clay[1] = 0x8E4A30, clay[2] = 0xF0D08A;
  vx_entities_debris(&g->ents, at, (v3){0, 3.0f, 0}, 3.5f, 12, 0.17f, clay, 3);
  tctx_temp_allocator_end(tmp);
  b32 under = y < VX_UNDERGROUND_Y, hell = y < VX_CAVERN_Y;
  u8 biome = vx_biome_at(&g->world.terrain, x, y, z);
  u32 copper = hell ? random_u32_range(&rng, 200, 600) : under ? random_u32_range(&rng, 50, 200) : random_u32_range(&rng, 15, 60);
  if (copper / 100) vx_entities_spawn_drop(&g->ents, at, I_SILVER_COIN, copper / 100);
  if (copper % 100) vx_entities_spawn_drop(&g->ents, at, I_COPPER_COIN, copper % 100);
  f32 r = random_f32(&rng);
  if (r < 0.12f) vx_entities_spawn_drop(&g->ents, at, I_HEALING_POTION, 1);
  else if (r < 0.4f) vx_entities_spawn_drop(&g->ents, at, biome == VX_BIOME_SNOW ? B_ICE_TORCH : B_TORCH, random_u32_range(&rng, 4, 12));
  else if (r < 0.58f) vx_entities_spawn_drop(&g->ents, at, I_WOODEN_ARROW, random_u32_range(&rng, 10, 20));
  else if (r < 0.72f && under) vx_entities_spawn_drop(&g->ents, at, I_GLOWSTICK, random_u32_range(&rng, 4, 12));
  if (g->scripted) log_info("loot: pot at % % % broke with % copper", fmt_i32(x), fmt_i32(y), fmt_i32(z), fmt_u32(copper));
}

hz_internal void vx_lt_put(VxChest *c, u32 *slot, VxItemId item, u32 count) {
  if (*slot >= VX_CHEST_SLOTS || !item || !count) return;
  c->slot[(*slot)++] = (VxStack){item, (u16)count};
}

// Terraria's chest scheme: one prized item by chest and depth, then bars, potions, light, ammo, gems and coins
void vx_loot_chest_fill(VxGame *g, VxChest *c, u8 id) {
  Random rng = random_create(vx_hash3i(c->x, c->y, c->z, g->seed ^ 0xC4E57u));
  b32 gold = id == B_GOLD_CHEST, under = c->y < VX_UNDERGROUND_Y;
  u8 biome = vx_biome_at(&g->world.terrain, c->x, c->y, c->z);
  u32 n = 0;
  // only the sky huts stand on sunplate, and a placed chest is never filled here, so this is a sky-island chest
  b32 sky = gold && vx_world_block(&g->world, c->x, c->y - 1, c->z) == B_SUNPLATE;
  if (sky) {
    vx_lt_put(c, &n, I_ANGEL_WINGS, 1);
  } else if (gold) {
    hz_internal const VxItemId prize[] = {I_MAGIC_MIRROR, I_CLOUD_BOTTLE, I_HERMES_BOOTS, I_GRAPPLING_HOOK, I_ENCHANTED_SWORD, I_NIGHTS_EDGE};
    vx_lt_put(c, &n, prize[random_u32_range(&rng, 0, ARRAY_SIZE(prize) - 1)], 1);
  } else if (under) {
    hz_internal const VxItemId prize[] = {I_MAGIC_MIRROR, I_CLOUD_BOTTLE, I_HERMES_BOOTS, I_WAND_OF_SPARKING, I_MINING_HELMET, I_ICE_BLADE};
    vx_lt_put(c, &n, prize[random_u32_range(&rng, 0, ARRAY_SIZE(prize) - 1)], 1);
  } else {
    hz_internal const VxItemId prize[] = {I_WAND_OF_SPARKING, I_WOODEN_BOW, I_COPPER_BROADSWORD};
    VxItemId p = prize[random_u32_range(&rng, 0, ARRAY_SIZE(prize) - 1)];
    vx_lt_put(c, &n, p, 1);
    if (p == I_WOODEN_BOW) vx_lt_put(c, &n, I_WOODEN_ARROW, random_u32_range(&rng, 30, 60));
  }
  if (random_f32(&rng) < 0.6f) {
    VxItemId bar = gold ? (random_f32(&rng) < 0.5f ? I_SILVER_BAR : I_GOLD_BAR)
                 : under ? (random_f32(&rng) < 0.5f ? I_IRON_BAR : I_SILVER_BAR)
                         : (random_f32(&rng) < 0.5f ? I_COPPER_BAR : I_TIN_BAR);
    vx_lt_put(c, &n, bar, random_u32_range(&rng, 3, 10));
  }
  if (random_f32(&rng) < 0.5f) vx_lt_put(c, &n, I_HEALING_POTION, random_u32_range(&rng, 1, gold ? 4 : 2));
  if (random_f32(&rng) < 0.6f) vx_lt_put(c, &n, biome == VX_BIOME_SNOW ? B_ICE_TORCH : B_TORCH, random_u32_range(&rng, 10, 20));
  if (random_f32(&rng) < 0.4f) vx_lt_put(c, &n, I_WOODEN_ARROW, random_u32_range(&rng, 25, 50));
  if (under && random_f32(&rng) < 0.4f) vx_lt_put(c, &n, I_GLOWSTICK, random_u32_range(&rng, 10, 20));
  if (under && random_f32(&rng) < 0.35f) {
    // the gem the biome's stone carries most
    VxItemId gem = biome == VX_BIOME_CORRUPTION ? I_AMETHYST_GEM
                 : biome == VX_BIOME_SNOW       ? I_SAPPHIRE_GEM
                 : biome == VX_BIOME_JUNGLE     ? I_EMERALD_GEM
                 : biome == VX_BIOME_DESERT     ? I_RUBY_GEM
                                                : I_AMETHYST_GEM + random_u32_range(&rng, 0, 3);
    vx_lt_put(c, &n, gem, random_u32_range(&rng, 1, 3));
  }
  u32 silver = gold ? random_u32_range(&rng, 20, 60) : under ? random_u32_range(&rng, 5, 15) : random_u32_range(&rng, 1, 4);
  vx_lt_put(c, &n, I_SILVER_COIN, silver);
  vx_lt_put(c, &n, I_COPPER_COIN, random_u32_range(&rng, 10, 99));
  if (g->scripted) {
    log_info("loot: % at % % % (%) filled with % stacks", fmt_cstr(sky ? "sky chest" : gold ? "gold chest" : "chest"), fmt_i32(c->x), fmt_i32(c->y),
             fmt_i32(c->z), fmt_cstr(vx_biome_name[biome]), fmt_u32(n));
    for (u32 i = 0; i < n; i++) log_info("loot:   % x %", fmt_u32(c->slot[i].count), fmt_cstr(vx_item(c->slot[i].item)->name));
  }
}

// the first ground cell at or below from_y in the column, past tree crowns and trunks, or -1
hz_internal i32 vx_lt_ground(const VxWorld *w, i32 x, i32 z, i32 from_y) {
  for (i32 y = MIN(from_y, VX_HEIGHT - 1); y > 0; y--) {
    u8 b = vx_world_block(w, x, y, z);
    if ((vx_solid[b] == 1 && !vx_is_leaves(b) && !vx_is_log(b)) || vx_shape[b] == VX_SHAPE_LIQUID) return y;
  }
  return -1;
}

hz_internal void vx_lt_land_star(VxGame *g, f64 x, f64 z, v3 heading) {
  i32 gy = vx_lt_ground(&g->world, vx_floor_i(x), vx_floor_i(z), VX_HEIGHT - 1);
  if (gy < 0) return;
  if (g->scripted) log_info("loot: a fallen star is coming down at % % %", fmt_f32_p((f32)x, 1), fmt_i32(gy + 1), fmt_f32_p((f32)z, 1));
  vx_combat_star(g, (VxV3d){x, gy + 1.2, z}, heading);
}

void vx_loot_update(VxGame *g, f32 dt) {
  if (g->landstar_forced > 0) {
    g->landstar_forced -= dt;
    if (g->landstar_forced <= 0) {
      f32 fx = -m_sinf(g->player.yaw), fz = -m_cosf(g->player.yaw);
      // it crosses the view from left to right, so the whole fall is in front of the camera
      vx_lt_land_star(g, g->player.pos.x + fx * 9.0, g->player.pos.z + fz * 9.0, (v3){-fz, 0, fx});
    }
  }
  b32 night = vx_lt_night(g);
  // at night, while the player is under the open sky, a star lands somewhere near every half minute or so
  if (night && g->player.pos.y > VX_UNDERGROUND_Y - 8) {
    g->landstar_timer -= dt;
    if (g->landstar_timer <= 0) {
      g->landstar_timer = 20.0f + vx_grand(g) * 40.0f;
      f32 a = vx_grand(g) * PI * 2, d = 14.0f + vx_grand(g) * 30.0f;
      vx_lt_land_star(g, g->player.pos.x + m_cosf(a) * d, g->player.pos.z + m_sinf(a) * d, (v3){0});
    }
  } else if (g->landstar_timer <= 0) {
    g->landstar_timer = 10.0f + vx_grand(g) * 20.0f;
  }
  // fallen stars twinkle where they lie, and fade with the dawn
  VxEntities *e = &g->ents;
  u32 kept = 0;
  for (u32 i = 0; i < e->drop_count; i++) {
    VxDrop d = e->drops[i];
    if (d.item == I_FALLEN_STAR) {
      VxV3d p = {d.p.x, d.p.y + 0.4, d.p.z};
      if (!night) {
        vx_entities_burst(e, VX_MOTE_SPARK, p, (v3){0, 1.5f, 0}, 1.5f, 10, 0.7f, 0.1f, vx_lt_rgba(0xFFE070, 1));
        if (g->scripted) log_info("loot: a fallen star faded at dawn");
        continue;
      }
      if (vx_grand(g) < dt * 6.0f) vx_entities_burst(e, VX_MOTE_SPARK, p, (v3){0, 1.2f, 0}, 0.9f, 1, 0.8f, 0.09f, vx_lt_rgba(0xFFF0A0, 1));
    }
    e->drops[kept++] = d;
  }
  e->drop_count = kept;
}
