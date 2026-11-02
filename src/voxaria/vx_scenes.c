#include "voxaria/vx_game.h"
#include "engine/hz_command_registry.h"

// the reference video's scenes, the same steps tools/scene.bat and the comparison captures run, minus the hud and test-play steps

#define VX_SC_BASE "0.1:vx_seed:12345;0.25:vx_spawn:natural=0;"
#define VX_SC_GOLD                                                                                                      \
  "0.35:vx_give:item=gold_helmet;0.36:vx_equip:item=gold_helmet;0.37:vx_give:item=gold_chainmail;"                      \
  "0.38:vx_equip:item=gold_chainmail;0.39:vx_give:item=gold_greaves;0.4:vx_equip:item=gold_greaves;"

typedef struct {
  const char *name;
  const char *script;
} VxScene;

hz_internal const VxScene vx_scenes[VX_SCENES] = {
    {"S1 Island overview", VX_SC_BASE "0.5:vx_view:landmark=0 dist=170 up=82 around=0.35 time=0.36 rain=0 fly=1 pitch=-0.4"},
    {"S2 Biome flyover", VX_SC_BASE "0.5:vx_view:x=-75 z=66 above=48 yaw=5.44 pitch=-0.45 time=0.3 rain=0 fly=1"},
    {"S3 Jungle canopy", VX_SC_BASE "0.5:vx_view:landmark=7 dist=60 up=70 around=2.75 pitch=-0.5 time=0.3 rain=0 fly=1"},
    {"S4 Sky island flight", VX_SC_BASE VX_SC_GOLD "0.41:vx_ability:wings=100;0.5:vx_view:x=372 z=-186 y=219 yaw=2.034 "
                                                   "pitch=-0.2 cam=3 dist=7 time=0.3 rain=0 fly=1"},
    {"S5 Town street",
     VX_SC_BASE VX_SC_GOLD "0.41:vx_ability:wings=100;0.5:vx_view:x=1.5 z=6 yaw=3.14159 pitch=-0.05 cam=3 dist=4.5 time=0.3 rain=0"},
    {"S6 House interior", VX_SC_BASE "0.5:vx_view:x=10.2 z=-38.5 y=151.05 yaw=-1.5708 pitch=-0.02 cam=1 time=0.3 rain=0"},
    {"S7 Banner wall", VX_SC_BASE "0.5:vx_view:x=37.5 z=16 yaw=2.75 pitch=-0.12 cam=3 dist=6.5 orbit=0.55 time=0.3 rain=0"},
    {"S8 Furniture museum", VX_SC_BASE "0.5:vx_view:x=-55 z=12 y=157 yaw=2.6 pitch=-0.55 cam=1 fly=1 time=0.3 rain=0"},
    {"S9 Crystal plaza",
     VX_SC_BASE VX_SC_GOLD "0.41:vx_ability:wings=100;0.5:vx_view:x=67 z=-12.5 yaw=-2.0 pitch=-0.5 cam=3 dist=5 time=0.3 rain=0"},
    {"S10 Desert fortress", VX_SC_BASE "0.5:vx_view:landmark=0 dist=75 up=38 around=-0.5 fly=1 time=0.3 pitch=-0.35"},
    {"S11 Snow mountain", VX_SC_BASE "0.5:vx_view:x=-56 y=192 z=-162 yaw=-0.98 pitch=-0.3 fly=1 time=0.3 rain=0"},
    {"S12 Jungle temple", VX_SC_BASE "0.5:vx_view:landmark=7 dist=58 up=50 around=-0.4 fly=1 time=0.3 rain=0 pitch=-0.45"},
    {"S13 Shimmer cave", VX_SC_BASE "0.5:vx_view:yaw=-1.5708 pitch=0.12 site=7 fly=1 time=0.3 rain=0"},
    {"S14 Mushroom cavern", VX_SC_BASE "0.5:vx_view:x=-66 y=84 z=62 yaw=2.1 pitch=-0.3 fly=1 time=0.3 rain=0"},
    {"S15 Digging fall", VX_SC_BASE "0.5:vx_view:x=40.5 y=150 z=40.5 yaw=0.3 pitch=-1.45 fly=0 cam=3 dist=7"},
    {"S16 Underworld", VX_SC_BASE "0.5:vx_view:x=-43 y=21 z=-30 yaw=0 pitch=-0.15 fly=1 cam=3 dist=6"},
    {"S17 Dungeon", VX_SC_BASE "0.5:vx_view:yaw=-1.5708 pitch=-0.12 site=8 cam=3 dist=4.5"},
    {"S18 Dusk boss fight",
     VX_SC_BASE VX_SC_GOLD "0.41:vx_ability:wings=1000;0.42:vx_give:item=enchanted_sword;0.43:vx_select:item=enchanted_sword;"
                           "0.6:vx_view:x=-200.5 y=188.5 z=-30.5 yaw=-1.15 pitch=-0.18 fly=1 cam=3 dist=7 orbit=-0.45 time=0.53;"
                           "4.5:vx_frame:x0=-206 z0=-38 x1=-170 z1=-22 y=185 h=6;5:vx_boss:kind=empress"},
    {"S19 Town at dusk", VX_SC_BASE "0.5:vx_view:x=70 z=-14 above=8 yaw=1.62 pitch=-0.55 fly=1 time=0.46 rain=0"},
    {"S20 Title", "0.1:vx_seed:12345"},
};

void vx_scene_start(VxGame *g, u32 n) {
  if (n < 1 || n > VX_SCENES) return;
  g->scene = n;
  g->scene_pos = 0;
  g->scene_t = 0;
  g->scene_resume = true;
  // the previous scene's granted abilities go with its gear
  g->forced_wings = 0;
  g->forced_double = false;
  g->forced_hook = false;
  log_info("scene: %", fmt_cstr(vx_scenes[n - 1].name));
  vx_game_toast(g, vx_scenes[n - 1].name);
}

void vx_wardrobe_clear(VxGame *g) {
  for (u32 i = 0; i < VX_ARMOR_SLOTS; i++) g->inv.armor[i] = (VxStack){0};
  for (u32 i = 0; i < VX_ACCESSORY_SLOTS; i++) g->inv.accessory[i] = (VxStack){0};
  g->forced_wings = 0;
  g->forced_double = false;
  g->forced_hook = false;
}

void vx_wardrobe_pick(VxGame *g, VxItemId item) {
  VxInventory *inv = &g->inv;
  u8 kind = vx_item(item)->kind;
  if (kind >= VX_IK_HEAD && kind <= VX_IK_LEGS) {
    VxStack *s = &inv->armor[kind - VX_IK_HEAD];
    *s = s->item == item ? (VxStack){0} : (VxStack){item, 1};
    return;
  }
  if (kind == VX_IK_ACCESSORY) {
    for (u32 i = 0; i < VX_ACCESSORY_SLOTS; i++)
      if (inv->accessory[i].item == item) {
        inv->accessory[i] = (VxStack){0};
        // wings a scene granted come off with the wings item
        if (item == I_ANGEL_WINGS) g->forced_wings = 0;
        if (item == I_CLOUD_BOTTLE) g->forced_double = false;
        if (item == I_GRAPPLING_HOOK) g->forced_hook = false;
        return;
      }
    u32 to = VX_ACCESSORY_SLOTS - 1;
    for (u32 i = 0; i < VX_ACCESSORY_SLOTS; i++)
      if (!inv->accessory[i].item) {
        to = i;
        break;
      }
    inv->accessory[to] = (VxStack){item, 1};
    return;
  }
  // a weapon or tool goes in the held slot; a bow brings a full quiver
  for (u32 i = 0; i < VX_INV_SLOTS; i++)
    if (inv->slot[i].item == item && i != inv->selected) inv->slot[i] = (VxStack){0};
  inv->slot[inv->selected] = (VxStack){item, 1};
  if (kind == VX_IK_BOW) {
    u32 have = vx_inventory_count(inv, I_WOODEN_ARROW);
    if (have < 999) vx_inventory_add(inv, I_WOODEN_ARROW, 999 - have);
  }
  vx_game_select(g, inv->selected);
}

// fires the scene's steps that are due; each runs through the same handler its --commands step would
void vx_scene_update(VxGame *g, f32 dt) {
  if (!g->scene) return;
  const char *script = vx_scenes[g->scene - 1].script;
  g->scene_t += dt;
  while (script[g->scene_pos]) {
    const char *s = script + g->scene_pos;
    u32 a = 0;
    while (s[a] && s[a] != ':') a++;
    f64 at = 0;
    if (!s[a] || !str_to_f64(STR((char *)s, a), &at)) {
      log_error("scene %: bad step at %", fmt_u32(g->scene), fmt_u32(g->scene_pos));
      g->scene_pos += cstr_len(s);
      return;
    }
    if ((f32)at > g->scene_t) return;
    u32 b = a + 1;
    while (s[b] && s[b] != ':' && s[b] != ';') b++;
    u32 e = b;
    while (s[e] && s[e] != ';') e++;
    g->scene_pos += e + (s[e] ? 1 : 0);
    tctx_temp_allocator_scope(ta, NULL) {
      CString name = str_to_cstr(STR((char *)s + a + 1, b - a - 1), &ta.allocator);
      CString args = str_to_cstr(s[b] == ':' ? STR((char *)s + b + 1, e - b - 1) : STR((char *)s + b, 0), &ta.allocator);
      HzRegisteredCmd cmd;
      if (hz_command_find(name, &cmd)) {
        HzCommandCtx ctx = {.args = args, .sim_time = g->elapsed};
        cmd.fn(&ctx, cmd.user);
      } else {
        log_error("scene %: unknown step %", fmt_u32(g->scene), fmt_cstr(name));
      }
    }
  }
}
