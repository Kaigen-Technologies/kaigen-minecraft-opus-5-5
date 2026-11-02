#include "voxaria/vx_game.h"

// using the held item: mining and chopping with accumulated damage, placing, consumables; drops and pickup text

#define VX_TILE_HEAL_TIME 5.0f // seconds a damaged block waits before it heals
#define VX_FELL_MAX 640        // logs one felled tree may hold
#define VX_LEAF_REACH 4        // leaves this close to a remaining log stay

void vx_world_text_add(VxGame *g, VxWorldTextKind kind, VxV3d p, VxItemId item, u32 amount) {
  // a pickup of the same item while its text is fresh adds to it, as Terraria's does
  if (kind == VX_WT_PICKUP) {
    for (u32 i = 0; i < g->text_count; i++) {
      VxWorldText *t = &g->texts[i];
      if (t->kind == VX_WT_PICKUP && t->item == item && t->t < VX_WORLD_TEXT_LIFE * 0.6f) {
        t->amount += amount;
        t->t = 0;
        t->p = p;
        return;
      }
    }
  }
  if (g->text_count == VX_MAX_WORLD_TEXT) {
    mem_move(g->texts, g->texts + 1, sizeof(VxWorldText) * (VX_MAX_WORLD_TEXT - 1));
    g->text_count--;
  }
  g->texts[g->text_count++] = (VxWorldText){.p = p, .kind = (u8)kind, .item = item, .amount = amount};
}

hz_internal void vx_world_text_update(VxGame *g, f32 dt) {
  u32 kept = 0;
  for (u32 i = 0; i < g->text_count; i++) {
    VxWorldText t = g->texts[i];
    t.t += dt;
    t.p.y += dt * 0.8;
    if (t.t < VX_WORLD_TEXT_LIFE) g->texts[kept++] = t;
  }
  g->text_count = kept;
}

hz_internal void vx_drop_block_item(VxGame *g, u8 id, i32 x, i32 y, i32 z) {
  if (id == B_POT) vx_loot_pot(g, x, y, z);
  VxItemId item = vx_block_drop(id);
  if (item) vx_entities_spawn_drop(&g->ents, (VxV3d){x + 0.5, y + 0.4, z + 0.5}, item, 1);
}

// ---- furniture ----

// the step along a piece's width for its quarter turn: +x, +z, -x, -z
force_inline void vx_furn_step(u32 rot, i32 *dx, i32 *dz) {
  *dx = rot == 0 ? 1 : rot == 2 ? -1 : 0;
  *dz = rot == 1 ? 1 : rot == 3 ? -1 : 0;
}

// the cell of part 0 of the piece a furniture cell belongs to
hz_internal void vx_furn_origin(u8 id, u8 state, i32 x, i32 y, i32 z, i32 *ox, i32 *oy, i32 *oz) {
  u32 part = VX_FURN_PART(state), w = vx_furniture[id].w;
  i32 dx, dz;
  vx_furn_step(state & 3, &dx, &dz);
  *ox = x - dx * (i32)(part % w);
  *oy = y - (i32)(part / w);
  *oz = z - dz * (i32)(part % w);
}

// a door's two blocks are one piece
force_inline b32 vx_same_piece(u8 a, u8 b) { return a == b || (vx_is_door(a) && vx_is_door(b)); }

hz_internal void vx_break_furniture(VxGame *g, i32 x, i32 y, i32 z) {
  VxWorld *w = &g->world;
  u8 id = vx_world_block(w, x, y, z), state = vx_world_state(w, x, y, z);
  i32 ox, oy, oz, dx, dz;
  vx_furn_origin(id, state, x, y, z, &ox, &oy, &oz);
  vx_furn_step(state & 3, &dx, &dz);
  VxFurniture fu = vx_furniture[id];
  for (u32 py = 0; py < fu.h; py++)
    for (u32 px = 0; px < fu.w; px++) {
      i32 cx = ox + dx * (i32)px, cy = oy + (i32)py, cz = oz + dz * (i32)px;
      u8 c = vx_world_block(w, cx, cy, cz);
      if (!vx_same_piece(c, id) || VX_FURN_PART(vx_world_state(w, cx, cy, cz)) != px + py * fu.w) continue;
      vx_world_set_block(w, cx, cy, cz, B_AIR, true);
      vx_entities_dust(&g->ents, c, cx, cy, cz, 8);
    }
  vx_drop_block_item(g, id, ox, oy, oz);
  vx_audio_break(&g->audio, id);
}

// a piece from the held stack, its front turned to the player and centred on the target cell; true if placed
hz_internal b32 vx_place_furniture(VxGame *g, i32 x, i32 y, i32 z, u8 id) {
  VxWorld *w = &g->world;
  VxFurniture fu = vx_furniture[id];
  f32 fx = -m_sinf(g->player.yaw), fz = -m_cosf(g->player.yaw);
  u32 rot = m_absf(fz) >= m_absf(fx) ? (fz < 0 ? 0u : 2u) : (fx > 0 ? 1u : 3u);
  i32 dx, dz;
  vx_furn_step(rot, &dx, &dz);
  i32 ox = x - dx * (i32)((fu.w - 1) / 2), oz = z - dz * (i32)((fu.w - 1) / 2);
  for (u32 py = 0; py < fu.h; py++)
    for (u32 px = 0; px < fu.w; px++) {
      i32 cx = ox + dx * (i32)px, cy = y + (i32)py, cz = oz + dz * (i32)px;
      if (cy < 1 || cy >= VX_HEIGHT) return false;
      u8 cur = vx_world_block(w, cx, cy, cz);
      if (cur != B_AIR && (!vx_replaceable[cur] || vx_shape[cur] == VX_SHAPE_LIQUID)) return false;
      if (vx_solid[id] && vx_player_intersects_block(&g->player, cx, cy, cz)) return false;
      // the bottom row stands on solid blocks; platforms hang anywhere
      if (py == 0 && id != B_PLATFORM && !vx_occludes[vx_world_block(w, cx, cy - 1, cz)]) return false;
    }
  for (u32 py = 0; py < fu.h; py++)
    for (u32 px = 0; px < fu.w; px++)
      vx_world_set_block_state(w, ox + dx * (i32)px, y + (i32)py, oz + dz * (i32)px, id,
                               (u8)(rot | (px + py * fu.w) << 2), true);
  vx_audio_place(&g->audio, id);
  return true;
}

// a door swings open or shut as one piece; it will not shut on the player
hz_internal b32 vx_toggle_door(VxGame *g, i32 x, i32 y, i32 z) {
  VxWorld *w = &g->world;
  u8 id = vx_world_block(w, x, y, z), state = vx_world_state(w, x, y, z);
  u8 to = id == B_DOOR ? B_DOOR_OPEN : B_DOOR;
  i32 ox, oy, oz;
  vx_furn_origin(id, state, x, y, z, &ox, &oy, &oz);
  for (i32 py = 0; py < 3; py++)
    if (to == B_DOOR && vx_player_intersects_block(&g->player, ox, oy + py, oz)) return false;
  for (i32 py = 0; py < 3; py++) {
    if (!vx_is_door(vx_world_block(w, ox, oy + py, oz))) continue;
    vx_world_set_block_state(w, ox, oy + py, oz, to, vx_world_state(w, ox, oy + py, oz), true);
  }
  vx_audio_play(&g->audio, to == B_DOOR ? VX_SND_DOOR_CLOSE : VX_SND_DOOR_OPEN);
  if (g->scripted) log_info("building: door %", fmt_cstr(to == B_DOOR ? "closed" : "opened"));
  return true;
}

// ---- chests ----

VxChest *vx_chest_at(VxGame *g, i32 x, i32 y, i32 z, b32 create) {
  for (u32 i = 0; i < g->chest_count; i++)
    if (g->chests[i].x == x && g->chests[i].y == y && g->chests[i].z == z) return &g->chests[i];
  if (!create || g->chest_count == VX_MAX_CHESTS) return NULL;
  VxChest *c = &g->chests[g->chest_count++];
  mem_zero(c, sizeof(*c));
  c->x = x, c->y = y, c->z = z;
  return c;
}

// a chest the world generated has no record until first touched: it gets one, filled with its loot; placed chests get theirs on placement
hz_internal VxChest *vx_world_chest(VxGame *g, i32 x, i32 y, i32 z, u8 id) {
  VxChest *c = vx_chest_at(g, x, y, z, false);
  if (c) return c;
  c = vx_chest_at(g, x, y, z, true);
  if (c) vx_loot_chest_fill(g, c, id);
  return c;
}

hz_internal b32 vx_chest_empty(const VxChest *c) {
  for (u32 i = 0; i < VX_CHEST_SLOTS; i++)
    if (c->slot[i].item) return false;
  return true;
}

void vx_chest_close(VxGame *g) { g->open_chest = -1; }

void vx_chest_loot_all(VxGame *g) {
  if (g->open_chest < 0) return;
  VxChest *c = &g->chests[g->open_chest];
  for (u32 i = 0; i < VX_CHEST_SLOTS; i++) {
    VxStack *s = &c->slot[i];
    if (!s->item) continue;
    u32 had = s->count;
    s->count = (u16)vx_inventory_add(&g->inv, s->item, s->count);
    if (g->scripted) log_info("chest: took % x %", fmt_u32(had - s->count), fmt_cstr(vx_item(s->item)->name));
    if (!s->count) s->item = 0;
  }
}

hz_internal void vx_chest_remove(VxGame *g, VxChest *c) {
  vx_chest_close(g);
  *c = g->chests[--g->chest_count];
}

b32 vx_interact(VxGame *g, VxRayHit h) {
  // a townsperson in front of the block is talked to first
  if (vx_creature_talk(g, g->cam.pos, h.dir)) return true;
  if (!h.hit || g->vitals.dead) return false;
  if (vx_is_door(h.id)) return vx_toggle_door(g, h.x, h.y, h.z);
  if (h.id == B_CHEST || h.id == B_GOLD_CHEST) {
    VxChest *c = vx_world_chest(g, h.x, h.y, h.z, h.id);
    if (!c) return false;
    g->open_chest = (i32)(c - g->chests);
    vx_game_set_mode(g, VX_MODE_INVENTORY);
    vx_audio_play(&g->audio, VX_SND_CHEST);
    if (g->scripted) log_info("building: chest opened at % % %", fmt_i32(h.x), fmt_i32(h.y), fmt_i32(h.z));
    return true;
  }
  return false;
}

// removes a block, with its dust, its drop and whatever it was holding up
void vx_break_block(VxGame *g, VxRayHit h) {
  // the world floor holds the underworld up
  if (h.y <= 0) return;
  VxWorld *w = &g->world;
  if (vx_is_furniture(h.id)) {
    vx_break_furniture(g, h.x, h.y, h.z);
  } else {
    // a chest with something in it stays, as in Terraria
    VxChest *c = h.id == B_CHEST || h.id == B_GOLD_CHEST ? vx_world_chest(g, h.x, h.y, h.z, h.id) : NULL;
    if (c && !vx_chest_empty(c)) {
      if (g->scripted) log_info("building: chest at % % % holds items and stays", fmt_i32(h.x), fmt_i32(h.y), fmt_i32(h.z));
      return;
    }
    if (!vx_world_set_block(w, h.x, h.y, h.z, B_AIR, true)) return;
    if (c) vx_chest_remove(g, c);
    vx_entities_spawn_break(&g->ents, h.id, h.x, h.y, h.z);
    vx_drop_block_item(g, h.id, h.x, h.y, h.z);
    vx_audio_break(&g->audio, h.id);
  }
  // plants, torches, furniture and cactus need the block below them
  for (i32 y = h.y + 1; y < VX_HEIGHT; y++) {
    u8 above = vx_world_block(w, h.x, y, h.z);
    if (vx_is_furniture(above)) {
      if (above != B_PLATFORM && VX_FURN_PART(vx_world_state(w, h.x, y, h.z)) < vx_furniture[above].w)
        vx_break_furniture(g, h.x, y, h.z);
      break;
    }
    if (!vx_needs_support(above) && above != B_CACTUS) break;
    // a chest with something in it holds on
    if ((above == B_CHEST || above == B_GOLD_CHEST)) {
      VxChest *c = vx_world_chest(g, h.x, y, h.z, above);
      if (c && !vx_chest_empty(c)) break;
      if (c) vx_chest_remove(g, c);
    }
    vx_world_set_block(w, h.x, y, h.z, B_AIR, true);
    vx_drop_block_item(g, above, h.x, y, h.z);
  }
  vx_liquids_touch(&g->liquids, w, h.x, h.y, h.z);
}

// a log and every log above it that connects to it, then the leaves no log holds any more
hz_internal void vx_fell_tree(VxGame *g, i32 x0, i32 y0, i32 z0) {
  VxWorld *w = &g->world;
  TempAllocator tmp = tctx_temp_allocator_begin(NULL);
  i32 *q = ALLOC_ARRAY(&tmp.allocator, i32, VX_FELL_MAX * 3);
  u32 head = 0, n = 0;
  q[0] = x0, q[1] = y0, q[2] = z0;
  n = 1;
  i32 bx0 = x0, bx1 = x0, by1 = y0, bz0 = z0, bz1 = z0;
  while (head < n) {
    i32 x = q[head * 3], y = q[head * 3 + 1], z = q[head * 3 + 2];
    head++;
    u8 id = vx_world_block(w, x, y, z);
    if (!vx_is_log(id) || !vx_world_set_block(w, x, y, z, B_AIR, true)) continue;
    vx_entities_dust(&g->ents, id, x, y, z, 6);
    vx_drop_block_item(g, id, x, y, z);
    bx0 = MIN(bx0, x), bx1 = MAX(bx1, x), by1 = MAX(by1, y), bz0 = MIN(bz0, z), bz1 = MAX(bz1, z);
    // branches leave the trunk diagonally, so the search takes all 26 neighbours that are not below the cut
    for (i32 dy = 0; dy <= 1; dy++)
      for (i32 dz = -1; dz <= 1; dz++)
        for (i32 dx = -1; dx <= 1; dx++) {
          if ((dx | dy | dz) == 0 || n == VX_FELL_MAX) continue;
          i32 nx = x + dx, ny = y + dy, nz = z + dz;
          if (ny < y0 || !vx_is_log(vx_world_block(w, nx, ny, nz))) continue;
          b32 queued = false;
          for (u32 k = head; k < n && !queued; k++)
            queued = q[k * 3] == nx && q[k * 3 + 1] == ny && q[k * 3 + 2] == nz;
          if (queued) continue;
          q[n * 3] = nx, q[n * 3 + 1] = ny, q[n * 3 + 2] = nz;
          n++;
        }
  }
  // leaves around the felled logs fall unless another log still holds them
  i32 R = VX_LEAF_REACH;
  for (i32 y = y0 - 1; y <= by1 + R; y++)
    for (i32 z = bz0 - R; z <= bz1 + R; z++)
      for (i32 x = bx0 - R; x <= bx1 + R; x++) {
        u8 id = vx_world_block(w, x, y, z);
        if (!vx_is_leaves(id)) continue;
        b32 held = false;
        for (i32 dy = -R; dy <= R && !held; dy++)
          for (i32 dz = -R; dz <= R && !held; dz++)
            for (i32 dx = -R; dx <= R && !held; dx++)
              held = vx_is_log(vx_world_block(w, x + dx, y + dy, z + dz));
        if (held) continue;
        if (vx_world_set_block(w, x, y, z, B_AIR, true) && ((x ^ y ^ z) & 3) == 0)
          vx_entities_dust(&g->ents, id, x, y, z, 3);
      }
  vx_audio_play(&g->audio, VX_SND_TREE_FALL);
  if (g->scripted) log_info("mining: felled a tree of % logs", fmt_u32(n));
  tctx_temp_allocator_end(tmp);
}

hz_internal VxTileHit *vx_tile_hit(VxGame *g, i32 x, i32 y, i32 z) {
  for (u32 i = 0; i < g->tile_hit_count; i++) {
    VxTileHit *t = &g->tile_hits[i];
    if (t->x == x && t->y == y && t->z == z) return t;
  }
  if (g->tile_hit_count == VX_TILE_HITS) {
    mem_move(g->tile_hits, g->tile_hits + 1, sizeof(VxTileHit) * (VX_TILE_HITS - 1));
    g->tile_hit_count--;
  }
  VxTileHit *t = &g->tile_hits[g->tile_hit_count++];
  *t = (VxTileHit){.x = x, .y = y, .z = z};
  return t;
}

hz_internal void vx_tile_hits_update(VxGame *g, f32 dt) {
  u32 kept = 0;
  for (u32 i = 0; i < g->tile_hit_count; i++) {
    VxTileHit t = g->tile_hits[i];
    t.idle += dt;
    if (t.idle < VX_TILE_HEAL_TIME && t.damage > 0 && vx_world_block(&g->world, t.x, t.y, t.z) != B_AIR)
      g->tile_hits[kept++] = t;
  }
  g->tile_hit_count = kept;
}

// one swing of a pickaxe or axe into a block: damage builds until it breaks
hz_internal void vx_hit_tile(VxGame *g, VxRayHit h, const VxItemInfo *tool) {
  b32 axe = tool->kind == VX_IK_AXE;
  b32 chops = vx_is_log(h.id) || h.id == B_CACTUS;
  // pickaxes leave trees alone and axes only cut wood; liquids are never mined
  if (vx_shape[h.id] == VX_SHAPE_LIQUID || axe != chops) return;
  u32 need = vx_block_pick_power(h.id);
  if (!axe && tool->power < need) {
    vx_entities_dust(&g->ents, h.id, h.x, h.y, h.z, 2);
    if (g->scripted) log_info("mining: % needs % pick power", fmt_cstr(vx_item(h.id)->name), fmt_u32(need));
    return;
  }
  VxTileHit *t = vx_tile_hit(g, h.x, h.y, h.z);
  // plants, torches and pots go in one hit
  f32 hit = vx_needs_support(h.id) || vx_is_furniture(h.id) ? 100.0f : (f32)tool->power * (f32)vx_block_mine_rate(h.id) / 100.0f;
  t->damage += hit;
  t->idle = 0;
  vx_entities_dust(&g->ents, h.id, h.x, h.y, h.z, 4);
  vx_audio_dig(&g->audio, h.id);
  if (t->damage < 100) return;
  t->damage = 0;
  if (axe && vx_is_log(h.id)) vx_fell_tree(g, h.x, h.y, h.z);
  else vx_break_block(g, h);
}

// a block from the held stack onto the face that was clicked; returns whether it was placed
hz_internal b32 vx_place_block(VxGame *g, VxRayHit h, u8 id) {
  i32 x = h.x + h.nx, y = h.y + h.ny, z = h.z + h.nz;
  // clicking a replaceable block (grass, flowers) replaces it directly
  if (vx_replaceable[h.id] && h.id != B_WATER) {
    x = h.x;
    y = h.y;
    z = h.z;
  }
  if (y < 0 || y >= VX_HEIGHT) return false;
  if (vx_is_furniture(id)) return vx_place_furniture(g, x, y, z, id);
  // a placed chest starts empty, so it needs its record before it exists
  b32 chest = id == B_CHEST || id == B_GOLD_CHEST;
  if (chest && g->chest_count == VX_MAX_CHESTS) return false;
  VxWorld *w = &g->world;
  u8 cur = vx_world_block(w, x, y, z);
  if (cur != B_AIR && !vx_replaceable[cur]) return false;
  if (vx_solid[id] && vx_player_intersects_block(&g->player, x, y, z)) return false;
  // a torch clicked onto a wall mounts there; a model faces the player
  u8 state = 0;
  if (vx_shape[id] == VX_SHAPE_TORCH && h.ny == 0 && vx_occludes[h.id]) {
    state = h.nx < 0 ? VX_SIDE_PX : h.nx > 0 ? VX_SIDE_NX : h.nz < 0 ? VX_SIDE_PZ : VX_SIDE_NZ;
  } else if (vx_shape[id] == VX_SHAPE_MODEL) {
    f32 fx = -m_sinf(g->player.yaw), fz = -m_cosf(g->player.yaw);
    // the front (+z unturned) points back at the player
    if (m_absf(fz) >= m_absf(fx)) state = fz < 0 ? 0 : 2;
    else state = fx > 0 ? 1 : 3;
  }
  if (vx_needs_support(id) && state == 0) {
    u8 below = vx_world_block(w, x, y - 1, z);
    if (!vx_occludes[below] && below != id && below != B_CACTUS) return false;
  }
  if (!vx_world_set_block_state(w, x, y, z, id, state, true)) return false;
  if (chest) vx_chest_at(g, x, y, z, true);
  vx_audio_place(&g->audio, id);
  vx_liquids_touch(&g->liquids, w, x, y, z);
  return true;
}

hz_internal void vx_consume_held(VxGame *g) {
  VxStack *s = vx_inventory_held(&g->inv);
  if (s->count > 0 && --s->count == 0) s->item = 0;
}

// consumables and tools that act on the player; returns whether the item was used
hz_internal b32 vx_use_self(VxGame *g, VxItemId item) {
  VxVitals *v = &g->vitals;
  const VxItemInfo *it = vx_item(item);
  VxV3d head = {g->player.pos.x, g->player.pos.y + 2.9, g->player.pos.z};
  switch (item) {
  case I_LIFE_CRYSTAL_ITEM:
    if (v->hp_max >= 400) return false;
    vx_audio_play(&g->audio, VX_SND_SHIMMER);
    v->hp_max += it->power;
    v->hp = m_minf(v->hp_max, v->hp + it->power);
    vx_world_text_add(g, VX_WT_HEAL, head, 0, it->power);
    if (g->scripted) log_info("vitals: Life Crystal raised max life to %", fmt_f32_p(v->hp_max, 0));
    return true;
  case I_MANA_CRYSTAL:
    if (v->mana_max >= 200) return false;
    vx_audio_play(&g->audio, VX_SND_SHIMMER);
    v->mana_max += it->power;
    v->mana = m_minf(v->mana_max, v->mana + it->power);
    if (g->scripted) log_info("vitals: Mana Crystal raised max mana to %", fmt_f32_p(v->mana_max, 0));
    return true;
  case I_HEALING_POTION:
    if (v->potion_cd > 0 || v->hp >= v->hp_max) return false;
    v->hp = m_minf(v->hp_max, v->hp + it->power);
    v->potion_cd = 60.0f;
    vx_audio_play(&g->audio, VX_SND_DRINK);
    vx_world_text_add(g, VX_WT_HEAL, head, 0, it->power);
    return true;
  case I_MAGIC_MIRROR:
    vx_audio_play(&g->audio, VX_SND_SHIMMER);
    vx_entities_cloud_puff(&g->ents, g->player.pos);
    g->player.pos = g->spawn_pos;
    g->player.vel = (v3){0, 0, 0};
    g->player.fall_peak = g->player.pos.y;
    g->player.hook_state = VX_HOOK_NONE;
    vx_entities_cloud_puff(&g->ents, g->player.pos);
    vx_render_invalidate_history(&g->renderer);
    return true;
  default: return false;
  }
}

b32 vx_quick_heal(VxGame *g) {
  if (g->vitals.dead || !vx_inventory_count(&g->inv, I_HEALING_POTION) || !vx_use_self(g, I_HEALING_POTION)) return false;
  vx_inventory_remove(&g->inv, I_HEALING_POTION, 1);
  return true;
}

void vx_event(VxGame *g, String text) { vx_event_rgb(g, text, 0); }

void vx_event_rgb(VxGame *g, String text, u32 rgb) {
  if (g->event_count == VX_EVENTS) {
    mem_move(g->events, g->events + VX_EVENT_LEN, VX_EVENT_LEN * (VX_EVENTS - 1));
    mem_move(g->event_t, g->event_t + 1, sizeof(f32) * (VX_EVENTS - 1));
    mem_move(g->event_rgb, g->event_rgb + 1, sizeof(u32) * (VX_EVENTS - 1));
    g->event_count--;
  }
  g->event_rgb[g->event_count] = rgb;
  u32 n = MIN(text.len, VX_EVENT_LEN - 1u);
  char *line = g->events + g->event_count * VX_EVENT_LEN;
  mem_cpy(line, text.value, n);
  line[n] = 0;
  g->event_t[g->event_count++] = 0;
  if (g->scripted) log_info("event: %", fmt_str(text));
}

void vx_use_item(VxGame *g, b32 held, b32 pressed, VxRayHit h) {
  if (g->vitals.dead || (!held && !pressed) || g->use_cd > 0) return;
  VxStack *s = vx_inventory_held(&g->inv);
  if (!s->item) return;
  const VxItemInfo *it = vx_item(s->item);
  f32 use = it->use_ms > 0 ? (f32)it->use_ms / 1000.0f : 0.25f;
  switch (it->kind) {
  case VX_IK_BLOCK:
    if (!h.hit) {
      if (g->scripted && pressed) log_info("building: % has nothing in reach to go against", fmt_cstr(it->name));
      return;
    }
    g->use_cd = pressed ? 0.18f : 0.12f;
    if (vx_place_block(g, h, (u8)s->item)) {
      vx_entities_trigger_swing(&g->ents, 0.2f);
      vx_consume_held(g);
      // a replaceable plant was taken over in place
      b32 took = vx_replaceable[h.id] && h.id != B_WATER;
      if (g->scripted)
        log_info("building: placed % at % % %", fmt_cstr(it->name), fmt_i32(took ? h.x : h.x + h.nx),
                 fmt_i32(took ? h.y : h.y + h.ny), fmt_i32(took ? h.z : h.z + h.nz));
    } else if (g->scripted && pressed) {
      log_info("building: % refused against % at % % % (face % % %)", fmt_cstr(it->name),
               fmt_cstr(vx_block_label[h.id]), fmt_i32(h.x), fmt_i32(h.y), fmt_i32(h.z), fmt_i32(h.nx), fmt_i32(h.ny),
               fmt_i32(h.nz));
    }
    return;
  case VX_IK_PICKAXE:
  case VX_IK_AXE:
    vx_entities_trigger_swing(&g->ents, use);
    vx_audio_play(&g->audio, VX_SND_SWING);
    g->use_cd = use;
    if (h.hit) vx_hit_tile(g, h, it);
    return;
  case VX_IK_SWORD:
    // the whoosh comes with the strike, not the wind-up
    vx_entities_trigger_swing(&g->ents, use);
    g->use_cd = use;
    vx_combat_swing(g, it, use);
    // blades cut grass and flowers, as in Terraria
    if (h.hit && vx_shape[h.id] == VX_SHAPE_CROSS && vx_replaceable[h.id]) vx_break_block(g, h);
    return;
  case VX_IK_BOW:
  case VX_IK_WAND:
    if (!vx_combat_can_shoot(g, it)) {
      if (pressed) vx_game_toast(g, it->kind == VX_IK_BOW ? "Out of arrows" : "Not enough mana");
      return;
    }
    // the bow is drawn and the wand lifted; the shot leaves at the strike
    g->use_cd = use;
    vx_entities_trigger_swing(&g->ents, use);
    g->ents.puppet.aim_t = use + 0.5f;
    vx_combat_ready(g, it, use);
    return;
  case VX_IK_SUMMON:
    if (!pressed) return;
    g->use_cd = use;
    if (vx_boss_summon(g, VX_CR_EYE_OF_CTHULHU, false)) {
      vx_entities_trigger_swing(&g->ents, 0.3f);
      vx_consume_held(g);
    } else {
      vx_game_toast(g, vx_boss_index(g) >= 0 ? "A boss is already here" : "Nothing happens. It only stirs at night");
    }
    return;
  case VX_IK_CONSUMABLE:
  case VX_IK_TOOL:
    if (!pressed) return;
    g->use_cd = use;
    if (vx_use_self(g, s->item)) {
      vx_entities_trigger_swing(&g->ents, 0.3f);
      if (it->kind == VX_IK_CONSUMABLE) vx_consume_held(g);
    }
    return;
  default:
    vx_entities_trigger_swing(&g->ents, use);
    g->use_cd = use;
    return;
  }
}

// abilities from what is equipped, plus whatever the test tooling forced on
hz_internal void vx_apply_equipment(VxGame *g) {
  VxPlayer *p = &g->player;
  const VxInventory *inv = &g->inv;
  p->has_double_jump = g->forced_double || vx_inventory_has_accessory(inv, I_CLOUD_BOTTLE);
  p->has_hook = g->forced_hook || vx_inventory_has_accessory(inv, I_GRAPPLING_HOOK);
  f32 wings = vx_inventory_has_accessory(inv, I_ANGEL_WINGS) ? 1.7f : g->forced_wings;
  if (wings != p->wing_time_max) p->wing_time = p->wing_time_max = wings;
  p->run_mult = vx_inventory_has_accessory(inv, I_HERMES_BOOTS) ? 1.45f : 1.0f;
}

// crafting stations within reach of the player's body
hz_internal u32 vx_stations_near(const VxWorld *w, VxV3d p) {
  u32 m = 1u << VX_ST_HAND;
  i32 px = vx_floor_i(p.x), py = vx_floor_i(p.y), pz = vx_floor_i(p.z);
  for (i32 y = py - 2; y <= py + 3; y++)
    for (i32 z = pz - 4; z <= pz + 4; z++)
      for (i32 x = px - 4; x <= px + 4; x++) {
        u8 b = vx_world_block(w, x, y, z);
        if (b == B_WORKBENCH) m |= 1u << VX_ST_WORKBENCH;
        else if (b == B_FURNACE) m |= 1u << VX_ST_FURNACE;
        else if (b == B_ANVIL) m |= 1u << VX_ST_ANVIL;
      }
  return m;
}

void vx_use_update(VxGame *g, f32 dt) {
  g->use_cd -= dt;
  g->stations = vx_stations_near(&g->world, g->player.pos);
  // an open chest closes when it is gone or out of reach
  if (g->open_chest >= 0) {
    const VxChest *c = &g->chests[g->open_chest];
    u8 b = vx_world_block(&g->world, c->x, c->y, c->z);
    f64 dx = c->x + 0.5 - g->player.pos.x, dy = c->y + 0.5 - (g->player.pos.y + 1.3), dz = c->z + 0.5 - g->player.pos.z;
    if ((b != B_CHEST && b != B_GOLD_CHEST) || dx * dx + dy * dy + dz * dz > 7.0 * 7.0) vx_chest_close(g);
  }
  vx_apply_equipment(g);
  vx_tile_hits_update(g, dt);
  vx_world_text_update(g, dt);
  for (u32 i = 0; i < g->event_count; i++) g->event_t[i] += dt;
  g->vitals.potion_cd = m_maxf(0, g->vitals.potion_cd - dt);
  VxEntities *e = &g->ents;
  VxPlayer *p = &g->player;
  if (!g->vitals.dead)
    vx_entities_update_drops(e, dt, &g->world, (VxV3d){p->pos.x, p->pos.y + 1.3, p->pos.z}, &g->inv);
  for (u32 i = 0; i < e->pickup_count; i++) {
    vx_world_text_add(g, VX_WT_PICKUP, (VxV3d){p->pos.x, p->pos.y + 2.9, p->pos.z}, e->pickups[i].item,
                      e->pickups[i].count);
    vx_audio_pickup(&g->audio);
    if (g->scripted)
      log_info("items: picked up % x %", fmt_u32(e->pickups[i].count), fmt_cstr(vx_item(e->pickups[i].item)->name));
  }
}

void vx_give_starting_kit(VxGame *g) {
  vx_inventory_clear(&g->inv);
  g->chest_count = 0;
  g->open_chest = -1;
  vx_inventory_add(&g->inv, I_COPPER_SHORTSWORD, 1);
  vx_inventory_add(&g->inv, I_COPPER_PICKAXE, 1);
  vx_inventory_add(&g->inv, I_COPPER_AXE, 1);
}

#undef VX_TILE_HEAL_TIME
#undef VX_FELL_MAX
#undef VX_LEAF_REACH
