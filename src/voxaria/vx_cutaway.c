#include "voxaria/vx_game.h"

// the Terraria view: a side-on camera on the player's slab, everything in front of it cut away, play kept to the plane

#define VX_CUT_DIST 44.0f // camera to the plane: with VX_CUT_FOV it frames about 32 blocks of height
#define VX_CUT_SEEP 0.7f  // light kept per solid tile it seeps through, on the 0..15 scale

void vx_cut_init(VxCut *c, Allocator *alloc) {
  c->light = ALLOC_ARRAY(alloc, u32, VX_CUT_W * VX_HEIGHT);
  c->work = ALLOC_ARRAY_NO_ZERO(alloc, f32, VX_CUT_W * VX_HEIGHT * 4);
  c->facing = 1;
}

void vx_cut_set(VxGame *g, b32 on) {
  VxCut *c = &g->cut;
  VxPlayer *p = &g->player;
  if (c->on == on) return;
  c->on = on;
  if (on) {
    c->z = vx_floor_i(p->pos.z);
    p->pos.z = c->z + 0.5;
    p->vel.z = 0;
    c->view_x = (f32)p->pos.x;
    c->view_y = (f32)p->pos.y + 1.2f;
    c->light_x0 = INT32_MIN;
    if (!g->test_play && !g->touch_mode) os_lock_mouse(false);
  } else if (g->mode == VX_MODE_PLAYING && !g->test_play && !g->touch_mode) {
    os_lock_mouse(true);
  }
  vx_world_set_cut(&g->world, on, c->z);
  vx_render_invalidate_history(&g->renderer);
  if (g->scripted) log_info("cutaway: % at slab z %", fmt_cstr(on ? "on" : "off"), fmt_i32(c->z));
}

// the slab's light: each cell's own voxel light and the held light, then seeping into solid tiles with a loss per tile
hz_internal void vx_cut_light(VxGame *g) {
  VxCut *c = &g->cut;
  const VxWorld *w = &g->world;
  const VxDynLight *d = &g->dyn;
  f32 *L = c->work;
  for (i32 y = 0; y < VX_HEIGHT; y++)
    for (i32 i = 0; i < VX_CUT_W; i++) {
      i32 x = c->light_x0 + i;
      f32 *o = L + (y * VX_CUT_W + i) * 4;
      if (vx_occludes[vx_world_block(w, x, y, c->z)]) {
        o[0] = o[1] = o[2] = o[3] = 0;
        continue;
      }
      u16 v = vx_world_light(w, x, y, c->z);
      o[0] = (f32)(v & 15), o[1] = (f32)((v >> 4) & 15), o[2] = (f32)((v >> 8) & 15);
      o[3] = (f32)(v >> VX_LIGHT_SKY_SHIFT);
      i32 dx = x - d->origin.x, dy = y - d->origin.y, dz = c->z - d->origin.z;
      if (d->lit && dx >= 0 && dy >= 0 && dz >= 0 && dx < VX_DYN_SIZE && dy < VX_DYN_SIZE && dz < VX_DYN_SIZE) {
        u32 t = d->texels[(u32)(dx | (dz << 5) | (dy << 10))];
        o[0] = m_maxf(o[0], (f32)(t & 255) / 17.0f);
        o[1] = m_maxf(o[1], (f32)((t >> 8) & 255) / 17.0f);
        o[2] = m_maxf(o[2], (f32)((t >> 16) & 255) / 17.0f);
      }
    }
  // two raster sweeps carry the strongest decayed light from any 4-neighbour into every solid tile
  for (i32 pass = 0; pass < 2; pass++) {
    i32 s = pass ? -1 : 1;
    for (i32 yy = 0; yy < VX_HEIGHT; yy++) {
      i32 y = pass ? VX_HEIGHT - 1 - yy : yy;
      for (i32 ii = 0; ii < VX_CUT_W; ii++) {
        i32 i = pass ? VX_CUT_W - 1 - ii : ii;
        if (!vx_occludes[vx_world_block(w, c->light_x0 + i, y, c->z)]) continue;
        f32 *o = L + (y * VX_CUT_W + i) * 4;
        i32 pi = i - s, py = y - s;
        const f32 *a = pi >= 0 && pi < VX_CUT_W ? L + (y * VX_CUT_W + pi) * 4 : NULL;
        const f32 *b = py >= 0 && py < VX_HEIGHT ? L + (py * VX_CUT_W + i) * 4 : NULL;
        for (u32 k = 0; k < 4; k++) {
          if (a) o[k] = m_maxf(o[k], a[k] * VX_CUT_SEEP);
          if (b) o[k] = m_maxf(o[k], b[k] * VX_CUT_SEEP);
        }
      }
    }
  }
  for (u32 n = 0; n < VX_CUT_W * VX_HEIGHT; n++) {
    const f32 *o = L + n * 4;
    c->light[n] = (u32)(o[0] * 17.0f) | ((u32)(o[1] * 17.0f) << 8) | ((u32)(o[2] * 17.0f) << 16) | ((u32)(o[3] * 17.0f) << 24);
  }
  c->light_version++;
}

// where a screen point meets the slab's middle, in world blocks
hz_internal b32 vx_cut_screen(const VxGame *g, f32 sx, f32 sy, f64 *wx, f64 *wy) {
  const VxCut *c = &g->cut;
  f32 w = (f32)g->ui->canvas_width, h = (f32)g->ui->canvas_height;
  if (w <= 0 || h <= 0) return false;
  f32 t = m_tanf(VX_CUT_FOV * PI / 360.0f);
  f32 nx = (2 * sx / w - 1) * t * (w / h), ny = (1 - 2 * sy / h) * t;
  // the camera looks straight down -z, so the ray reaches the slab's middle after its distance to it
  f32 dist = VX_CUT_DIST + 0.5f;
  *wx = c->view_x + nx * dist;
  *wy = c->view_y + ny * dist;
  return true;
}

hz_internal b32 vx_cut_cursor(const VxGame *g, f64 *wx, f64 *wy) {
  const VxCut *c = &g->cut;
  if (c->scripted_cursor || (g->touch_mode && c->touch_cursor)) {
    *wx = c->cursor_x, *wy = c->cursor_y;
    return true;
  }
  return vx_cut_screen(g, g->input.mouse_pos.x, g->input.mouse_pos.y, wx, wy);
}

// the tile under the pointer, within reach: itself when it is there to mine or use, else a solid neighbour to build against
hz_internal VxRayHit vx_cut_target(VxGame *g, f64 wx, f64 wy) {
  const VxCut *c = &g->cut;
  i32 x = vx_floor_i(wx), y = vx_floor_i(wy), z = c->z;
  VxV3d o = vx_player_reach_origin(&g->player);
  f64 ex = x + 0.5 - o.x, ey = y + 0.5 - o.y;
  f32 dist = (f32)m_sqrt(ex * ex + ey * ey);
  v3 dir = dist > 0.01f ? (v3){(f32)(ex / dist), (f32)(ey / dist), 0} : (v3){1, 0, 0};
  if (dist > VX_REACH) return (VxRayHit){.dir = dir};
  u8 id = vx_world_block(&g->world, x, y, z);
  if (id != B_AIR && vx_shape[id] != VX_SHAPE_LIQUID)
    return (VxRayHit){.hit = true, .x = x, .y = y, .z = z, .nz = 1, .dist = dist, .id = id, .dir = dir};
  // an empty tile is only a place to build: below first, then the sides, then above
  if (!vx_item_is_block(g->inv.slot[g->inv.selected].item)) return (VxRayHit){.dir = dir};
  hz_internal const i32 VX_CUT_NBR[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
  for (u32 k = 0; k < 4; k++) {
    i32 nx = x + VX_CUT_NBR[k][0], ny = y + VX_CUT_NBR[k][1];
    u8 n = vx_world_block(&g->world, nx, ny, z);
    if (n == B_AIR || vx_shape[n] == VX_SHAPE_LIQUID || vx_shape[n] == VX_SHAPE_CROSS) continue;
    return (VxRayHit){.hit = true, .x = nx, .y = ny, .z = z, .nx = -VX_CUT_NBR[k][0], .ny = -VX_CUT_NBR[k][1],
                      .dist = dist, .id = n, .dir = dir};
  }
  return (VxRayHit){.dir = dir};
}

VxRayHit vx_cut_pick(VxGame *g, f32 sx, f32 sy) {
  VxCut *c = &g->cut;
  f64 wx, wy;
  if (!vx_cut_screen(g, sx, sy, &wx, &wy)) return (VxRayHit){0};
  c->touch_cursor = true;
  c->cursor_x = wx, c->cursor_y = wy;
  VxV3d eye = vx_player_eye(&g->player);
  v3 aim = {(f32)(wx - eye.x), (f32)(wy - eye.y), 0};
  f32 al = v3_length(aim);
  g->cam.fwd = al > 0.01f ? v3_scale(aim, 1 / al) : (v3){(f32)c->facing, 0, 0};
  return vx_cut_target(g, wx, wy);
}

void vx_cut_steer(VxGame *g, VxPlayerInput *in) {
  VxCut *c = &g->cut;
  VxPlayer *p = &g->player;
  // the touch stick walks the plane with its sideways push
  i32 dir = (in->right || in->stick_x > 0.3f ? 1 : 0) - (in->left || in->stick_x < -0.3f ? 1 : 0);
  if (dir) c->facing = dir;
  else if (g->input.buttons[MOUSE_LEFT].is_pressed && m_absf(g->cam.fwd.x) > 0.05f) c->facing = g->cam.fwd.x > 0 ? 1 : -1;
  // facing +x is a yaw of -pi/2, so forward walks the facing way
  p->yaw = c->facing > 0 ? -PI * 0.5f : PI * 0.5f;
  p->pitch = 0;
  in->down |= in->back;
  in->forward = dir != 0;
  in->back = in->left = in->right = false;
  in->stick_x = 0;
  in->stick_z = 0;
}

void vx_cut_hold(VxGame *g) {
  if (!g->cut.on) return;
  // play stays on the slab: knockback, creature pushes and hooks cannot carry the player off it
  g->player.pos.z = g->cut.z + 0.5;
  g->player.vel.z = 0;
}

void vx_cut_update(VxGame *g, f32 dt) {
  VxCut *c = &g->cut;
  if (!c->on) return;
  VxPlayer *p = &g->player;
  vx_cut_hold(g);
  f32 k = 1 - m_expf(-dt * 10.0f);
  c->view_x += ((f32)p->pos.x - c->view_x) * k;
  c->view_y += ((f32)p->pos.y + 1.2f - c->view_y) * k;
  g->view_pos = (VxV3d){c->view_x, c->view_y, c->z + 1 + VX_CUT_DIST};
  g->view_yaw = 0;
  g->view_pitch = 0;
  // the light map follows the view, and is redone when the world, the held light or the slab's lighting changed
  i32 x0 = vx_floor_i(c->view_x) - VX_CUT_W / 2;
  c->light_t -= dt;
  if (c->light_x0 == INT32_MIN || m_abs((f64)(x0 - c->light_x0)) > 16 || c->light_edits != g->world.edit_serial ||
      c->light_dyn != g->dyn.version || c->light_t <= 0) {
    c->light_x0 = x0;
    c->light_edits = g->world.edit_serial;
    c->light_dyn = g->dyn.version;
    c->light_t = 0.5f;
    vx_cut_light(g);
  }
  // the logical camera aims from the eye at the pointer, so swings, shots and the hook follow it in the plane
  f64 wx, wy;
  if (!vx_cut_cursor(g, &wx, &wy)) return;
  VxV3d eye = vx_player_eye(p);
  v3 aim = {(f32)(wx - eye.x), (f32)(wy - eye.y), 0};
  f32 al = v3_length(aim);
  g->cam.pos = eye;
  g->cam.fwd = al > 0.01f ? v3_scale(aim, 1 / al) : (v3){(f32)c->facing, 0, 0};
  g->target = g->mode == VX_MODE_PLAYING ? vx_cut_target(g, wx, wy) : (VxRayHit){0};
}

#undef VX_CUT_DIST
#undef VX_CUT_SEEP
