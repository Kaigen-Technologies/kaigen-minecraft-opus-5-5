#include "voxaria/vx_game.h"

// the item table, what blocks drop and how hard they are, and the inventory's stack rules

const VxItemInfo vx_items[I_COUNT] = {
#define VX_X(id, label, sh, ly, so, oc, op, lr, lg, lb, wa, re, in, ts, tt, tb)                                        \
  [B_##id] = {.name = label, .kind = VX_IK_BLOCK, .art = VX_ART_BLOCK, .stack = 999},
    VX_BLOCK_LIST(VX_X)
#undef VX_X
#define VX_X(id, nm, k, ar, mt, pw, dm, us, st, ra, mn)                                                               \
  [I_##id] = {.name = nm, .kind = k, .art = ar, .mat = mt, .rarity = ra, .power = pw, .damage = dm, .use_ms = us,    \
              .stack = st, .mana = mn},
        VX_ITEM_LIST(VX_X)
#undef VX_X
};

VxItemId vx_block_drop(u8 b) {
  switch (b) {
  case B_AIR:
  case B_WATER:
  case B_LAVA:
  case B_HONEY:
  case B_SHIMMER:
  case B_GRASS_PLANT:
  case B_FLOWERS:
  case B_JUNGLE_PLANT:
  case B_CORRUPT_PLANT:
  case B_HALLOW_PLANT:
  case B_VINE:
  case B_POT: return 0; // pots break into their own loot
  case B_GRASS:
  case B_HALLOW_GRASS:
  case B_CORRUPT_GRASS: return B_DIRT;
  case B_JUNGLE_GRASS:
  case B_MUSHROOM_GRASS: return B_MUD;
  case B_GLOW_CAP:
  case B_GLOW_STEM: return B_GLOW_MUSHROOM;
  case B_LIFE_CRYSTAL: return I_LIFE_CRYSTAL_ITEM;
  case B_DOOR_OPEN: return B_DOOR;
  case B_COPPER_BEAM_X:
  case B_COPPER_BEAM_Z: return B_COPPER_POST;
  case B_WOOD_BEAM_X:
  case B_WOOD_BEAM_Z: return B_WOOD_POST;
  // trees give wood that builds as planks, as Terraria's Wood does
  case B_LOG: return B_PLANKS;
  case B_BOREAL_LOG: return B_BOREAL_PLANKS;
  case B_MAHOGANY_LOG: return B_MAHOGANY_PLANKS;
  case B_AMETHYST: return I_AMETHYST_GEM;
  case B_SAPPHIRE: return I_SAPPHIRE_GEM;
  case B_RUBY: return I_RUBY_GEM;
  case B_EMERALD: return I_EMERALD_GEM;
  default: return vx_is_leaves(b) ? 0 : b;
  }
}

u32 vx_block_pick_power(u8 b) {
  switch (b) {
  case B_DEMONITE_ORE:
  case B_EBONSTONE:
  case B_OBSIDIAN:
  case B_OBSIDIAN_BRICK: return 55;
  case B_HELLSTONE:
  case B_HELLSTONE_BRICK: return 65;
  default: return 0;
  }
}

u32 vx_block_mine_rate(u8 b) {
  switch (b) {
  case B_DIRT:
  case B_GRASS:
  case B_SAND:
  case B_PEARLSAND:
  case B_MUD:
  case B_JUNGLE_GRASS:
  case B_MUSHROOM_GRASS:
  case B_CORRUPT_GRASS:
  case B_HALLOW_GRASS:
  case B_SNOW:
  case B_ASH:
  case B_CLAY:
  case B_CLOUD:
  case B_HIVE: return 200;
  case B_HELLSTONE:
  case B_HELLSTONE_BRICK:
  case B_DEMONITE_ORE:
  case B_OBSIDIAN:
  case B_OBSIDIAN_BRICK: return 50;
  case B_GOLD_ORE:
  case B_SILVER_ORE: return 80;
  default: return 100;
  }
}

void vx_inventory_clear(VxInventory *inv) { memzero_struct(inv); }

hz_internal u32 vx_stack_cap(VxItemId item) { return vx_item(item)->stack ? vx_item(item)->stack : 1; }

u32 vx_inventory_add(VxInventory *inv, VxItemId item, u32 count) {
  if (item == 0 || item >= I_COUNT) return count;
  // coins change up in whole hundreds as they are taken, as Terraria's do; what the next tier cannot take stays here
  if (vx_is_coin(item) && item != I_PLATINUM_COIN) {
    u32 have = vx_inventory_count(inv, item), total = have + count;
    if (total >= 100) {
      vx_inventory_remove(inv, item, have);
      u32 left = vx_inventory_add(inv, (VxItemId)(item + 1), total / 100);
      count = total % 100 + left * 100;
    }
  }
  u32 cap = vx_stack_cap(item);
  for (u32 i = 0; i < VX_INV_SLOTS && count; i++) {
    VxStack *s = &inv->slot[i];
    if (s->item != item || s->count >= cap) continue;
    u32 n = MIN(count, cap - s->count);
    s->count = (u16)(s->count + n);
    count -= n;
  }
  for (u32 i = 0; i < VX_INV_SLOTS && count; i++) {
    VxStack *s = &inv->slot[i];
    if (s->item) continue;
    u32 n = MIN(count, cap);
    *s = (VxStack){item, (u16)n};
    count -= n;
  }
  return count;
}

b32 vx_inventory_can_take(const VxInventory *inv, VxItemId item) {
  u32 cap = vx_stack_cap(item);
  for (u32 i = 0; i < VX_INV_SLOTS; i++)
    if (!inv->slot[i].item || (inv->slot[i].item == item && inv->slot[i].count < cap)) return true;
  return false;
}

u32 vx_inventory_count(const VxInventory *inv, VxItemId item) {
  u32 n = 0;
  for (u32 i = 0; i < VX_INV_SLOTS; i++)
    if (inv->slot[i].item == item) n += inv->slot[i].count;
  return n;
}

u32 vx_inventory_remove(VxInventory *inv, VxItemId item, u32 count) {
  u32 removed = 0;
  for (i32 i = VX_INV_SLOTS - 1; i >= 0 && removed < count; i--) {
    VxStack *s = &inv->slot[i];
    if (s->item != item) continue;
    u32 n = MIN(count - removed, (u32)s->count);
    s->count = (u16)(s->count - n);
    removed += n;
    if (s->count == 0) s->item = 0;
  }
  return removed;
}

b32 vx_slot_accepts(VxSlotKind kind, VxItemId item) {
  if (!item) return true;
  u8 k = vx_item(item)->kind;
  switch (kind) {
  case VX_SLOT_HEAD: return k == VX_IK_HEAD;
  case VX_SLOT_BODY: return k == VX_IK_BODY;
  case VX_SLOT_LEGS: return k == VX_IK_LEGS;
  case VX_SLOT_ACCESSORY: return k == VX_IK_ACCESSORY;
  default: return true;
  }
}

hz_internal VxStack *vx_equip_target(VxInventory *inv, VxItemId item, VxSlotKind *kind) {
  u8 k = vx_item(item)->kind;
  if (k == VX_IK_HEAD || k == VX_IK_BODY || k == VX_IK_LEGS) {
    *kind = (VxSlotKind)(VX_SLOT_HEAD + (k - VX_IK_HEAD));
    return &inv->armor[k - VX_IK_HEAD];
  }
  if (k != VX_IK_ACCESSORY) return NULL;
  *kind = VX_SLOT_ACCESSORY;
  // the first free accessory slot, else the last one is swapped out
  for (u32 i = 0; i < VX_ACCESSORY_SLOTS; i++)
    if (!inv->accessory[i].item) return &inv->accessory[i];
  return &inv->accessory[VX_ACCESSORY_SLOTS - 1];
}

hz_internal b32 vx_accessory_duplicate(const VxInventory *inv, const VxStack *slot, VxItemId item) {
  for (u32 i = 0; i < VX_ACCESSORY_SLOTS; i++)
    if (&inv->accessory[i] != slot && inv->accessory[i].item == item) return true;
  return false;
}

void vx_inventory_click(VxInventory *inv, VxStack *slot, VxSlotKind kind, b32 right) {
  VxStack *cur = &inv->cursor;
  if (right) {
    VxSlotKind ek;
    VxStack *eq = kind == VX_SLOT_INV && slot->item ? vx_equip_target(inv, slot->item, &ek) : NULL;
    if (eq) {
      if (ek == VX_SLOT_ACCESSORY && vx_accessory_duplicate(inv, eq, slot->item)) return;
      VxStack t = *eq;
      *eq = *slot;
      *slot = t;
      return;
    }
    // take one onto the cursor
    if (!slot->item || (cur->item && (cur->item != slot->item || cur->count >= vx_stack_cap(cur->item)))) return;
    cur->item = slot->item;
    cur->count++;
    if (--slot->count == 0) slot->item = 0;
    return;
  }
  if (kind == VX_SLOT_TRASH) {
    // the trash holds one stack: dropping onto it replaces what was there
    if (cur->item) {
      *slot = *cur;
      *cur = (VxStack){0};
    } else {
      *cur = *slot;
      *slot = (VxStack){0};
    }
    return;
  }
  if (cur->item && cur->item == slot->item) {
    u32 cap = vx_stack_cap(slot->item), n = MIN((u32)cur->count, cap - slot->count);
    slot->count = (u16)(slot->count + n);
    cur->count = (u16)(cur->count - n);
    if (!cur->count) cur->item = 0;
    return;
  }
  if (!vx_slot_accepts(kind, cur->item)) return;
  if (kind == VX_SLOT_ACCESSORY && cur->item && vx_accessory_duplicate(inv, slot, cur->item)) return;
  VxStack t = *slot;
  *slot = *cur;
  *cur = t;
}

u32 vx_inventory_stow_cursor(VxInventory *inv) {
  if (!inv->cursor.item) return 0;
  u32 left = vx_inventory_add(inv, inv->cursor.item, inv->cursor.count);
  inv->cursor.count = (u16)left;
  if (!left) inv->cursor.item = 0;
  return left;
}

u32 vx_inventory_defense(const VxInventory *inv) {
  u32 d = 0;
  for (u32 i = 0; i < VX_ARMOR_SLOTS; i++)
    if (inv->armor[i].item) d += vx_item(inv->armor[i].item)->power;
  return d;
}

b32 vx_inventory_has_accessory(const VxInventory *inv, VxItemId item) {
  for (u32 i = 0; i < VX_ACCESSORY_SLOTS; i++)
    if (inv->accessory[i].item == item) return true;
  return false;
}


const char *const vx_station_name[VX_ST_COUNT] = {"By hand", "Work Bench", "Furnace", "Iron Anvil"};

#define VX_R(out, n, st, a, an, b, bn, c, cn) {{out, n}, st, {{a, an}, {b, bn}, {c, cn}}}
const VxRecipe vx_recipes[] = {
    // by hand
    VX_R(B_WORKBENCH, 1, VX_ST_HAND, B_PLANKS, 10, 0, 0, 0, 0),
    VX_R(B_TORCH, 3, VX_ST_HAND, I_GEL, 1, B_PLANKS, 1, 0, 0),
    VX_R(B_PLATFORM, 2, VX_ST_HAND, B_PLANKS, 1, 0, 0, 0, 0),
    VX_R(I_MANA_CRYSTAL, 1, VX_ST_HAND, I_FALLEN_STAR, 5, 0, 0, 0, 0),
    // work bench
    VX_R(B_TABLE, 1, VX_ST_WORKBENCH, B_PLANKS, 8, 0, 0, 0, 0),
    VX_R(B_CHAIR, 1, VX_ST_WORKBENCH, B_PLANKS, 4, 0, 0, 0, 0),
    VX_R(B_BED, 1, VX_ST_WORKBENCH, B_PLANKS, 15, 0, 0, 0, 0),
    VX_R(B_BOOKCASE, 1, VX_ST_WORKBENCH, B_PLANKS, 20, 0, 0, 0, 0),
    VX_R(B_SOFA, 1, VX_ST_WORKBENCH, B_PLANKS, 5, 0, 0, 0, 0),
    VX_R(B_LAMP, 1, VX_ST_WORKBENCH, B_PLANKS, 3, B_TORCH, 1, 0, 0),
    VX_R(B_CLOCK, 1, VX_ST_WORKBENCH, B_PLANKS, 10, I_IRON_BAR, 3, 0, 0),
    VX_R(B_PIANO, 1, VX_ST_WORKBENCH, B_PLANKS, 15, 0, 0, 0, 0),
    VX_R(B_DOOR, 1, VX_ST_WORKBENCH, B_PLANKS, 6, 0, 0, 0, 0),
    VX_R(B_CHEST, 1, VX_ST_WORKBENCH, B_PLANKS, 8, I_IRON_BAR, 2, 0, 0),
    VX_R(B_FURNACE, 1, VX_ST_WORKBENCH, B_STONE, 20, B_PLANKS, 4, B_TORCH, 3),
    VX_R(B_ANVIL, 1, VX_ST_WORKBENCH, I_IRON_BAR, 5, 0, 0, 0, 0),
    VX_R(I_WOODEN_BOW, 1, VX_ST_WORKBENCH, B_PLANKS, 10, 0, 0, 0, 0),
    VX_R(I_WOODEN_ARROW, 5, VX_ST_WORKBENCH, B_PLANKS, 1, B_STONE, 1, 0, 0),
    VX_R(I_HEALING_POTION, 2, VX_ST_WORKBENCH, B_MUSHROOM, 1, I_GEL, 2, 0, 0),
    VX_R(I_SUSPICIOUS_EYE, 1, VX_ST_WORKBENCH, I_LENS, 6, 0, 0, 0, 0),
    VX_R(B_GLOW_MUSHROOM, 1, VX_ST_WORKBENCH, B_MUSHROOM, 1, I_GEL, 1, 0, 0),
    // furnace
    VX_R(I_COPPER_BAR, 1, VX_ST_FURNACE, B_COPPER_ORE, 3, 0, 0, 0, 0),
    VX_R(I_TIN_BAR, 1, VX_ST_FURNACE, B_TIN_ORE, 3, 0, 0, 0, 0),
    VX_R(I_IRON_BAR, 1, VX_ST_FURNACE, B_IRON_ORE, 3, 0, 0, 0, 0),
    VX_R(I_SILVER_BAR, 1, VX_ST_FURNACE, B_SILVER_ORE, 4, 0, 0, 0, 0),
    VX_R(I_GOLD_BAR, 1, VX_ST_FURNACE, B_GOLD_ORE, 4, 0, 0, 0, 0),
    VX_R(B_GLASS, 1, VX_ST_FURNACE, B_SAND, 2, 0, 0, 0, 0),
    VX_R(B_GRAY_BRICK, 1, VX_ST_FURNACE, B_STONE, 2, 0, 0, 0, 0),
    VX_R(B_RED_BRICK, 1, VX_ST_FURNACE, B_CLAY, 2, 0, 0, 0, 0),
    // iron anvil
    VX_R(I_COPPER_SHORTSWORD, 1, VX_ST_ANVIL, I_COPPER_BAR, 7, 0, 0, 0, 0),
    VX_R(I_COPPER_BROADSWORD, 1, VX_ST_ANVIL, I_COPPER_BAR, 8, 0, 0, 0, 0),
    VX_R(I_COPPER_PICKAXE, 1, VX_ST_ANVIL, I_COPPER_BAR, 12, B_PLANKS, 4, 0, 0),
    VX_R(I_COPPER_AXE, 1, VX_ST_ANVIL, I_COPPER_BAR, 9, B_PLANKS, 3, 0, 0),
    VX_R(I_IRON_BROADSWORD, 1, VX_ST_ANVIL, I_IRON_BAR, 8, 0, 0, 0, 0),
    VX_R(I_IRON_PICKAXE, 1, VX_ST_ANVIL, I_IRON_BAR, 12, B_PLANKS, 3, 0, 0),
    VX_R(I_IRON_AXE, 1, VX_ST_ANVIL, I_IRON_BAR, 9, B_PLANKS, 3, 0, 0),
    VX_R(I_SILVER_BROADSWORD, 1, VX_ST_ANVIL, I_SILVER_BAR, 8, 0, 0, 0, 0),
    VX_R(I_SILVER_PICKAXE, 1, VX_ST_ANVIL, I_SILVER_BAR, 12, B_PLANKS, 4, 0, 0),
    VX_R(I_SILVER_AXE, 1, VX_ST_ANVIL, I_SILVER_BAR, 9, B_PLANKS, 3, 0, 0),
    VX_R(I_GOLD_BROADSWORD, 1, VX_ST_ANVIL, I_GOLD_BAR, 8, 0, 0, 0, 0),
    VX_R(I_GOLD_PICKAXE, 1, VX_ST_ANVIL, I_GOLD_BAR, 12, B_PLANKS, 4, 0, 0),
    VX_R(I_GOLD_AXE, 1, VX_ST_ANVIL, I_GOLD_BAR, 9, B_PLANKS, 3, 0, 0),
    VX_R(I_IRON_HELMET, 1, VX_ST_ANVIL, I_IRON_BAR, 15, 0, 0, 0, 0),
    VX_R(I_IRON_CHAINMAIL, 1, VX_ST_ANVIL, I_IRON_BAR, 25, 0, 0, 0, 0),
    VX_R(I_IRON_GREAVES, 1, VX_ST_ANVIL, I_IRON_BAR, 20, 0, 0, 0, 0),
    VX_R(I_GOLD_HELMET, 1, VX_ST_ANVIL, I_GOLD_BAR, 15, 0, 0, 0, 0),
    VX_R(I_GOLD_CHAINMAIL, 1, VX_ST_ANVIL, I_GOLD_BAR, 25, 0, 0, 0, 0),
    VX_R(I_GOLD_GREAVES, 1, VX_ST_ANVIL, I_GOLD_BAR, 20, 0, 0, 0, 0),
    VX_R(I_GRAPPLING_HOOK, 1, VX_ST_ANVIL, I_IRON_BAR, 6, 0, 0, 0, 0),
};
#undef VX_R
const u32 vx_recipe_count = sizeof(vx_recipes) / sizeof(vx_recipes[0]);

b32 vx_recipe_available(const VxInventory *inv, const VxRecipe *r, u32 stations) {
  if (!(stations & (1u << r->station))) return false;
  for (u32 i = 0; i < VX_RECIPE_INPUTS; i++)
    if (r->in[i].item && vx_inventory_count(inv, r->in[i].item) < r->in[i].count) return false;
  return true;
}

b32 vx_craft(VxInventory *inv, const VxRecipe *r, u32 stations) {
  if (!vx_recipe_available(inv, r, stations)) return false;
  // the result joins the cursor stack, so it must be empty or the same item with room
  VxStack *c = &inv->cursor;
  if (c->item && (c->item != r->out.item || (u32)c->count + r->out.count > vx_stack_cap(c->item))) return false;
  for (u32 i = 0; i < VX_RECIPE_INPUTS; i++)
    if (r->in[i].item) vx_inventory_remove(inv, r->in[i].item, r->in[i].count);
  *c = (VxStack){r->out.item, (u16)(c->count + r->out.count)};
  return true;
}
