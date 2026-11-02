#ifndef VX_ITEMS_H
#define VX_ITEMS_H

#include "voxaria/vx.h"

// items: ids below B_COUNT are the blocks themselves (placeable), the rest come from VX_ITEM_LIST

typedef u16 VxItemId;

typedef enum {
  VX_IK_BLOCK,
  VX_IK_PICKAXE,
  VX_IK_AXE,
  VX_IK_SWORD,
  VX_IK_BOW,
  VX_IK_AMMO,
  VX_IK_WAND,
  VX_IK_MATERIAL,
  VX_IK_CONSUMABLE,
  VX_IK_ACCESSORY,
  VX_IK_HEAD,
  VX_IK_BODY,
  VX_IK_LEGS,
  VX_IK_THROWN,
  VX_IK_TOOL,
  VX_IK_SUMMON,
} VxItemKind;

// pixel-art generator for an item's sprite; blocks draw as their cube instead
typedef enum {
  VX_ART_BLOCK,
  VX_ART_PICKAXE,
  VX_ART_AXE,
  VX_ART_BROADSWORD,
  VX_ART_SHORTSWORD,
  VX_ART_BOW,
  VX_ART_ARROW,
  VX_ART_WAND,
  VX_ART_BAR,
  VX_ART_GEL,
  VX_ART_LENS,
  VX_ART_STAR,
  VX_ART_POTION,
  VX_ART_LIFE_CRYSTAL,
  VX_ART_MANA_CRYSTAL,
  VX_ART_MIRROR,
  VX_ART_EYE,
  VX_ART_HOOK,
  VX_ART_BOTTLE,
  VX_ART_BOOTS,
  VX_ART_WINGS,
  VX_ART_MINING_HELMET,
  VX_ART_HELMET,
  VX_ART_CHAINMAIL,
  VX_ART_GREAVES,
  VX_ART_GLOWSTICK,
  VX_ART_GEM,
  VX_ART_COIN,
  VX_ART_COUNT,
} VxItemArt;

// palette ramp an item's art is recoloured with, the way Terraria's copper-to-gold tools are
typedef enum {
  VX_IM_NONE,
  VX_IM_WOOD,
  VX_IM_COPPER,
  VX_IM_TIN,
  VX_IM_IRON,
  VX_IM_SILVER,
  VX_IM_GOLD,
  VX_IM_BLUE,
  VX_IM_RED,
  VX_IM_GREEN,
  VX_IM_WHITE,
  VX_IM_PURPLE,
  VX_IM_COUNT,
} VxItemMat;

// rarity sets the name colour: white, blue, green, orange, light red
typedef enum {
  VX_RARE_WHITE,
  VX_RARE_BLUE,
  VX_RARE_GREEN,
  VX_RARE_ORANGE,
  VX_RARE_LIGHT_RED,
} VxRarity;

// id name kind art mat power(pick/axe power, defense, or amount granted) damage use_ms stack rarity mana
#define VX_ITEM_LIST(X)                                                                                                \
  X(COPPER_SHORTSWORD, "Copper Shortsword", VX_IK_SWORD, VX_ART_SHORTSWORD, VX_IM_COPPER, 0, 5, 220, 1, 0, 0)      \
  X(COPPER_BROADSWORD, "Copper Broadsword", VX_IK_SWORD, VX_ART_BROADSWORD, VX_IM_COPPER, 0, 8, 380, 1, 0, 0)      \
  X(COPPER_PICKAXE, "Copper Pickaxe", VX_IK_PICKAXE, VX_ART_PICKAXE, VX_IM_COPPER, 35, 4, 250, 1, 0, 0)            \
  X(COPPER_AXE, "Copper Axe", VX_IK_AXE, VX_ART_AXE, VX_IM_COPPER, 35, 3, 330, 1, 0, 0)                            \
  X(IRON_BROADSWORD, "Iron Broadsword", VX_IK_SWORD, VX_ART_BROADSWORD, VX_IM_IRON, 0, 10, 360, 1, 0, 0)           \
  X(IRON_PICKAXE, "Iron Pickaxe", VX_IK_PICKAXE, VX_ART_PICKAXE, VX_IM_IRON, 40, 5, 220, 1, 0, 0)                  \
  X(IRON_AXE, "Iron Axe", VX_IK_AXE, VX_ART_AXE, VX_IM_IRON, 45, 5, 300, 1, 0, 0)                                  \
  X(SILVER_BROADSWORD, "Silver Broadsword", VX_IK_SWORD, VX_ART_BROADSWORD, VX_IM_SILVER, 0, 11, 340, 1, 0, 0)     \
  X(SILVER_PICKAXE, "Silver Pickaxe", VX_IK_PICKAXE, VX_ART_PICKAXE, VX_IM_SILVER, 45, 6, 200, 1, 0, 0)            \
  X(SILVER_AXE, "Silver Axe", VX_IK_AXE, VX_ART_AXE, VX_IM_SILVER, 50, 6, 280, 1, 0, 0)                            \
  X(GOLD_BROADSWORD, "Gold Broadsword", VX_IK_SWORD, VX_ART_BROADSWORD, VX_IM_GOLD, 0, 13, 320, 1, 1, 0)           \
  X(GOLD_PICKAXE, "Gold Pickaxe", VX_IK_PICKAXE, VX_ART_PICKAXE, VX_IM_GOLD, 55, 6, 180, 1, 1, 0)                  \
  X(GOLD_AXE, "Gold Axe", VX_IK_AXE, VX_ART_AXE, VX_IM_GOLD, 55, 7, 260, 1, 1, 0)                                  \
  X(WOODEN_BOW, "Wooden Bow", VX_IK_BOW, VX_ART_BOW, VX_IM_WOOD, 0, 4, 450, 1, 0, 0)                               \
  X(WOODEN_ARROW, "Wooden Arrow", VX_IK_AMMO, VX_ART_ARROW, VX_IM_WOOD, 0, 5, 0, 999, 0, 0)                        \
  X(WAND_OF_SPARKING, "Wand of Sparking", VX_IK_WAND, VX_ART_WAND, VX_IM_RED, 0, 8, 140, 1, 1, 1)                  \
  X(GEL, "Gel", VX_IK_MATERIAL, VX_ART_GEL, VX_IM_BLUE, 0, 0, 0, 999, 0, 0)                                        \
  X(LENS, "Lens", VX_IK_MATERIAL, VX_ART_LENS, VX_IM_RED, 0, 0, 0, 999, 0, 0)                                      \
  X(FALLEN_STAR, "Fallen Star", VX_IK_MATERIAL, VX_ART_STAR, VX_IM_GOLD, 0, 0, 0, 999, 1, 0)                       \
  X(COPPER_BAR, "Copper Bar", VX_IK_MATERIAL, VX_ART_BAR, VX_IM_COPPER, 0, 0, 0, 999, 0, 0)                        \
  X(TIN_BAR, "Tin Bar", VX_IK_MATERIAL, VX_ART_BAR, VX_IM_TIN, 0, 0, 0, 999, 0, 0)                                 \
  X(IRON_BAR, "Iron Bar", VX_IK_MATERIAL, VX_ART_BAR, VX_IM_IRON, 0, 0, 0, 999, 0, 0)                              \
  X(SILVER_BAR, "Silver Bar", VX_IK_MATERIAL, VX_ART_BAR, VX_IM_SILVER, 0, 0, 0, 999, 0, 0)                        \
  X(GOLD_BAR, "Gold Bar", VX_IK_MATERIAL, VX_ART_BAR, VX_IM_GOLD, 0, 0, 0, 999, 0, 0)                              \
  X(LIFE_CRYSTAL_ITEM, "Life Crystal", VX_IK_CONSUMABLE, VX_ART_LIFE_CRYSTAL, VX_IM_RED, 20, 0, 500, 99, 2, 0)     \
  X(MANA_CRYSTAL, "Mana Crystal", VX_IK_CONSUMABLE, VX_ART_MANA_CRYSTAL, VX_IM_BLUE, 20, 0, 500, 99, 2, 0)         \
  X(HEALING_POTION, "Lesser Healing Potion", VX_IK_CONSUMABLE, VX_ART_POTION, VX_IM_RED, 50, 0, 300, 30, 0, 0)     \
  X(MAGIC_MIRROR, "Magic Mirror", VX_IK_TOOL, VX_ART_MIRROR, VX_IM_BLUE, 0, 0, 900, 1, 1, 0)                       \
  X(SUSPICIOUS_EYE, "Suspicious Looking Eye", VX_IK_SUMMON, VX_ART_EYE, VX_IM_RED, 0, 0, 900, 20, 1, 0)            \
  X(GRAPPLING_HOOK, "Grappling Hook", VX_IK_ACCESSORY, VX_ART_HOOK, VX_IM_IRON, 0, 0, 0, 1, 1, 0)                  \
  X(CLOUD_BOTTLE, "Cloud in a Bottle", VX_IK_ACCESSORY, VX_ART_BOTTLE, VX_IM_WHITE, 0, 0, 0, 1, 1, 0)              \
  X(HERMES_BOOTS, "Hermes Boots", VX_IK_ACCESSORY, VX_ART_BOOTS, VX_IM_RED, 0, 0, 0, 1, 1, 0)                      \
  X(ANGEL_WINGS, "Angel Wings", VX_IK_ACCESSORY, VX_ART_WINGS, VX_IM_GOLD, 1, 0, 0, 1, 3, 0)                      \
  X(MINING_HELMET, "Mining Helmet", VX_IK_HEAD, VX_ART_MINING_HELMET, VX_IM_GOLD, 1, 0, 0, 1, 0, 0)                \
  X(IRON_HELMET, "Iron Helmet", VX_IK_HEAD, VX_ART_HELMET, VX_IM_IRON, 2, 0, 0, 1, 0, 0)                           \
  X(IRON_CHAINMAIL, "Iron Chainmail", VX_IK_BODY, VX_ART_CHAINMAIL, VX_IM_IRON, 3, 0, 0, 1, 0, 0)                  \
  X(IRON_GREAVES, "Iron Greaves", VX_IK_LEGS, VX_ART_GREAVES, VX_IM_IRON, 2, 0, 0, 1, 0, 0)                        \
  X(GLOWSTICK, "Glowstick", VX_IK_THROWN, VX_ART_GLOWSTICK, VX_IM_GREEN, 0, 0, 300, 99, 0, 0)                          \
  X(AMETHYST_GEM, "Amethyst", VX_IK_MATERIAL, VX_ART_GEM, VX_IM_PURPLE, 0, 0, 0, 999, 0, 0)                            \
  X(SAPPHIRE_GEM, "Sapphire", VX_IK_MATERIAL, VX_ART_GEM, VX_IM_BLUE, 0, 0, 0, 999, 1, 0)                              \
  X(RUBY_GEM, "Ruby", VX_IK_MATERIAL, VX_ART_GEM, VX_IM_RED, 0, 0, 0, 999, 1, 0)                                       \
  X(EMERALD_GEM, "Emerald", VX_IK_MATERIAL, VX_ART_GEM, VX_IM_GREEN, 0, 0, 0, 999, 1, 0)                               \
  X(COPPER_COIN, "Copper Coin", VX_IK_MATERIAL, VX_ART_COIN, VX_IM_COPPER, 0, 0, 0, 100, 0, 0)                         \
  X(SILVER_COIN, "Silver Coin", VX_IK_MATERIAL, VX_ART_COIN, VX_IM_SILVER, 0, 0, 0, 100, 0, 0)                         \
  X(GOLD_COIN, "Gold Coin", VX_IK_MATERIAL, VX_ART_COIN, VX_IM_GOLD, 0, 0, 0, 100, 0, 0)                               \
  X(PLATINUM_COIN, "Platinum Coin", VX_IK_MATERIAL, VX_ART_COIN, VX_IM_WHITE, 0, 0, 0, 999, 0, 0)                     \
  X(GOLD_HELMET, "Gold Helmet", VX_IK_HEAD, VX_ART_HELMET, VX_IM_GOLD, 6, 0, 0, 1, 1, 0)                               \
  X(GOLD_CHAINMAIL, "Gold Chainmail", VX_IK_BODY, VX_ART_CHAINMAIL, VX_IM_GOLD, 7, 0, 0, 1, 1, 0)                      \
  X(GOLD_GREAVES, "Gold Greaves", VX_IK_LEGS, VX_ART_GREAVES, VX_IM_GOLD, 6, 0, 0, 1, 1, 0)                            \
  X(ICE_BLADE, "Ice Blade", VX_IK_SWORD, VX_ART_BROADSWORD, VX_IM_BLUE, 0, 15, 300, 1, 1, 0)                           \
  X(ENCHANTED_SWORD, "Enchanted Sword", VX_IK_SWORD, VX_ART_BROADSWORD, VX_IM_WHITE, 0, 17, 290, 1, 2, 0)              \
  X(NIGHTS_EDGE, "Night's Edge", VX_IK_SWORD, VX_ART_BROADSWORD, VX_IM_PURPLE, 0, 21, 280, 1, 3, 0)

typedef enum {
  I_FIRST = B_COUNT - 1,
#define VX_X(id, name, kind, art, mat, power, dmg, use, stack, rare, mana) I_##id,
  VX_ITEM_LIST(VX_X)
#undef VX_X
  I_COUNT
} VxItemEnum;

typedef struct {
  const char *name;
  u8 kind, art, mat, rarity;
  u16 power, damage, use_ms, stack, mana;
} VxItemInfo;

extern const VxItemInfo vx_items[I_COUNT];
// what breaking a block leaves behind: an item id, or 0
VxItemId vx_block_drop(u8 b);
// the pick power a block needs, 0 for any
u32 vx_block_pick_power(u8 b);
// a hit's worth on a block relative to stone, in percent: dirt and sand break faster, hellstone slower
u32 vx_block_mine_rate(u8 b);

force_inline b32 vx_item_is_block(VxItemId id) { return id > 0 && id < B_COUNT; }
force_inline const VxItemInfo *vx_item(VxItemId id) { return &vx_items[id < I_COUNT ? id : 0]; }
force_inline b32 vx_is_log(u8 b) {
  return b == B_LOG || b == B_BOREAL_LOG || b == B_MAHOGANY_LOG || b == B_PALM_LOG || b == B_EBON_LOG || b == B_PEARL_LOG;
}
force_inline b32 vx_is_leaves(u8 b) {
  return b == B_LEAVES || b == B_BOREAL_LEAVES || b == B_JUNGLE_LEAVES || b == B_PALM_LEAVES || b == B_CORRUPT_LEAVES || b == B_HALLOW_LEAVES ||
         b == B_GOLD_LEAVES;
}

// ---- inventory ----

#define VX_INV_SLOTS 50
#define VX_HOTBAR_SLOTS 10
#define VX_ARMOR_SLOTS 3
#define VX_ACCESSORY_SLOTS 3

typedef struct {
  VxItemId item;
  u16 count;
} VxStack;

typedef struct {
  VxStack slot[VX_INV_SLOTS]; // the first VX_HOTBAR_SLOTS are the hotbar
  VxStack armor[VX_ARMOR_SLOTS];
  VxStack accessory[VX_ACCESSORY_SLOTS];
  VxStack cursor; // the stack picked up in the inventory screen
  VxStack trash;
  u32 selected;
} VxInventory;

void vx_inventory_clear(VxInventory *inv);
// adds into matching stacks, then empty slots (hotbar first); returns what did not fit
u32 vx_inventory_add(VxInventory *inv, VxItemId item, u32 count);
u32 vx_inventory_count(const VxInventory *inv, VxItemId item);
force_inline b32 vx_is_coin(VxItemId id) { return id >= I_COPPER_COIN && id <= I_PLATINUM_COIN; }

#define VX_ITEM_ART_LAYERS (I_COUNT - B_COUNT)
// 16x16 srgb sprite into one layer of a mip-major texture array of layer_count layers
void vx_sprite_mips(const u8 *rgba, u8 *out, u32 layer_count, u32 layer);
// an item's 16x16 srgb rgba art; items only, blocks draw from the tile textures
void vx_item_art_pixels(VxItemId id, u8 *rgba);
b32 vx_inventory_can_take(const VxInventory *inv, VxItemId item);
// removes up to count from the stacks, last slots first; returns how many were removed
u32 vx_inventory_remove(VxInventory *inv, VxItemId item, u32 count);
b32 vx_inventory_has_accessory(const VxInventory *inv, VxItemId item);
force_inline VxStack *vx_inventory_held(VxInventory *inv) { return &inv->slot[inv->selected]; }

typedef enum {
  VX_SLOT_INV,
  VX_SLOT_HEAD,
  VX_SLOT_BODY,
  VX_SLOT_LEGS,
  VX_SLOT_ACCESSORY,
  VX_SLOT_TRASH,
} VxSlotKind;

b32 vx_slot_accepts(VxSlotKind kind, VxItemId item);
// Terraria's clicks: left picks up, drops, merges or swaps with the cursor; right takes one, or equips from the bag
void vx_inventory_click(VxInventory *inv, VxStack *slot, VxSlotKind kind, b32 right);
// ---- crafting ----

typedef enum {
  VX_ST_HAND,
  VX_ST_WORKBENCH,
  VX_ST_FURNACE,
  VX_ST_ANVIL,
  VX_ST_COUNT,
} VxStation;

#define VX_RECIPE_INPUTS 3

typedef struct {
  VxStack out;
  u8 station;
  VxStack in[VX_RECIPE_INPUTS]; // unused inputs have item 0
} VxRecipe;

extern const VxRecipe vx_recipes[];
extern const u32 vx_recipe_count;
extern const char *const vx_station_name[VX_ST_COUNT];
// stations: a bit per VxStation the player stands near (VX_ST_HAND always set)
b32 vx_recipe_available(const VxInventory *inv, const VxRecipe *r, u32 stations);
// consumes the inputs and puts the result on the cursor, as Terraria's crafting does; false if it cannot
b32 vx_craft(VxInventory *inv, const VxRecipe *r, u32 stations);

// the cursor stack back into the bag; returns what did not fit
u32 vx_inventory_stow_cursor(VxInventory *inv);
// total defense of the worn armor
u32 vx_inventory_defense(const VxInventory *inv);

#endif
