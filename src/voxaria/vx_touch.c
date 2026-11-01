#include "voxaria/vx_game.h"

// minecraft's default touch scheme: left-thumb stick, drag to look, tap a block to place, hold it to break

#define VX_STICK_ZONE 0.4f      // share of the screen width, from the left, that starts the stick
#define VX_STICK_RADIUS 56.0f   // ui px of knob travel for full speed
#define VX_STICK_DEAD 0.12f
#define VX_DRAG_SLOP 10.0f      // ui px a finger may wander and still be a tap or hold
#define VX_HOLD_BREAK 0.3f      // seconds before a still finger starts breaking
#define VX_TOUCH_LOOK_SCALE 2.6f // radians per px relative to the mouse: a thumb sweep turns about 150 degrees

b32 vx_touch_portrait(const VxGame *g) {
  return g->touch_mode && g->ui->canvas_height > g->ui->canvas_width;
}

void vx_touch_reset(VxTouch *t) {
  memzero_struct(t);
}

hz_internal VxTouchTrack *vx_touch_track(VxTouch *t, u32 id) {
  for (u32 i = 0; i < MAX_TOUCHES; i++)
    if (t->tracks[i].role != VX_TOUCH_FREE && t->tracks[i].id == id) return &t->tracks[i];
  return NULL;
}

hz_internal VxTouchTrack *vx_touch_track_new(VxTouch *t, u32 id) {
  for (u32 i = 0; i < MAX_TOUCHES; i++) {
    if (t->tracks[i].role == VX_TOUCH_FREE) {
      t->tracks[i] = (VxTouchTrack){.id = id, .role = VX_TOUCH_IGNORED};
      return &t->tracks[i];
    }
  }
  return NULL;
}

hz_internal b32 vx_touch_hits(HzUIElementId id, f32 x, f32 y) {
  HzUIElementData d = ui_get_element_data(id);
  if (!d.found) return false;
  // a few px of forgiveness around every button
  const f32 m = 6;
  return x >= d.boundingBox.x - m && x <= d.boundingBox.x + d.boundingBox.width + m && y >= d.boundingBox.y - m &&
         y <= d.boundingBox.y + d.boundingBox.height + m;
}

// buttons are hit-tested against last frame's layout, which is exactly what is on screen
hz_internal b32 vx_touch_button_at(f32 x, f32 y, u32 *out) {
  for (u32 i = 0; i < VX_HOTBAR_SLOTS; i++) {
    if (vx_touch_hits(ui_idi("HotbarSlot", i), x, y)) {
      *out = VX_TBTN_HOTBAR + i;
      return true;
    }
  }
  if (vx_touch_hits(ui_id("TouchJump"), x, y)) *out = VX_TBTN_JUMP;
  else if (vx_touch_hits(ui_id("TouchSneak"), x, y)) *out = VX_TBTN_SNEAK;
  else if (vx_touch_hits(ui_id("TouchInventory"), x, y)) *out = VX_TBTN_INVENTORY;
  else if (vx_touch_hits(ui_id("TouchPause"), x, y)) *out = VX_TBTN_PAUSE;
  else if (vx_touch_hits(ui_id("TouchMap"), x, y)) *out = VX_TBTN_MAP;
  else if (vx_touch_hits(ui_id("TouchHook"), x, y)) *out = VX_TBTN_HOOK;
  else if (vx_touch_hits(ui_id("TouchHeal"), x, y)) *out = VX_TBTN_HEAL;
  else if (vx_touch_hits(ui_id("TouchView"), x, y)) *out = VX_TBTN_VIEW;
  else return false;
  return true;
}

// the world ray under a screen point, through the camera the frame is rendered with
hz_internal VxRayHit vx_touch_ray(VxGame *g, f32 x, f32 y) {
  // the side view picks the tile on the plane under the finger
  if (g->cut.on) return vx_cut_pick(g, x, y);
  const VxPlayer *p = &g->player;
  f32 w = (f32)g->ui->canvas_width, h = (f32)g->ui->canvas_height;
  if (w <= 0 || h <= 0) return (VxRayHit){0};
  v3 fwd = vx_player_forward(p);
  v3 right = v3_normalize(v3_cross(fwd, (v3){0, 1, 0}));
  v3 up = v3_cross(right, fwd);
  f32 t = m_tanf(g->settings.fov * (1 + p->fov_boost) * PI / 360.0f);
  f32 nx = (2 * x / w - 1) * t * (w / h);
  f32 ny = (1 - 2 * y / h) * t;
  v3 d = v3_normalize(v3_add(fwd, v3_add(v3_scale(right, nx), v3_scale(up, ny))));
  VxRayHit hit = vx_camera_target(&g->cam, g->cam.pos, d, p, &g->world, VX_REACH, g->smart_cursor);
  hit.dir = d;
  return hit;
}

hz_internal void vx_touch_button_down(VxGame *g, u32 button) {
  VxTouch *t = &g->touch;
  if (button >= VX_TBTN_HOTBAR) vx_game_select(g, button - VX_TBTN_HOTBAR);
  else if (button == VX_TBTN_JUMP) t->jump_pressed = true;
  else if (button == VX_TBTN_HOOK) t->hook_pressed = true;
  else if (button == VX_TBTN_HEAL) vx_quick_heal(g);
  // on the ground sneak is a toggle; flying it is held to descend
  else if (button == VX_TBTN_SNEAK && !g->player.flying) t->sneak_on = !t->sneak_on;
}

// menus open on release, like every ui button: the lifting finger must not click the new screen
hz_internal void vx_touch_button_up(VxGame *g, u32 button) {
  if (button == VX_TBTN_INVENTORY) vx_game_set_mode(g, VX_MODE_INVENTORY);
  else if (button == VX_TBTN_PAUSE) vx_game_set_mode(g, VX_MODE_PAUSED);
  else if (button == VX_TBTN_MAP) g->show_map = !g->show_map;
  else if (button == VX_TBTN_VIEW) vx_cut_set(g, !g->cut.on);
}

void vx_touch_update(VxGame *g, f32 dt) {
  VxTouch *t = &g->touch;
  const MobileTouches *touches = &g->input.touches;
  f32 w = (f32)g->ui->canvas_width;
  t->jump_pressed = false;
  t->hook_pressed = false;
  t->jump_held = false;
  t->sneak_held = false;
  t->target = (VxRayHit){0};
  f32 look_dx = 0, look_dy = 0;
  b32 stick_live = false;

  // a lift seen on a frame this did not run (a menu, portrait) leaves a track whose finger is gone: drop it
  for (u32 k = 0; k < MAX_TOUCHES; k++) {
    VxTouchTrack *tr = &t->tracks[k];
    if (tr->role == VX_TOUCH_FREE) continue;
    b32 live = false;
    for (u32 i = 0; i < MAX_TOUCHES; i++) {
      const MobileTouch *mt = &touches->items[i];
      live |= (mt->is_active || mt->stopped_this_frame) && !mt->started_this_frame && mt->id == tr->id;
    }
    if (!live) *tr = (VxTouchTrack){0};
  }

  for (u32 i = 0; i < MAX_TOUCHES; i++) {
    const MobileTouch *mt = &touches->items[i];
    if (!mt->is_active && !mt->stopped_this_frame) continue;
    VxTouchTrack *tr = vx_touch_track(t, mt->id);
    if (mt->started_this_frame && !tr) {
      tr = vx_touch_track_new(t, mt->id);
      if (!tr) continue;
      u32 button;
      b32 stick_taken = false;
      for (u32 k = 0; k < MAX_TOUCHES; k++) stick_taken |= t->tracks[k].role == VX_TOUCH_STICK;
      if (vx_touch_button_at(mt->start_x, mt->start_y, &button)) {
        tr->role = VX_TOUCH_BUTTON;
        tr->button = button;
        vx_touch_button_down(g, button);
      } else if (mt->start_x < w * VX_STICK_ZONE && !stick_taken) {
        tr->role = VX_TOUCH_STICK;
        t->stick_center = (v2){mt->start_x, mt->start_y};
      } else {
        tr->role = VX_TOUCH_WORLD;
      }
    }
    // fingers that went down outside play stay inert until lifted
    if (!tr) continue;

    f32 mdx = mt->current_x - mt->start_x, mdy = mt->current_y - mt->start_y;
    b32 wandered = mdx * mdx + mdy * mdy > VX_DRAG_SLOP * VX_DRAG_SLOP;
    switch (tr->role) {
    case VX_TOUCH_STICK: {
      f32 len = m_sqrtf(mdx * mdx + mdy * mdy);
      f32 k = len > VX_STICK_RADIUS ? VX_STICK_RADIUS / len : 1.0f;
      t->stick_knob = (v2){t->stick_center.x + mdx * k, t->stick_center.y + mdy * k};
      f32 sx = mdx * k / VX_STICK_RADIUS, sz = mdy * k / VX_STICK_RADIUS;
      f32 mag = m_sqrtf(sx * sx + sz * sz);
      // rescale past the dead zone so the stick still reaches full speed
      f32 live = mag > VX_STICK_DEAD ? (mag - VX_STICK_DEAD) / (1 - VX_STICK_DEAD) / mag : 0;
      t->stick_x = sx * live;
      t->stick_z = sz * live;
      stick_live = !mt->stopped_this_frame;
      break;
    }
    case VX_TOUCH_WORLD:
      if (wandered) tr->role = VX_TOUCH_LOOK;
      else if (g->input_now - mt->start_time >= VX_HOLD_BREAK) tr->role = VX_TOUCH_BREAK;
      else if (mt->stopped_this_frame) {
        // a tap on a door or chest works it; anywhere else it uses the held item
        VxRayHit h = vx_touch_ray(g, mt->current_x, mt->current_y);
        if (!vx_interact(g, h)) vx_use_item(g, false, true, h);
      }
      break;
    case VX_TOUCH_LOOK:
      look_dx += mt->current_x - mt->prev_frame_x;
      look_dy += mt->current_y - mt->prev_frame_y;
      break;
    case VX_TOUCH_BREAK:
      break;
    case VX_TOUCH_BUTTON:
      if (tr->button == VX_TBTN_JUMP) t->jump_held = true;
      if (tr->button == VX_TBTN_SNEAK && g->player.flying) t->sneak_held = true;
      if (mt->stopped_this_frame) vx_touch_button_up(g, tr->button);
      break;
    default:
      break;
    }
    // a hold that became a break this frame breaks this frame; the finger steers the target
    if (tr->role == VX_TOUCH_BREAK && !mt->stopped_this_frame) {
      t->target = vx_touch_ray(g, mt->current_x, mt->current_y);
      vx_use_item(g, true, false, t->target);
    }
    if (mt->stopped_this_frame) *tr = (VxTouchTrack){0};
  }

  if (!stick_live) t->stick_x = t->stick_z = 0;
  if (look_dx != 0 || look_dy != 0)
    vx_player_look(&g->player, look_dx * VX_TOUCH_LOOK_SCALE, look_dy * VX_TOUCH_LOOK_SCALE);
}

b32 vx_touch_stick_active(const VxTouch *t) {
  for (u32 i = 0; i < MAX_TOUCHES; i++)
    if (t->tracks[i].role == VX_TOUCH_STICK) return true;
  return false;
}
