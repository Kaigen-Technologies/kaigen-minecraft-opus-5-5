#include "webcraft/wc_game.h"

// minecraft's default touch scheme: left-thumb stick, drag to look, tap a block to place, hold it to break

#define WC_STICK_ZONE 0.4f      // share of the screen width, from the left, that starts the stick
#define WC_STICK_RADIUS 56.0f   // ui px of knob travel for full speed
#define WC_STICK_DEAD 0.12f
#define WC_STICK_SPRINT 0.95f   // pushed to the rim while mostly forward
#define WC_DRAG_SLOP 10.0f      // ui px a finger may wander and still be a tap or hold
#define WC_HOLD_BREAK 0.3f      // seconds before a still finger starts breaking
#define WC_TOUCH_LOOK_SCALE 2.6f // radians per px relative to the mouse: a thumb sweep turns about 150 degrees
#define WC_REACH 5.5f

b32 wc_touch_portrait(const WcGame *g) {
  return g->touch_mode && g->ui->canvas_height > g->ui->canvas_width;
}

void wc_touch_reset(WcTouch *t) {
  memzero_struct(t);
}

hz_internal WcTouchTrack *wc_touch_track(WcTouch *t, u32 id) {
  for (u32 i = 0; i < MAX_TOUCHES; i++)
    if (t->tracks[i].role != WC_TOUCH_FREE && t->tracks[i].id == id) return &t->tracks[i];
  return NULL;
}

hz_internal WcTouchTrack *wc_touch_track_new(WcTouch *t, u32 id) {
  for (u32 i = 0; i < MAX_TOUCHES; i++) {
    if (t->tracks[i].role == WC_TOUCH_FREE) {
      t->tracks[i] = (WcTouchTrack){.id = id, .role = WC_TOUCH_IGNORED};
      return &t->tracks[i];
    }
  }
  return NULL;
}

hz_internal b32 wc_touch_hits(HzUIElementId id, f32 x, f32 y) {
  HzUIElementData d = ui_get_element_data(id);
  if (!d.found) return false;
  // a few px of forgiveness around every button
  const f32 m = 6;
  return x >= d.boundingBox.x - m && x <= d.boundingBox.x + d.boundingBox.width + m && y >= d.boundingBox.y - m &&
         y <= d.boundingBox.y + d.boundingBox.height + m;
}

// buttons are hit-tested against last frame's layout, which is exactly what is on screen
hz_internal b32 wc_touch_button_at(f32 x, f32 y, u32 *out) {
  for (u32 i = 0; i < WC_HOTBAR_SLOTS; i++) {
    if (wc_touch_hits(ui_idi("HotbarSlot", i), x, y)) {
      *out = WC_TBTN_HOTBAR + i;
      return true;
    }
  }
  if (wc_touch_hits(ui_id("TouchJump"), x, y)) *out = WC_TBTN_JUMP;
  else if (wc_touch_hits(ui_id("TouchSneak"), x, y)) *out = WC_TBTN_SNEAK;
  else if (wc_touch_hits(ui_id("TouchInventory"), x, y)) *out = WC_TBTN_INVENTORY;
  else if (wc_touch_hits(ui_id("TouchPause"), x, y)) *out = WC_TBTN_PAUSE;
  else return false;
  return true;
}

// the world ray under a screen point, through the camera the frame is rendered with
hz_internal WcRayHit wc_touch_ray(const WcGame *g, f32 x, f32 y) {
  const WcPlayer *p = &g->player;
  f32 w = (f32)g->ui->canvas_width, h = (f32)g->ui->canvas_height;
  if (w <= 0 || h <= 0) return (WcRayHit){0};
  v3 fwd = wc_player_forward(p);
  v3 right = v3_normalize(v3_cross(fwd, (v3){0, 1, 0}));
  v3 up = v3_cross(right, fwd);
  f32 t = m_tanf(g->settings.fov * (1 + p->fov_boost) * PI / 360.0f);
  f32 nx = (2 * x / w - 1) * t * (w / h);
  f32 ny = (1 - 2 * y / h) * t;
  v3 d = v3_normalize(v3_add(fwd, v3_add(v3_scale(right, nx), v3_scale(up, ny))));
  return wc_raycast(&g->world, wc_player_camera(p, g->settings.bobbing), d, WC_REACH);
}

hz_internal void wc_touch_button_down(WcGame *g, u32 button) {
  WcTouch *t = &g->touch;
  if (button >= WC_TBTN_HOTBAR) wc_game_select(g, button - WC_TBTN_HOTBAR);
  else if (button == WC_TBTN_JUMP) t->jump_pressed = true;
  // on the ground sneak is a toggle; flying it is held to descend
  else if (button == WC_TBTN_SNEAK && !g->player.flying) t->sneak_on = !t->sneak_on;
}

// menus open on release, like every ui button: the lifting finger must not click the new screen
hz_internal void wc_touch_button_up(WcGame *g, u32 button) {
  if (button == WC_TBTN_INVENTORY) wc_game_set_mode(g, WC_MODE_INVENTORY);
  else if (button == WC_TBTN_PAUSE) wc_game_set_mode(g, WC_MODE_PAUSED);
}

void wc_touch_update(WcGame *g, f32 dt) {
  WcTouch *t = &g->touch;
  const MobileTouches *touches = &g->input.touches;
  f32 w = (f32)g->ui->canvas_width;
  t->jump_pressed = false;
  t->jump_held = false;
  t->sneak_held = false;
  t->target = (WcRayHit){0};
  g->break_cd -= dt;
  f32 look_dx = 0, look_dy = 0;
  b32 stick_live = false;

  for (u32 i = 0; i < MAX_TOUCHES; i++) {
    const MobileTouch *mt = &touches->items[i];
    if (!mt->is_active && !mt->stopped_this_frame) continue;
    WcTouchTrack *tr = wc_touch_track(t, mt->id);
    if (mt->started_this_frame && !tr) {
      tr = wc_touch_track_new(t, mt->id);
      if (!tr) continue;
      u32 button;
      b32 stick_taken = false;
      for (u32 k = 0; k < MAX_TOUCHES; k++) stick_taken |= t->tracks[k].role == WC_TOUCH_STICK;
      if (wc_touch_button_at(mt->start_x, mt->start_y, &button)) {
        tr->role = WC_TOUCH_BUTTON;
        tr->button = button;
        wc_touch_button_down(g, button);
      } else if (mt->start_x < w * WC_STICK_ZONE && !stick_taken) {
        tr->role = WC_TOUCH_STICK;
        t->stick_center = (v2){mt->start_x, mt->start_y};
      } else {
        tr->role = WC_TOUCH_WORLD;
      }
    }
    // fingers that went down outside play stay inert until lifted
    if (!tr) continue;

    f32 mdx = mt->current_x - mt->start_x, mdy = mt->current_y - mt->start_y;
    b32 wandered = mdx * mdx + mdy * mdy > WC_DRAG_SLOP * WC_DRAG_SLOP;
    switch (tr->role) {
    case WC_TOUCH_STICK: {
      f32 len = m_sqrtf(mdx * mdx + mdy * mdy);
      f32 k = len > WC_STICK_RADIUS ? WC_STICK_RADIUS / len : 1.0f;
      t->stick_knob = (v2){t->stick_center.x + mdx * k, t->stick_center.y + mdy * k};
      f32 sx = mdx * k / WC_STICK_RADIUS, sz = mdy * k / WC_STICK_RADIUS;
      f32 mag = m_sqrtf(sx * sx + sz * sz);
      // rescale past the dead zone so the stick still reaches full speed
      f32 live = mag > WC_STICK_DEAD ? (mag - WC_STICK_DEAD) / (1 - WC_STICK_DEAD) / mag : 0;
      t->stick_x = sx * live;
      t->stick_z = sz * live;
      stick_live = !mt->stopped_this_frame;
      break;
    }
    case WC_TOUCH_WORLD:
      if (wandered) tr->role = WC_TOUCH_LOOK;
      else if (g->input_now - mt->start_time >= WC_HOLD_BREAK) tr->role = WC_TOUCH_BREAK;
      else if (mt->stopped_this_frame) {
        WcRayHit h = wc_touch_ray(g, mt->current_x, mt->current_y);
        if (h.hit) wc_place_block(g, h);
      }
      break;
    case WC_TOUCH_LOOK:
      look_dx += mt->current_x - mt->prev_frame_x;
      look_dy += mt->current_y - mt->prev_frame_y;
      break;
    case WC_TOUCH_BREAK:
      break;
    case WC_TOUCH_BUTTON:
      if (tr->button == WC_TBTN_JUMP) t->jump_held = true;
      if (tr->button == WC_TBTN_SNEAK && g->player.flying) t->sneak_held = true;
      if (mt->stopped_this_frame) wc_touch_button_up(g, tr->button);
      break;
    default:
      break;
    }
    // a hold that became a break this frame breaks this frame; the finger steers the target
    if (tr->role == WC_TOUCH_BREAK && !mt->stopped_this_frame) {
      t->target = wc_touch_ray(g, mt->current_x, mt->current_y);
      if (t->target.hit && g->break_cd <= 0) {
        wc_break_block(g, t->target);
        g->break_cd = 0.22f;
      }
    }
    if (mt->stopped_this_frame) *tr = (WcTouchTrack){0};
  }

  if (!stick_live) t->stick_x = t->stick_z = 0;
  if (look_dx != 0 || look_dy != 0)
    wc_player_look(&g->player, look_dx * WC_TOUCH_LOOK_SCALE, look_dy * WC_TOUCH_LOOK_SCALE);
}

b32 wc_touch_stick_active(const WcTouch *t) {
  for (u32 i = 0; i < MAX_TOUCHES; i++)
    if (t->tracks[i].role == WC_TOUCH_STICK) return true;
  return false;
}

b32 wc_touch_sprinting(const WcTouch *t) {
  return t->stick_z < 0 && t->stick_x * t->stick_x + t->stick_z * t->stick_z >= WC_STICK_SPRINT * WC_STICK_SPRINT &&
         -t->stick_z > m_absf(t->stick_x);
}
