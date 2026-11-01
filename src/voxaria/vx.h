#ifndef H_VX
#define H_VX

#include "lib/typedefs.h"
#include "lib/hzmath.h"
#include "lib/memory.h"
#include "lib/thread_context.h"
#include "lib/allocator_arena.h"
#include "lib/offset_allocator.h"
#include "os/os.h"
#include "gpu/gpu.h"

#define VX_CHUNK 16
#define VX_HEIGHT 256
#define VX_SECTIONS 16
#define VX_SEA_LEVEL 140
#define VX_CHUNK_VOLUME (VX_CHUNK * VX_CHUNK * VX_HEIGHT)
#define VX_SECTION_VOLUME (VX_CHUNK * VX_CHUNK * VX_CHUNK)
// loaded chunks always fit in this toroidal grid: unload radius 28.5 < 32
#define VX_GRID 64
#define VX_GRID_MASK (VX_GRID - 1)
#define VX_MAX_CHUNKS (VX_GRID * VX_GRID)
#define VX_MAX_RENDER_DISTANCE 24
#define VX_NO_SLOT 0xFFFFu

#define VX_IDX(x, y, z) (((y) << 8) | ((z) << 4) | (x))

// double-precision world position (player and camera)
typedef struct {
  f64 x, y, z;
} VxV3d;

typedef enum {
  VX_SHAPE_AIR,
  VX_SHAPE_CUBE,
  VX_SHAPE_CROSS,
  VX_SHAPE_LIQUID,
  VX_SHAPE_TORCH,
  VX_SHAPE_MODEL, // a few axis-aligned boxes (vx_model)
} VxShape;

typedef enum {
  VX_LAYER_NONE,
  VX_LAYER_OPAQUE,
  VX_LAYER_CUTOUT,
  VX_LAYER_TRANSLUCENT,
} VxLayer;

typedef enum {
  VX_WAVE_NONE,
  VX_WAVE_LEAVES,
  VX_WAVE_PLANT,
} VxWave;

// face order everywhere: +X -X +Y -Y +Z -Z, then the plant pseudo-face
enum {
  VX_FACE_PX,
  VX_FACE_NX,
  VX_FACE_PY,
  VX_FACE_NY,
  VX_FACE_PZ,
  VX_FACE_NZ,
  VX_FACE_PLANT,
};

// tile textures: id, name, painted variants (on consecutive array layers)
#define VX_TEXTURE_LIST(X)                                                     \
  X(DIRT, "dirt", 1)                                                           \
  X(GRASS_TOP, "grass_top", 1)                                                 \
  X(GRASS_SIDE, "grass_side", 1)                                               \
  X(STONE, "stone", 1)                                                         \
  X(CLAY, "clay", 1)                                                           \
  X(MUD, "mud", 1)                                                             \
  X(JUNGLE_GRASS_TOP, "jungle_grass_top", 1)                                   \
  X(JUNGLE_GRASS_SIDE, "jungle_grass_side", 1)                                 \
  X(MUSHROOM_GRASS_TOP, "mushroom_grass_top", 1)                               \
  X(MUSHROOM_GRASS_SIDE, "mushroom_grass_side", 1)                             \
  X(CORRUPT_GRASS_TOP, "corrupt_grass_top", 1)                                 \
  X(CORRUPT_GRASS_SIDE, "corrupt_grass_side", 1)                               \
  X(EBONSTONE, "ebonstone", 1)                                                 \
  X(SAND, "sand", 1)                                                           \
  X(SAND_SIDE, "sand_side", 1)                                                 \
  X(SANDSTONE, "sandstone", 1)                                                 \
  X(SNOW, "snow", 1)                                                           \
  X(ICE, "ice", 1)                                                             \
  X(PINK_ICE, "pink_ice", 1)                                                   \
  X(AETHERIUM, "aetherium", 1)                                                 \
  X(DUNGEON_BRICK, "dungeon_brick", 4)                                         \
  X(COBWEB, "cobweb", 1)                                                       \
  X(ASH, "ash", 1)                                                             \
  X(HELLSTONE, "hellstone", 1)                                                 \
  X(OBSIDIAN, "obsidian", 1)                                                   \
  X(OBSIDIAN_BRICK, "obsidian_brick", 1)                                       \
  X(HELLSTONE_BRICK, "hellstone_brick", 1)                                     \
  X(LOG_SIDE, "log_side", 1)                                                   \
  X(LOG_TOP, "log_top", 1)                                                     \
  X(BOREAL_LOG_SIDE, "boreal_log_side", 1)                                     \
  X(BOREAL_LOG_TOP, "boreal_log_top", 1)                                       \
  X(MAHOGANY_LOG_SIDE, "mahogany_log_side", 1)                                 \
  X(MAHOGANY_LOG_TOP, "mahogany_log_top", 1)                                   \
  X(PALM_LOG_SIDE, "palm_log_side", 1)                                         \
  X(PALM_LOG_TOP, "palm_log_top", 1)                                           \
  X(EBON_LOG_SIDE, "ebon_log_side", 1)                                         \
  X(EBON_LOG_TOP, "ebon_log_top", 1)                                           \
  X(PLANKS, "planks", 1)                                                       \
  X(BOREAL_PLANKS, "boreal_planks", 1)                                         \
  X(MAHOGANY_PLANKS, "mahogany_planks", 1)                                     \
  X(LEAVES, "leaves", 1)                                                       \
  X(BOREAL_LEAVES, "boreal_leaves", 1)                                         \
  X(JUNGLE_LEAVES, "jungle_leaves", 1)                                         \
  X(PALM_LEAVES, "palm_leaves", 1)                                             \
  X(CORRUPT_LEAVES, "corrupt_leaves", 1)                                       \
  X(CACTUS_SIDE, "cactus_side", 1)                                             \
  X(CACTUS_TOP, "cactus_top", 1)                                               \
  X(COPPER_ORE, "copper_ore", 1)                                               \
  X(TIN_ORE, "tin_ore", 1)                                                     \
  X(IRON_ORE, "iron_ore", 1)                                                   \
  X(SILVER_ORE, "silver_ore", 1)                                               \
  X(GOLD_ORE, "gold_ore", 1)                                                   \
  X(DEMONITE_ORE, "demonite_ore", 1)                                           \
  X(AMETHYST, "amethyst", 1)                                                   \
  X(SAPPHIRE, "sapphire", 1)                                                   \
  X(RUBY, "ruby", 1)                                                           \
  X(EMERALD, "emerald", 1)                                                     \
  X(GRAY_BRICK, "gray_brick", 1)                                               \
  X(RED_BRICK, "red_brick", 1)                                                 \
  X(CLOUD, "cloud", 1)                                                         \
  X(SUNPLATE, "sunplate", 1)                                                   \
  X(GLASS, "glass", 1)                                                         \
  X(TORCH, "torch", 1)                                                         \
  X(ICE_TORCH, "ice_torch", 1)                                                 \
  X(GRASS_PLANT, "grass_plant", 3)                                             \
  X(FLOWERS, "flowers", 3)                                                     \
  X(JUNGLE_PLANT, "jungle_plant", 3)                                           \
  X(CORRUPT_PLANT, "corrupt_plant", 3)                                         \
  X(MUSHROOM, "mushroom", 3)                                                   \
  X(GLOW_MUSHROOM, "glow_mushroom", 3)                                         \
  X(GLOW_CAP, "glow_cap", 1)                                                   \
  X(GLOW_STEM, "glow_stem", 1)                                                 \
  X(VINE, "vine", 3)                                                           \
  X(LIFE_CRYSTAL, "life_crystal", 1)                                           \
  X(POT, "pot", 1)                                                             \
  X(POT_TOP, "pot_top", 1)                                                     \
  X(CHEST_SIDE, "chest_side", 1)                                               \
  X(CHEST_TOP, "chest_top", 1)                                                 \
  X(CHEST_FRONT, "chest_front", 1)                                             \
  X(GOLD_CHEST_SIDE, "gold_chest_side", 1)                                     \
  X(GOLD_CHEST_TOP, "gold_chest_top", 1)                                       \
  X(GOLD_CHEST_FRONT, "gold_chest_front", 1)                                   \
  X(DOOR, "door", 1)                                                           \
  X(FURNACE, "furnace", 1)                                                     \
  X(FURNACE_FRONT, "furnace_front", 1)                                         \
  X(ANVIL, "anvil", 1)                                                         \
  X(WATER, "water", 1)                                                         \
  X(LAVA, "lava", 1)                                                           \
  X(HONEY, "honey", 1)                                                         \
  X(SHIMMER, "shimmer", 1)                                                     \
  X(HIVE, "hive", 1)                                                           \
  X(MESA_TOP, "mesa_top", 4)                                                   \
  X(MESA_SIDE, "mesa_side", 1)                                                 \
  X(BUTTE, "butte", 1)                                                         \
  X(HALLOW_GRASS_TOP, "hallow_grass_top", 1)                                   \
  X(HALLOW_GRASS_SIDE, "hallow_grass_side", 1)                                 \
  X(PEARL_LOG_SIDE, "pearl_log_side", 1)                                       \
  X(PEARL_LOG_TOP, "pearl_log_top", 1)                                         \
  X(HALLOW_LEAVES, "hallow_leaves", 1)                                         \
  X(GOLD_LEAVES, "gold_leaves", 1)                                             \
  X(HALLOW_PLANT, "hallow_plant", 3)                                           \
  X(COBBLE, "cobble", 1)                                                       \
  X(COPPER, "copper", 1)                                                       \
  X(LIMESTONE_BRICK, "limestone_brick", 1)                                     \
  X(PEARLSAND, "pearlsand", 1)                                                 \
  X(SLATE, "slate", 1)                                                         \
  X(BANNER, "banner", 16)                                                      \
  X(FURN_WOOD, "furn_wood", 16)                                                \
  X(FURN_CLOTH, "furn_cloth", 16)                                              \
  X(BOOKS, "books", 1)                                                         \
  X(MARBLE, "marble", 1)                                                       \
  X(PYLON, "pylon", 1)                                                           X(LIGHT_BOX_SIDE, "light_box_side", 1)                                         X(LIGHT_BOX_TOP, "light_box_top", 1)                                         \
  X(SIGN, "sign", 16)                                                          \
  X(PART_MOTE, "part_mote", 1)                                                 \
  X(PART_PUFF, "part_puff", 1)                                                 \
  X(PART_SPARK, "part_spark", 1)                                               \
  X(PART_FLAKE, "part_flake", 1)                                               \
  X(PART_LEAF, "part_leaf", 1)                                                 \
  X(PUPPET, "puppet", 1)                                                 \
  X(CRACK, "crack", 3)

typedef enum {
#define VX_X(id, name, v) T_##id,
  VX_TEXTURE_LIST(VX_X)
#undef VX_X
  T_COUNT
} VxTexture;

// the array layer of each texture's first variant, counted at compile time
enum {
#define VX_X(id, name, v) L_##id, L_##id##_LAST = L_##id + (v) - 1,
  VX_TEXTURE_LIST(VX_X)
#undef VX_X
  VX_TEX_LAYERS
};
// webgpu guarantees 256 array layers, and so does every mobile gpu we target
static_assert(VX_TEX_LAYERS <= 256, "the tile array must fit in 256 layers");
#define VX_TEX_MAX_VARIANTS 3
extern const u16 vx_tex_layer[T_COUNT];
extern const u8 vx_tex_variants[T_COUNT];
// layer of variant v (any value, wrapped) of texture t
force_inline u32 vx_tex_layer_of(u32 t, u32 v) { return vx_tex_layer[t] + v % vx_tex_variants[t]; }

// id label shape layer solid occludes opacity light(r g b, 0-15) wave replaceable inventory side top bottom
#define VX_BLOCK_LIST(X)                                                                                                                              \
  X(AIR, "Air", VX_SHAPE_AIR, VX_LAYER_NONE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 1, 0, T_STONE, T_STONE, T_STONE)                                         \
  X(DIRT, "Dirt Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_DIRT, T_DIRT, T_DIRT)                                \
  X(GRASS, "Grass Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_GRASS_SIDE, T_GRASS_TOP, T_DIRT)                   \
  X(STONE, "Stone Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_STONE, T_STONE, T_STONE)                           \
  X(CLAY, "Clay Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_CLAY, T_CLAY, T_CLAY)                                \
  X(MUD, "Mud Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_MUD, T_MUD, T_MUD)                                     \
  X(JUNGLE_GRASS, "Jungle Grass", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_JUNGLE_GRASS_SIDE, T_JUNGLE_GRASS_TOP, T_MUD) \
  X(MUSHROOM_GRASS, "Mushroom Grass", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 4, 6, 9, VX_WAVE_NONE, 0, 1, T_MUSHROOM_GRASS_SIDE, T_MUSHROOM_GRASS_TOP, T_MUD) \
  X(CORRUPT_GRASS, "Corrupt Grass", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_CORRUPT_GRASS_SIDE, T_CORRUPT_GRASS_TOP, T_DIRT) \
  X(EBONSTONE, "Ebonstone Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_EBONSTONE, T_EBONSTONE, T_EBONSTONE)       \
  X(SAND, "Sand Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_SAND_SIDE, T_SAND, T_SAND)                                \
  X(SANDSTONE, "Sandstone Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_SANDSTONE, T_SANDSTONE, T_SANDSTONE)       \
  X(SNOW, "Snow Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_SNOW, T_SNOW, T_SNOW)                                \
  X(ICE, "Ice Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_ICE, T_ICE, T_ICE)                                     \
  X(ASH, "Ash Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_ASH, T_ASH, T_ASH)                                     \
  X(HELLSTONE, "Hellstone", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 5, 2, 1, VX_WAVE_NONE, 0, 1, T_HELLSTONE, T_HELLSTONE, T_HELLSTONE)             \
  X(OBSIDIAN, "Obsidian", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_OBSIDIAN, T_OBSIDIAN, T_OBSIDIAN)                  \
  X(OBSIDIAN_BRICK, "Obsidian Brick", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_OBSIDIAN_BRICK, T_OBSIDIAN_BRICK, T_OBSIDIAN_BRICK) \
  X(HELLSTONE_BRICK, "Hellstone Brick", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 9, 2, 1, VX_WAVE_NONE, 0, 1, T_HELLSTONE_BRICK, T_HELLSTONE_BRICK, T_HELLSTONE_BRICK) \
  X(LOG, "Tree Trunk", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_LOG_SIDE, T_LOG_TOP, T_LOG_TOP)                             \
  X(BOREAL_LOG, "Boreal Trunk", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_BOREAL_LOG_SIDE, T_BOREAL_LOG_TOP, T_BOREAL_LOG_TOP) \
  X(MAHOGANY_LOG, "Mahogany Trunk", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_MAHOGANY_LOG_SIDE, T_MAHOGANY_LOG_TOP, T_MAHOGANY_LOG_TOP) \
  X(PALM_LOG, "Palm Wood", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_PALM_LOG_SIDE, T_PALM_LOG_TOP, T_PALM_LOG_TOP)    \
  X(EBON_LOG, "Ebonwood", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_EBON_LOG_SIDE, T_EBON_LOG_TOP, T_EBON_LOG_TOP)       \
  X(PLANKS, "Wood", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_PLANKS, T_PLANKS, T_PLANKS)                       \
  X(BOREAL_PLANKS, "Boreal Wood", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_BOREAL_PLANKS, T_BOREAL_PLANKS, T_BOREAL_PLANKS) \
  X(MAHOGANY_PLANKS, "Rich Mahogany", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_MAHOGANY_PLANKS, T_MAHOGANY_PLANKS, T_MAHOGANY_PLANKS) \
  X(LEAVES, "Leaves", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 1, 0, 0, 0, VX_WAVE_LEAVES, 0, 1, T_LEAVES, T_LEAVES, T_LEAVES)                           \
  X(BOREAL_LEAVES, "Boreal Leaves", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 1, 0, 0, 0, VX_WAVE_LEAVES, 0, 1, T_BOREAL_LEAVES, T_BOREAL_LEAVES, T_BOREAL_LEAVES) \
  X(JUNGLE_LEAVES, "Jungle Leaves", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 1, 0, 0, 0, VX_WAVE_LEAVES, 0, 1, T_JUNGLE_LEAVES, T_JUNGLE_LEAVES, T_JUNGLE_LEAVES) \
  X(PALM_LEAVES, "Palm Leaves", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 1, 0, 0, 0, VX_WAVE_LEAVES, 0, 1, T_PALM_LEAVES, T_PALM_LEAVES, T_PALM_LEAVES)  \
  X(CORRUPT_LEAVES, "Ebonwood Leaves", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 1, 0, 0, 0, VX_WAVE_LEAVES, 0, 1, T_CORRUPT_LEAVES, T_CORRUPT_LEAVES, T_CORRUPT_LEAVES) \
  X(CACTUS, "Cactus", VX_SHAPE_CUBE, VX_LAYER_CUTOUT, 1, 0, 2, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_CACTUS_SIDE, T_CACTUS_TOP, T_CACTUS_TOP)                \
  X(COPPER_ORE, "Copper Ore", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_COPPER_ORE, T_COPPER_ORE, T_COPPER_ORE)        \
  X(TIN_ORE, "Tin Ore", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_TIN_ORE, T_TIN_ORE, T_TIN_ORE)                       \
  X(IRON_ORE, "Iron Ore", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_IRON_ORE, T_IRON_ORE, T_IRON_ORE)                  \
  X(SILVER_ORE, "Silver Ore", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_SILVER_ORE, T_SILVER_ORE, T_SILVER_ORE)        \
  X(GOLD_ORE, "Gold Ore", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_GOLD_ORE, T_GOLD_ORE, T_GOLD_ORE)                  \
  X(DEMONITE_ORE, "Demonite Ore", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 7, 2, 10, VX_WAVE_NONE, 0, 1, T_DEMONITE_ORE, T_DEMONITE_ORE, T_DEMONITE_ORE) \
  X(AMETHYST, "Amethyst Stone", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_AMETHYST, T_AMETHYST, T_AMETHYST)                  \
  X(SAPPHIRE, "Sapphire Stone", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_SAPPHIRE, T_SAPPHIRE, T_SAPPHIRE)                  \
  X(RUBY, "Ruby Stone", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_RUBY, T_RUBY, T_RUBY)                                      \
  X(EMERALD, "Emerald Stone", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_EMERALD, T_EMERALD, T_EMERALD)                       \
  X(GRAY_BRICK, "Gray Brick", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_GRAY_BRICK, T_GRAY_BRICK, T_GRAY_BRICK)        \
  X(RED_BRICK, "Red Brick", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_RED_BRICK, T_RED_BRICK, T_RED_BRICK)             \
  X(CLOUD, "Cloud", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_CLOUD, T_CLOUD, T_CLOUD)                                 \
  X(SUNPLATE, "Sunplate Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_SUNPLATE, T_SUNPLATE, T_SUNPLATE)            \
  X(GLASS, "Glass", VX_SHAPE_CUBE, VX_LAYER_CUTOUT, 1, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_GLASS, T_GLASS, T_GLASS)                                  \
  X(TORCH, "Torch", VX_SHAPE_TORCH, VX_LAYER_CUTOUT, 0, 0, 0, 15, 13, 10, VX_WAVE_NONE, 0, 1, T_TORCH, T_TORCH, T_TORCH)                              \
  X(ICE_TORCH, "Ice Torch", VX_SHAPE_TORCH, VX_LAYER_CUTOUT, 0, 0, 0, 11, 14, 15, VX_WAVE_NONE, 0, 1, T_ICE_TORCH, T_ICE_TORCH, T_ICE_TORCH)          \
  X(GRASS_PLANT, "Grass", VX_SHAPE_CROSS, VX_LAYER_CUTOUT, 0, 0, 0, 0, 0, 0, VX_WAVE_PLANT, 1, 0, T_GRASS_PLANT, T_GRASS_PLANT, T_GRASS_PLANT)        \
  X(FLOWERS, "Flowers", VX_SHAPE_CROSS, VX_LAYER_CUTOUT, 0, 0, 0, 0, 0, 0, VX_WAVE_PLANT, 1, 0, T_FLOWERS, T_FLOWERS, T_FLOWERS)                      \
  X(JUNGLE_PLANT, "Jungle Plant", VX_SHAPE_CROSS, VX_LAYER_CUTOUT, 0, 0, 0, 0, 0, 0, VX_WAVE_PLANT, 1, 0, T_JUNGLE_PLANT, T_JUNGLE_PLANT, T_JUNGLE_PLANT) \
  X(CORRUPT_PLANT, "Vile Weed", VX_SHAPE_CROSS, VX_LAYER_CUTOUT, 0, 0, 0, 0, 0, 0, VX_WAVE_PLANT, 1, 0, T_CORRUPT_PLANT, T_CORRUPT_PLANT, T_CORRUPT_PLANT) \
  X(MUSHROOM, "Mushroom", VX_SHAPE_CROSS, VX_LAYER_CUTOUT, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 1, 1, T_MUSHROOM, T_MUSHROOM, T_MUSHROOM)                  \
  X(GLOW_MUSHROOM, "Glowing Mushroom", VX_SHAPE_CROSS, VX_LAYER_CUTOUT, 0, 0, 0, 7, 10, 13, VX_WAVE_NONE, 1, 1, T_GLOW_MUSHROOM, T_GLOW_MUSHROOM, T_GLOW_MUSHROOM) \
  X(GLOW_CAP, "Giant Mushroom Cap", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 9, 11, 14, VX_WAVE_NONE, 0, 1, T_GLOW_CAP, T_GLOW_CAP, T_GLOW_CAP)       \
  X(GLOW_STEM, "Giant Mushroom Stem", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_GLOW_STEM, T_GLOW_STEM, T_GLOW_STEM)  \
  X(VINE, "Vine", VX_SHAPE_CROSS, VX_LAYER_CUTOUT, 0, 0, 0, 0, 0, 0, VX_WAVE_PLANT, 1, 0, T_VINE, T_VINE, T_VINE)                                    \
  X(LIFE_CRYSTAL, "Life Crystal", VX_SHAPE_CROSS, VX_LAYER_CUTOUT, 0, 0, 0, 12, 7, 10, VX_WAVE_NONE, 0, 1, T_LIFE_CRYSTAL, T_LIFE_CRYSTAL, T_LIFE_CRYSTAL) \
  X(POT, "Pot", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_POT, T_POT_TOP, T_POT)                                         \
  X(CHEST, "Chest", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 1, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_CHEST_SIDE, T_CHEST_TOP, T_CHEST_SIDE)                     \
  X(GOLD_CHEST, "Gold Chest", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 1, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_GOLD_CHEST_SIDE, T_GOLD_CHEST_TOP, T_GOLD_CHEST_SIDE) \
  X(PLATFORM, "Wood Platform", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_PLANKS, T_PLANKS, T_PLANKS)                    \
  X(DOOR, "Wooden Door", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 1, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_DOOR, T_PLANKS, T_PLANKS)                            \
  X(DOOR_OPEN, "Open Door", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_DOOR, T_PLANKS, T_PLANKS)                         \
  X(WORKBENCH, "Work Bench", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_PLANKS, T_PLANKS, T_PLANKS)                      \
  X(TABLE, "Wooden Table", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_PLANKS, T_PLANKS, T_PLANKS)                        \
  X(CHAIR, "Wooden Chair", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_PLANKS, T_PLANKS, T_PLANKS)                        \
  X(FURNACE, "Furnace", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 13, 7, 2, VX_WAVE_NONE, 0, 1, T_FURNACE, T_FURNACE, T_FURNACE)                       \
  X(ANVIL, "Iron Anvil", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_ANVIL, T_ANVIL, T_ANVIL)                             \
  X(WATER, "Water", VX_SHAPE_LIQUID, VX_LAYER_TRANSLUCENT, 0, 0, 2, 0, 0, 0, VX_WAVE_NONE, 1, 1, T_WATER, T_WATER, T_WATER)                         \
  X(LAVA, "Lava", VX_SHAPE_LIQUID, VX_LAYER_OPAQUE, 0, 0, 15, 15, 9, 3, VX_WAVE_NONE, 1, 1, T_LAVA, T_LAVA, T_LAVA)                                  \
  X(HONEY, "Honey", VX_SHAPE_LIQUID, VX_LAYER_TRANSLUCENT, 0, 0, 3, 0, 0, 0, VX_WAVE_NONE, 1, 1, T_HONEY, T_HONEY, T_HONEY)                         \
  X(HIVE, "Hive", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_HIVE, T_HIVE, T_HIVE) \
  X(MESA, "Red Sandstone", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_MESA_SIDE, T_MESA_TOP, T_MESA_SIDE) \
  X(HALLOW_GRASS, "Hallowed Grass", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_HALLOW_GRASS_SIDE, T_HALLOW_GRASS_TOP, T_DIRT) \
  X(PEARL_LOG, "Pearlwood", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_PEARL_LOG_SIDE, T_PEARL_LOG_TOP, T_PEARL_LOG_TOP) \
  X(HALLOW_LEAVES, "Pearlwood Leaves", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 1, 0, 0, 0, VX_WAVE_LEAVES, 0, 1, T_HALLOW_LEAVES, T_HALLOW_LEAVES, T_HALLOW_LEAVES) \
  X(GOLD_LEAVES, "Golden Leaves", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 1, 0, 0, 0, VX_WAVE_LEAVES, 0, 1, T_GOLD_LEAVES, T_GOLD_LEAVES, T_GOLD_LEAVES) \
  X(HALLOW_PLANT, "Hallowed Grass Tuft", VX_SHAPE_CROSS, VX_LAYER_CUTOUT, 0, 0, 0, 0, 0, 0, VX_WAVE_PLANT, 1, 0, T_HALLOW_PLANT, T_HALLOW_PLANT, T_HALLOW_PLANT) \
  X(COBBLE, "Stone Slab", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_COBBLE, T_COBBLE, T_COBBLE) \
  X(COPPER_POST, "Copper Post", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_COPPER, T_COPPER, T_COPPER) \
  X(COPPER_BEAM_X, "Copper Beam", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 0, T_COPPER, T_COPPER, T_COPPER) \
  X(COPPER_BEAM_Z, "Copper Beam", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 0, T_COPPER, T_COPPER, T_COPPER) \
  X(LIMESTONE_BRICK, "Limestone Brick", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_LIMESTONE_BRICK, T_LIMESTONE_BRICK, T_LIMESTONE_BRICK) \
  X(PEARLSAND, "Pearlsand Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_PEARLSAND, T_PEARLSAND, T_PEARLSAND) \
  X(SLATE, "Slate Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_SLATE, T_SLATE, T_SLATE)            \
  X(BANNER, "Banner Wall", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_GRAY_BRICK, T_GRAY_BRICK, T_GRAY_BRICK) \
  X(BED, "Bed", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_FURN_WOOD, T_FURN_WOOD, T_FURN_WOOD)                                  \
  X(BOOKCASE, "Bookcase", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_FURN_WOOD, T_FURN_WOOD, T_FURN_WOOD)                        \
  X(SOFA, "Sofa", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_FURN_WOOD, T_FURN_WOOD, T_FURN_WOOD)                                \
  X(LAMP, "Lamp", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 12, 10, 7, VX_WAVE_NONE, 0, 1, T_FURN_WOOD, T_FURN_WOOD, T_FURN_WOOD)                              \
  X(CLOCK, "Grandfather Clock", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_FURN_WOOD, T_FURN_WOOD, T_FURN_WOOD)                  \
  X(PIANO, "Piano", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_FURN_WOOD, T_FURN_WOOD, T_FURN_WOOD) \
  X(MARBLE, "Marble Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_MARBLE, T_MARBLE, T_MARBLE)        \
  X(PYLON, "Pylon", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 9, 10, 13, VX_WAVE_NONE, 0, 1, T_PYLON, T_PYLON, T_PYLON)                   X(LIGHT_BOX, "Light Box", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 10, 13, 15, VX_WAVE_NONE, 0, 1, T_LIGHT_BOX_SIDE, T_LIGHT_BOX_TOP, T_LIGHT_BOX_SIDE) \
  X(SIGN, "Sign", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_PLANKS, T_PLANKS, T_PLANKS) \
  X(BUTTE, "Sandstone Cliff", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_BUTTE, T_MESA_TOP, T_BUTTE) \
  X(PINK_ICE, "Pink Ice Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_PINK_ICE, T_PINK_ICE, T_PINK_ICE) \
  X(SHIMMER, "Shimmer", VX_SHAPE_LIQUID, VX_LAYER_TRANSLUCENT, 0, 0, 2, 13, 11, 15, VX_WAVE_NONE, 1, 1, T_SHIMMER, T_SHIMMER, T_SHIMMER) \
  X(AETHERIUM, "Aetherium Block", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_AETHERIUM, T_AETHERIUM, T_AETHERIUM) \
  X(DUNGEON_BRICK, "Green Dungeon Brick", VX_SHAPE_CUBE, VX_LAYER_OPAQUE, 1, 1, 15, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_DUNGEON_BRICK, T_DUNGEON_BRICK, T_DUNGEON_BRICK) \
  X(COBWEB, "Cobweb", VX_SHAPE_CROSS, VX_LAYER_CUTOUT, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 1, 0, T_COBWEB, T_COBWEB, T_COBWEB) \
  X(WOOD_POST, "Wooden Post", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 1, T_LOG_SIDE, T_LOG_TOP, T_LOG_TOP) \
  X(WOOD_BEAM_X, "Wooden Beam", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 0, T_PLANKS, T_PLANKS, T_PLANKS) \
  X(WOOD_BEAM_Z, "Wooden Beam", VX_SHAPE_MODEL, VX_LAYER_OPAQUE, 0, 0, 0, 0, 0, 0, VX_WAVE_NONE, 0, 0, T_PLANKS, T_PLANKS, T_PLANKS)

typedef enum {
#define VX_X(id, label, sh, ly, so, oc, op, lr, lg, lb, wa, re, in, ts, tt, tb) B_##id,
  VX_BLOCK_LIST(VX_X)
#undef VX_X
  B_COUNT
} VxBlock;

// lookup tables over every u8 id: ids past B_COUNT behave like stone
#define VX_BLOCK_IDS 256
extern const u8 vx_shape[VX_BLOCK_IDS];
extern const u8 vx_layer[VX_BLOCK_IDS];
extern const u8 vx_solid[VX_BLOCK_IDS];
extern const u8 vx_occludes[VX_BLOCK_IDS];
extern const u8 vx_light_opacity[VX_BLOCK_IDS];
// emitted light as r | g << 4 | b << 8, the voxel light word's low 12 bits
extern const u16 vx_emission[VX_BLOCK_IDS];
extern const u8 vx_wave[VX_BLOCK_IDS];
extern const u8 vx_replaceable[VX_BLOCK_IDS];
extern const u8 vx_face_tex[VX_BLOCK_IDS][6];

// block models: boxes in 1/16 block units, hi exclusive; a face texture of VX_BOX_TEX_BLOCK uses the block's own
#define VX_BOX_TEX_BLOCK 255
#define VX_BOX_TEX_NONE 254 // a face not drawn
typedef struct {
  u8 lo[3], hi[3];
  u8 tex[6];
} VxBox;
typedef struct {
  u8 first, count;
} VxModel;
extern const VxBox vx_boxes[];
extern const VxModel vx_model[VX_BLOCK_IDS];
extern const char *const vx_block_label[B_COUNT];
extern const u8 vx_block_in_inventory[B_COUNT];
extern const char *const vx_texture_name[T_COUNT];


force_inline b32 vx_needs_support(u8 id) {
  return vx_shape[id] == VX_SHAPE_CROSS || vx_shape[id] == VX_SHAPE_TORCH ||
         (vx_shape[id] == VX_SHAPE_MODEL && id != B_PLATFORM);
}

// furniture: a model over w x h cells one deep, boxes in 1/16 units over the footprint, cut per cell when meshed
typedef struct {
  u8 w, h, first, count;
} VxFurniture;
extern const VxFurniture vx_furniture[VX_BLOCK_IDS];
extern const VxBox vx_furn_boxes[];
// a banner block's state: the face its banner hangs on in bits 0-2, the banner layer above; a layer holds two banners, the block column picks one
#define VX_BANNER_FACE(state) ((u32)(state) & 7u)
#define VX_BANNER_LAYER(state) ((u32)(state) >> 3)
// a sign's state: quarter turns | word << 2, the word painted on its board
typedef enum {
  VX_SIGN_SKY_ISLAND,
  VX_SIGN_CORRUPTION,
  VX_SIGN_HALLOW,
  VX_SIGN_MARKET,
  VX_SIGN_DESERT,
  VX_SIGN_JUNGLE,
  VX_SIGN_SNOW,
  VX_SIGN_OCEAN,
  VX_SIGN_TOWN,
  VX_SIGN_MUSEUM,
  VX_SIGN_DUNGEON,
  VX_SIGN_ARMOURY,
  VX_SIGN_BANNERS,
  VX_SIGN_PYLONS,
  VX_SIGN_MUSHROOMS,
  VX_SIGN_UNDERWORLD,
  VX_SIGN_COUNT,
} VxSignWord;
#define VX_SIGN_STATE(rot, word) ((u8)((rot) | (word) << 2))
#define VX_FURN_PART(state) (((u32)(state) >> 2) & 15u) // state: quarter turn in bits 0-1, part px + py * w above
#define VX_FURN_SHOW 64u // a showpiece: its wood and cloth take the material of the 4 x 8 cell its origin stands in
force_inline b32 vx_is_furniture(u8 id) { return vx_furniture[id].w != 0; }
force_inline b32 vx_is_door(u8 id) { return id == B_DOOR || id == B_DOOR_OPEN; }

// ---- hashing / rng (32-bit wrapping multiplies) ----

force_inline u32 vx_hash2i(i32 x, i32 z, u32 seed) {
  u32 h = seed ^ ((u32)x * 0x27d4eb2du) ^ ((u32)z * 0x165667b1u);
  h = (h ^ (h >> 15)) * 0x2c1b3c6du;
  h = (h ^ (h >> 12)) * 0x297a2d39u;
  h ^= h >> 15;
  return h;
}

force_inline u32 vx_hash3i(i32 x, i32 y, i32 z, u32 seed) {
  u32 h = seed ^ ((u32)x * 0x27d4eb2du) ^ ((u32)y * 0x9e3779b1u) ^
          ((u32)z * 0x165667b1u);
  h = (h ^ (h >> 16)) * 0x85ebca6bu;
  h = (h ^ (h >> 13)) * 0xc2b2ae35u;
  h ^= h >> 16;
  return h;
}

force_inline f64 vx_hash2(i32 x, i32 z, u32 seed) {
  return (f64)vx_hash2i(x, z, seed) / 4294967296.0;
}

typedef struct {
  u32 a;
} VxRng;

force_inline VxRng vx_rng(u32 seed) { return (VxRng){seed}; }

// mulberry32, [0, 1)
force_inline f64 vx_rng_next(VxRng *r) {
  r->a += 0x6d2b79f5u;
  u32 t = r->a;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return (f64)(t ^ (t >> 14)) / 4294967296.0;
}

force_inline f32 vx_rng_nextf(VxRng *r) { return (f32)vx_rng_next(r); }

u32 vx_hash_string(const char *s);

force_inline i32 vx_floor_i(f64 x) {
  i32 i = (i32)x;
  return (x < (f64)i) ? i - 1 : i;
}

force_inline f64 vx_clamp(f64 x, f64 a, f64 b) { return x < a ? a : x > b ? b : x; }
force_inline f64 vx_lerp(f64 a, f64 b, f64 t) { return a + (b - a) * t; }
force_inline f64 vx_smoothstep(f64 e0, f64 e1, f64 x) {
  f64 t = vx_clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

// ---- noise ----

typedef struct {
  u8 *perm;       // 512
  u8 *perm_mod12; // 512
} VxSimplex;

void vx_simplex_init(VxSimplex *s, u32 seed, Allocator *alloc);
f64 vx_noise2(const VxSimplex *s, f64 x, f64 y);
f64 vx_noise3(const VxSimplex *s, f64 x, f64 y, f64 z);
f64 vx_fbm2(const VxSimplex *s, f64 x, f64 y, u32 octaves);
f64 vx_ridged2(const VxSimplex *s, f64 x, f64 y, u32 octaves);

// ---- terrain generation: a bounded island in Terraria's layers ----

#define VX_ISLAND_RADIUS 384
// the playable square is 2 * VX_WORLD_HALF blocks across; outside it the ocean only streams in as scenery
#define VX_WORLD_HALF 384
force_inline b32 vx_in_world(i32 x, i32 z) { return x >= -VX_WORLD_HALF && x < VX_WORLD_HALF && z >= -VX_WORLD_HALF && z < VX_WORLD_HALF; }
#define VX_SURFACE_Y 150
#define VX_UNDERGROUND_Y 120
#define VX_CAVERN_Y 48
#define VX_LAVA_Y 14

typedef enum {
  VX_BIOME_FOREST,
  VX_BIOME_DESERT,
  VX_BIOME_SNOW,
  VX_BIOME_JUNGLE,
  VX_BIOME_CORRUPTION,
  VX_BIOME_OCEAN,
  VX_BIOME_MUSHROOM,
  VX_BIOME_HALLOW,
  VX_BIOME_COUNT,
} VxBiome;

extern const char *const vx_biome_name[VX_BIOME_COUNT];

typedef enum {
  VX_LM_NONE,
  VX_LM_BUTTE,   // a red-capped sandstone butte: an outer tier round a taller core
  VX_LM_PILLAR,  // a lone sandstone pillar with a red cap
  VX_LM_PYRAMID, // a stepped sandstone pyramid half sunk in the dunes
  VX_LM_OASIS,   // a pit in the sand with a pool at its bottom
  VX_LM_TERRACE, // a steep round jungle mountain of stone stepped into grassy ledges
  VX_LM_SPIRE,   // a lone stone spire in the jungle with a grass cap
  VX_LM_PEAK,    // a steep four-faced peak of slate and ice in the snow
  VX_LM_CREVASSE, // a long crack in the snow walled in pink ice
} VxLandmarkKind;

// a desert, jungle or snow landmark, placed once per seed: centre, radii, the ground it stands on and its heights above it
typedef struct {
  f32 x, z, r, r2;
  i32 base, h1, h2;
  u8 kind;
} VxLandmark;

#define VX_LANDMARKS 16

typedef struct {
  i32 height;
  i32 ground;   // the height before any landmark
  u8 biome;
  u8 landmark;  // VxLandmarkKind of the column's landmark
  u8 water;     // water fills up to this row
  f32 coast;    // 0 inland, 1 open ocean
  f32 blend;    // 0 deep inside the biome, 1 at a border
} VxColumnSample;

typedef enum {
  VX_NOISE_WARP_X,
  VX_NOISE_WARP_Z,
  VX_NOISE_HILLS,
  VX_NOISE_DETAIL,
  VX_NOISE_COAST,
  VX_NOISE_SECTOR,
  VX_NOISE_TUNNEL_A,
  VX_NOISE_TUNNEL_B,
  VX_NOISE_CAVERN,
  VX_NOISE_CAVERN_FINE,
  VX_NOISE_MUSHROOM,
  VX_NOISE_HELL,
  VX_NOISE_PATCH,
  VX_NOISE_DIRT,
  VX_NOISE_COUNT,
} VxNoise;

#define VX_MUSHROOM_POCKETS 4

typedef struct {
  u32 seed;
  f32 sector_angle; // the seed's rotation of the biome ring
  v4 mushroom[VX_MUSHROOM_POCKETS]; // glowing mushroom pockets: centre xyz, radius w
  v4 aether;                        // the shimmer cave: centre xz, shimmer level y, radius w
  v4 dungeon;                       // the dungeon: centre xz, floor y, half-size w
  VxLandmark landmarks[VX_LANDMARKS];
  u32 landmark_count;
  i32 town_y;     // the town plateau's ground row, 0 while it is being planned
  i32 armoury_y;  // the armoury's floor row
  VxSimplex n[VX_NOISE_COUNT];
} VxTerrain;

#define VX_GEN_MARGIN 4
#define VX_GEN_SPAN (VX_CHUNK + VX_GEN_MARGIN * 2)
#define VX_CAVE_GX 5
#define VX_CAVE_GY 65

// an underground lake: centre, radius and water level
typedef struct {
  f64 x, z, r;
  i32 level;
  b32 ok;
} VxLake;

#define VX_GEN_LAKES 4

// per-lane generation scratch
typedef struct {
  i32 h[VX_GEN_SPAN * VX_GEN_SPAN];
  i32 ground[VX_GEN_SPAN * VX_GEN_SPAN];
  u8 biome[VX_GEN_SPAN * VX_GEN_SPAN];
  u8 landmark[VX_GEN_SPAN * VX_GEN_SPAN];
  u8 water[VX_GEN_SPAN * VX_GEN_SPAN];
  f32 coast[VX_GEN_SPAN * VX_GEN_SPAN];
  f32 tun[VX_CAVE_GX * VX_CAVE_GX * VX_CAVE_GY];
  f32 cav[VX_CAVE_GX * VX_CAVE_GX * VX_CAVE_GY];
  f32 soil[VX_CAVE_GX * VX_CAVE_GX * VX_CAVE_GY];
  VxLake lakes[VX_GEN_LAKES]; // the lakes that reach the chunk being generated
} VxGenScratch;

void vx_terrain_init(VxTerrain *t, u32 seed, Allocator *alloc);
// re-seeds in place; the permutation tables keep their storage
void vx_terrain_reseed(VxTerrain *t, u32 seed);
VxColumnSample vx_sample_column(const VxTerrain *t, i32 x, i32 z);
// the town (vx_town.c): plan at reseed, the plateau's pull on the ground, where trees keep off, stamping per chunk
void vx_town_plan(VxTerrain *t);
f64 vx_town_weight(const VxTerrain *T, i32 x, i32 z);
b32 vx_town_clear(i32 x, i32 z);
// a generated block's state byte (a door's or furniture's part and turn, a torch's wall), applied when the chunk lands
typedef struct {
  u16 index;
  u8 id, state;
} VxGenState;
#define VX_GEN_STATES 512
typedef struct {
  VxGenState *items; // VX_GEN_STATES
  u32 count;
} VxGenStates;
void vx_gen_state(VxGenStates *s, i32 lx, i32 y, i32 lz, u8 id, u8 state);
void vx_town_stamp(const VxTerrain *T, u8 *blocks, VxGenStates *states, i32 x0, i32 z0);
// the town's people: the Guide at his hut, the rest keeping market tables on the main street
typedef enum {
  VX_ROLE_GUIDE,
  VX_ROLE_MERCHANT,
  VX_ROLE_CLOTHIER,
  VX_ROLE_DRYAD,
  VX_ROLE_MECHANIC,
  VX_ROLE_DEMOLITIONIST,
  VX_ROLE_COUNT,
} VxTownRole;
// where a role's keeper stands and the way it faces; false for a role without a table or a world without a town
b32 vx_town_keeper(const VxTerrain *T, u32 role, f64 *x, i32 *y, f64 *z, f32 *yaw);
// a room in lot space: world xz = o + u * U + v * V at block centres, v running away from the road, the floor row y
typedef struct {
  f64 ox, oz;
  f32 ux, uz, vx, vz;
  i32 y, w, d;
} VxTownRoom;
// the armoury's ground floor, where the armour stands are shown
b32 vx_town_armoury(const VxTerrain *T, VxTownRoom *r);
// the pylon avenue's centre on the plateau and its long axis (+x); false for a world without a town
b32 vx_town_avenue(const VxTerrain *T, f64 *x, f64 *y, f64 *z, f64 *half_len);
// blocks: VX_CHUNK_VOLUME ids, biome: 16x16 per column, states: structures' state bytes or null; surface: no caves
void vx_generate_chunk(const VxTerrain *t, VxGenScratch *s, i32 cx, i32 cz, u8 *blocks, u8 *biome, VxGenStates *states,
                       b32 surface);
v3 vx_find_spawn(const VxTerrain *t);
// the Guide's hut beside spawn, in blocks: corner, width along x, depth along z
#define VX_HUT_X 6
#define VX_HUT_Z (-5)
#define VX_HUT_W 11
#define VX_HUT_D 9
typedef enum {
  VX_SITE_CABIN,
  VX_SITE_LAKE,
  VX_SITE_SKY_ISLAND,
  VX_SITE_RUIN,
  VX_SITE_MUSHROOM,
  VX_SITE_POND,
  VX_SITE_HIVE,
  VX_SITE_AETHER,
  VX_SITE_DUNGEON,
  VX_SITE_KIND_COUNT,
} VxSiteKind;
// the generated structure of a kind nearest to (x, z): a standing spot inside or beside it
b32 vx_find_site(const VxTerrain *t, u32 kind, i32 x, i32 z, i32 *ox, i32 *oy, i32 *oz);
// biome the player is in: the column's surface biome, or the mushroom pocket around a cavern position
u8 vx_biome_at(const VxTerrain *t, i32 x, i32 y, i32 z);
// ---- chunks ----

typedef enum {
  VX_CHUNK_GENERATING,
  VX_CHUNK_GENERATED,
  VX_CHUNK_LIT,
} VxChunkState;

#define VX_NO_POOL 0xFFFFFFFFu
#define VX_CHUNK_PYLONS 32

// one draw's worth of column geometry: sections packed bottom to top
typedef struct {
  u32 pool;
  u32 alloc_token;
  u32 base;
  u32 start[VX_SECTIONS + 1];
  u16 *meta; // VX_SECTIONS * VX_SECTION_META
} VxMeshPart;

// per-voxel state byte: a liquid's level (0 full, else 1..7 eighths), a torch's wall side, a model's quarter turns
#define VX_STATE_PAGES 4096
enum {
  VX_SIDE_FLOOR,
  VX_SIDE_PX, // mounted on the block at +x
  VX_SIDE_NX,
  VX_SIDE_PZ,
  VX_SIDE_NZ,
};

// voxel light word: block light r | g << 4 | b << 8, sky light << 12
#define VX_LIGHT_SKY_SHIFT 12
#define VX_LIGHT_RGB_MASK 0x0fffu
#define VX_LIGHT_SKY_FULL 0xf000u

typedef struct {
  i32 cx, cz;
  u32 uid;
  u8 state;
  u8 in_use;
  u8 mesh_started;
  u8 modified;
  u8 *blocks;
  u16 *light;
  u8 biome[VX_CHUNK * VX_CHUNK];
  u8 h15[VX_CHUNK * VX_CHUNK];
  i32 section_count[VX_SECTIONS];
  i32 sec_version[VX_SECTIONS];
  i32 sec_meshed[VX_SECTIONS];
  i32 sec_pending[VX_SECTIONS];
  // bumped whenever a section's light changes; the light volume re-uploads sections whose version moved
  u32 light_version[VX_SECTIONS];
  // per-voxel state page of each section, + 1; 0: every voxel of the section has state 0
  u16 state_page[VX_SECTIONS];
  // the generator's state bytes, applied to the pages when its job is integrated
  VxGenState *gen_states; // VX_GEN_STATES
  u32 gen_state_count;
  // pylon cells (block index), for the gems the entity pass floats over them; rescanned on generation and pylon edits
  u16 pylons[VX_CHUNK_PYLONS];
  u32 pylon_count;
  // render side
  VxMeshPart parts[2];
  i32 surface_min;
  u32 render_index;
  u16 urgent_mask;
} VxChunk;

// ---- lighting ----

typedef struct {
  VxChunk *win[9];
  Allocator *alloc;
  u32 *q;
  u32 qcap, qh, qt;
  u32 *rq;
  u32 rcap, rh, rt;
  u32 writes;
} VxLightEngine;

void vx_light_init(VxLightEngine *e, Allocator *alloc);
void vx_light_chunk(VxLightEngine *e, VxChunk *center);
void vx_light_block_changed(VxLightEngine *e, i32 wx, i32 y, i32 wz, u8 new_id);
void vx_chunk_column_fill(VxChunk *c);
void vx_chunk_recount_sections(VxChunk *c);

// ---- meshing ----

#define VX_PAD 18
#define VX_PAD2 (VX_PAD * VX_PAD)
#define VX_PAD3 (VX_PAD2 * VX_PAD)
#define VX_MAX_SECTION_QUADS (VX_SECTION_VOLUME * 6)
// quad record (vx_quad.glsl): w0 x4 z4 y8 face3 shape2 bits3, w1 layer10 flags6 ao8 level4, w2 model box
#define VX_QUAD_U32 3
// culling buckets: plain cube faces by direction, flagged (cutout, waving) cube faces by direction, the rest
#define VX_BUCKETS 13
#define VX_BUCKET_FLAGGED 6
#define VX_BUCKET_OTHER 12
// per section: quad offsets of buckets 0..VX_BUCKETS from the section start, then the mask of non-empty buckets
#define VX_SECTION_META 16
#define VX_META_MASK 14

#define VX_VF_CUTOUT 1u
#define VX_VF_WAVE_LEAF 2u
#define VX_VF_WAVE_PLANT 4u
#define VX_VF_WATER 8u
#define VX_VF_LAVA 16u
#define VX_VF_HONEY 32u

typedef struct {
  u8 *blocks; // VX_PAD3 ids
  u8 *state;  // VX_PAD3 state bytes
  u32 *opaque; // VX_MAX_SECTION_QUADS quads, bucket by bucket
  u32 opaque_quads;
  u32 *trans;
  u32 trans_quads;
  u32 *sort; // VX_MAX_SECTION_QUADS quads
  u16 buckets[2][VX_BUCKETS]; // quads per bucket, opaque then translucent
  i32 cut_lz;                 // the cutaway slab's z inside this section, -1 when it does not pass through
} VxMeshScratch;

void vx_mesh_scratch_init(VxMeshScratch *m, Allocator *alloc);
// meshes the padded volume in m; ox/oy/oz are the section's world origin
void vx_mesh_section(VxMeshScratch *m, i32 ox, i32 oy, i32 oz);

// ---- player edits (persisted, re-applied when a chunk regenerates) ----

typedef struct {
  u16 index;
  u8 id;
  u8 state;
} VxEdit;

typedef struct {
  i32 cx, cz;
  u32 count, cap;
  VxEdit *items;
} VxChunkEdits;

typedef struct {
  Allocator alloc; // tlsf: edit lists grow and move
  VxChunkEdits *chunks;
  u32 chunk_count, chunk_cap;
  u32 *table; // open addressing: chunk key -> index + 1
  u32 table_cap;
} VxEditStore;

void vx_edits_init(VxEditStore *s);
void vx_edits_clear(VxEditStore *s);
void vx_edits_record(VxEditStore *s, i32 cx, i32 cz, u16 index, u8 id, u8 state);
const VxChunkEdits *vx_edits_for_chunk(const VxEditStore *s, i32 cx, i32 cz);

// ---- GPU mesh storage: column parts live in big storage-buffer pools ----

#define VX_MAX_MESH_POOLS 32
#define VX_POOL_QUADS (1u << 20)
#define VX_QUAD_BYTES (VX_QUAD_U32 * 4)

typedef struct {
  GpuBuffer buf;
  OffsetAllocator oa;
} VxMeshPool;

typedef struct {
  u32 pool;
  u32 token;
  u32 offset;
  u64 frame;
} VxMeshRetire;

typedef struct {
  const u32 *data; // quads * VX_QUAD_U32, or NULL for an empty section
  u32 quads;
  const u16 *buckets; // VX_BUCKETS counts, NULL for an empty section
  b32 changed;
} VxSectionUpdate;

typedef struct {
  Allocator alloc;
  VxMeshPool pools[VX_MAX_MESH_POOLS];
  u32 pool_count;
  GpuBuffer upload;
  u8 *upload_ptr;
  u32 upload_segment;
  u32 upload_segments;
  u32 upload_used;
  u32 upload_base;
  VxMeshRetire *retire;
  u32 retire_count, retire_cap;
  u64 quads_used;
  u64 frame;
} VxMeshStore;

void vx_mesh_store_init(VxMeshStore *ms, Allocator *alloc);
void vx_mesh_store_begin_frame(VxMeshStore *ms);
u32 vx_mesh_store_upload_space(const VxMeshStore *ms);
// false when this frame's upload space cannot hold the changed sections
b32 vx_mesh_store_rebuild(VxMeshStore *ms, VxMeshPart *part,
                          const VxSectionUpdate *sections);
void vx_mesh_store_free(VxMeshStore *ms, VxMeshPart *part);

// ---- world: chunk storage, streaming and lane jobs ----

typedef enum {
  VX_JOB_GEN,
  VX_JOB_LIGHT,
  VX_JOB_MESH,
  VX_JOB_FAR_GEN,  // slot: far tile
  VX_JOB_FAR_MESH, // slot: far region
  VX_JOB_KIND_COUNT,
} VxJobKind;

typedef struct {
  u8 kind;
  u8 urgent;
  u8 done;
  u8 sy;
  u16 slot;
  u16 lane;
  u32 uid;
  i32 version;
  u32 out_offset; // u32 offset into the lane's result buffer
  u32 opaque_quads;
  u32 trans_quads;
  u16 buckets[2][VX_BUCKETS];
  f32 ms; // run time, measured by the lane that ran it
} VxJob;

#define VX_LANE_RESULT_U32 (1u << 20)

typedef struct {
  ArenaAllocator *arena;
  Allocator alloc;
  VxGenScratch *gen;
  VxMeshScratch *mesh;
  VxLightEngine light;
  u8 *far_blocks; // VX_CHUNK_VOLUME: a far tile's chunk, generated surface only
  u32 *results;
  u32 results_used;
  u32 phase_jobs; // jobs this lane started in the current job phase
} VxLane;

typedef struct {
  i32 cx, cz;
  f32 d;
} VxWanted;

// ---- far terrain: the whole island as 2x2-block columns of solid runs, drawn where no chunk mesh is ----

#define VX_FAR_CHUNKS (VX_WORLD_HALF * 2 / VX_CHUNK)
#define VX_FAR_C0 (-VX_WORLD_HALF / VX_CHUNK)
#define VX_FAR_TILES (VX_FAR_CHUNKS * VX_FAR_CHUNKS)
#define VX_FAR_CELLS 64 // 8x8 cells of 2x2 blocks per chunk
#define VX_FAR_RUNS 3
#define VX_FAR_REGION 8 // chunks per region side: one mesh and one draw
#define VX_FAR_REGIONS (VX_FAR_CHUNKS / VX_FAR_REGION)
#define VX_FAR_REGION_COUNT (VX_FAR_REGIONS * VX_FAR_REGIONS)
// the near mask: chunks a chunk mesh covers, 64x64 bits round the player after a two-word origin
#define VX_FAR_MASK_SIDE 64
#define VX_FAR_MASK_U32 (2 + VX_FAR_MASK_SIDE * VX_FAR_MASK_SIDE / 32)
// a full water block's surface, as the liquid mesher draws it
#define VX_WATER_TOP (14.0f / 16.0f)

// a solid run of a cell, block rows bot..top; the last kept run reaches the bottom of the world
typedef struct {
  u8 top, bot, top_id, sub_id, bot_id;
} VxFarRun;

typedef struct {
  VxFarRun run[VX_FAR_CELLS][VX_FAR_RUNS];
  u8 count[VX_FAR_CELLS];
} VxFarTile;

// far tile flags: its runs are valid, a job is writing them, its chunk changed since
enum {
  VX_FAR_HAS = 1,
  VX_FAR_QUEUED = 2,
  VX_FAR_DIRTY = 4,
};

typedef struct {
  ArenaAllocator *arena;
  Allocator alloc;
  VxFarTile *tiles;   // VX_FAR_TILES
  u8 *tile_flags;     // VX_FAR_*
  u16 *order;         // tiles nearest the player first
  i32 order_cx, order_cz;
  b32 has_order;
  u32 ready_count;
  VxMeshPart regions[VX_FAR_REGION_COUNT];
  u8 region_dirty[VX_FAR_REGION_COUNT];
  u8 region_queued[VX_FAR_REGION_COUNT];
  u8 region_lo[VX_FAR_REGION_COUNT], region_hi[VX_FAR_REGION_COUNT]; // block rows the region's quads span
  u32 version;        // bumped when any region's mesh changes
} VxFar;

i32 vx_far_tile_of(i32 cx, i32 cz);
// any lane: the runs of one chunk's blocks
void vx_far_reduce(const u8 *blocks, VxFarTile *t);
// any lane: region r's quads into out (cap quads); returns the count, or cap + 1 when it does not fit
u32 vx_far_mesh_region(const VxFar *f, u32 r, u32 *out, u32 cap, u8 *lo, u8 *hi);

typedef struct {
  u32 chunks;
  u32 pending_gen;
  u32 pending_mesh;
  u32 meshed_sections;
  u32 jobs_last_frame;
  f32 light_ms;
  u32 light_writes; // voxel light writes of the last edit
  u32 dirty_sections; // sections the last edit queued for remeshing (its geometry neighbourhood)
} VxWorldStats;

typedef struct {
  ArenaAllocator *arena;
  Allocator alloc;
  VxTerrain terrain;
  VxEditStore *edits;
  VxMeshStore *meshes;

  VxChunk *chunks;
  u16 *grid;
  u16 *free_slots;
  u32 free_count;
  u32 next_uid;
  u32 edit_serial; // bumped on every block edit

  i32 render_distance;
  i32 center_cx, center_cz, center_r;
  b32 has_center;
  VxWanted *wanted;
  u32 wanted_count;

  u32 *claims;
  u32 claim_stamp;

  u16 *urgent;
  u32 urgent_count;

  VxJob *jobs;
  u32 job_count;
  u32 job_cursor;
  u64 job_start;
  f32 job_budget_ms;
  f32 job_round_end_ms; // deadline of the current round, in ms after job_start
  b32 job_near_first;   // latency over throughput: the player's surroundings are missing
  f32 job_est_ms[VX_JOB_KIND_COUNT]; // running average cost per kind, sizes each round
  b32 job_more;                      // main: another round follows; lanes read it after a sync
  u32 job_rounds;                    // rounds run in the last job phase
  VxJob *results;
  u32 result_count;

  VxLane *lanes;
  u32 lane_count;

  u16 *renderables;
  u32 renderable_count;

  // columns whose meshes changed since the renderer last looked (cx, cz pairs); overflow means "everywhere"
  i32 *mesh_changes;
  u32 mesh_change_count;
  b32 mesh_changes_overflow;

  // state pages: allocated and freed on the main lane only, read by mesh jobs
  ArenaAllocator *state_arena;
  Allocator state_alloc;
  u8 **state_pages;
  u32 state_page_count;
  u16 *state_free;
  u32 state_free_count;

  VxFar far_field;

  // the cutaway: blocks past slab cut_z are not drawn, and that slab's cubes show their +z face as the cut; read by mesh jobs
  b32 cut_on;
  i32 cut_z;

  VxWorldStats stats;
} VxWorld;

void vx_world_init(VxWorld *w, u32 seed, VxEditStore *edits, VxMeshStore *meshes);
void vx_world_reset(VxWorld *w, u32 seed);
// main lane, first in the frame: takes in job results the last job phase left and uploads their meshes
void vx_world_integrate(VxWorld *w);
// main lane, before the job phase: stream and schedule; near_first keeps work close to the player
void vx_world_update(VxWorld *w, f64 px, f64 pz, f32 budget_ms, b32 near_first);
// every lane: job rounds until the budget or the work runs out
void vx_world_run_jobs(VxWorld *w);
// main lane, after the frame rendered: the renderer has seen the mesh changes so far
void vx_world_clear_mesh_changes(VxWorld *w);

VxChunk *vx_world_chunk(const VxWorld *w, i32 cx, i32 cz);
u8 vx_world_block(const VxWorld *w, i32 x, i32 y, i32 z);
u16 vx_world_light(const VxWorld *w, i32 x, i32 y, i32 z);
// sets or clears (on = false) the cutaway slab and remeshes the columns that held the old and the new one
void vx_world_set_cut(VxWorld *w, b32 on, i32 z);
b32 vx_world_set_block(VxWorld *w, i32 x, i32 y, i32 z, u8 id, b32 record);
u8 vx_world_state(const VxWorld *w, i32 x, i32 y, i32 z);
// sets a block and its state byte; false when nothing changed or the chunk is not ready
b32 vx_world_set_block_state(VxWorld *w, i32 x, i32 y, i32 z, u8 id, u8 state, b32 record);
b32 vx_world_is_ready(const VxWorld *w, f64 x, f64 z);
f32 vx_world_load_progress(const VxWorld *w, i32 radius);
b32 vx_world_idle(const VxWorld *w);
// main lane: the near mask for the far terrain (VX_FAR_MASK_U32 words) round the player's chunk
void vx_world_near_mask(const VxWorld *w, i32 pcx, i32 pcz, u32 *mask);

#endif
