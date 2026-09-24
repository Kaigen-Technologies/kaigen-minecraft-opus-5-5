#ifndef H_WC
#define H_WC

#include "lib/typedefs.h"
#include "lib/hzmath.h"
#include "lib/memory.h"
#include "lib/thread_context.h"
#include "lib/allocator_arena.h"
#include "lib/offset_allocator.h"
#include "os/os.h"
#include "gpu/gpu.h"

#define WC_CHUNK 16
#define WC_HEIGHT 256
#define WC_SECTIONS 16
#define WC_SEA_LEVEL 62
#define WC_CHUNK_VOLUME (WC_CHUNK * WC_CHUNK * WC_HEIGHT)
#define WC_SECTION_VOLUME (WC_CHUNK * WC_CHUNK * WC_CHUNK)
// loaded chunks always fit in this toroidal grid: unload radius 28.5 < 32
#define WC_GRID 64
#define WC_GRID_MASK (WC_GRID - 1)
#define WC_MAX_CHUNKS (WC_GRID * WC_GRID)
#define WC_MAX_RENDER_DISTANCE 24
#define WC_NO_SLOT 0xFFFFu

#define WC_IDX(x, y, z) (((y) << 8) | ((z) << 4) | (x))

// double-precision world position (player and camera)
typedef struct {
  f64 x, y, z;
} WcV3d;

typedef enum {
  WC_SHAPE_AIR,
  WC_SHAPE_CUBE,
  WC_SHAPE_CROSS,
  WC_SHAPE_LIQUID,
  WC_SHAPE_TORCH,
} WcShape;

typedef enum {
  WC_LAYER_NONE,
  WC_LAYER_OPAQUE,
  WC_LAYER_CUTOUT,
  WC_LAYER_TRANSLUCENT,
} WcLayer;

typedef enum {
  WC_TINT_NONE,
  WC_TINT_GRASS,
  WC_TINT_FOLIAGE,
  WC_TINT_WATER,
  WC_TINT_BIRCH,
  WC_TINT_SPRUCE,
} WcTint;

typedef enum {
  WC_WAVE_NONE,
  WC_WAVE_LEAVES,
  WC_WAVE_PLANT,
} WcWave;

// face order everywhere: +X -X +Y -Y +Z -Z, then the plant pseudo-face
enum {
  WC_FACE_PX,
  WC_FACE_NX,
  WC_FACE_PY,
  WC_FACE_NY,
  WC_FACE_PZ,
  WC_FACE_NZ,
  WC_FACE_PLANT,
};

// texture array layers, in first-use order of the block table
#define WC_TEXTURE_LIST(X)                                                     \
  X(STONE, "stone")                                                            \
  X(GRASS_SIDE, "grass_side")                                                  \
  X(GRASS_TOP, "grass_top")                                                    \
  X(DIRT, "dirt")                                                              \
  X(COBBLESTONE, "cobblestone")                                                \
  X(OAK_PLANKS, "oak_planks")                                                  \
  X(BEDROCK, "bedrock")                                                        \
  X(SAND, "sand")                                                              \
  X(GRAVEL, "gravel")                                                          \
  X(OAK_LOG, "oak_log")                                                        \
  X(OAK_LOG_TOP, "oak_log_top")                                                \
  X(OAK_LEAVES, "oak_leaves")                                                  \
  X(GLASS, "glass")                                                            \
  X(WATER, "water")                                                            \
  X(COAL_ORE, "coal_ore")                                                      \
  X(IRON_ORE, "iron_ore")                                                      \
  X(GOLD_ORE, "gold_ore")                                                      \
  X(DIAMOND_ORE, "diamond_ore")                                                \
  X(BIRCH_LOG, "birch_log")                                                    \
  X(BIRCH_LOG_TOP, "birch_log_top")                                            \
  X(BIRCH_LEAVES, "birch_leaves")                                              \
  X(SPRUCE_LOG, "spruce_log")                                                  \
  X(SPRUCE_LOG_TOP, "spruce_log_top")                                          \
  X(SPRUCE_LEAVES, "spruce_leaves")                                            \
  X(GRASS_SIDE_SNOWED, "grass_side_snowed")                                    \
  X(SNOW, "snow")                                                              \
  X(ICE, "ice")                                                                \
  X(SANDSTONE_SIDE, "sandstone_side")                                          \
  X(SANDSTONE_TOP, "sandstone_top")                                            \
  X(SANDSTONE_BOTTOM, "sandstone_bottom")                                      \
  X(CACTUS_SIDE, "cactus_side")                                                \
  X(CACTUS_TOP, "cactus_top")                                                  \
  X(CACTUS_BOTTOM, "cactus_bottom")                                            \
  X(TALL_GRASS, "tall_grass")                                                  \
  X(DANDELION, "dandelion")                                                    \
  X(POPPY, "poppy")                                                            \
  X(CORNFLOWER, "cornflower")                                                  \
  X(DAISY, "daisy")                                                            \
  X(DEAD_BUSH, "dead_bush")                                                    \
  X(SUGAR_CANE, "sugar_cane")                                                  \
  X(FERN, "fern")                                                              \
  X(RED_MUSHROOM, "red_mushroom")                                              \
  X(BROWN_MUSHROOM, "brown_mushroom")                                          \
  X(BRICKS, "bricks")                                                          \
  X(STONE_BRICKS, "stone_bricks")                                              \
  X(MOSSY_COBBLESTONE, "mossy_cobblestone")                                    \
  X(OBSIDIAN, "obsidian")                                                      \
  X(BOOKSHELF, "bookshelf")                                                    \
  X(GLOWSTONE, "glowstone")                                                    \
  X(TORCH, "torch")                                                            \
  X(LAVA, "lava")                                                              \
  X(CLAY, "clay")                                                              \
  X(GOLD_BLOCK, "gold_block")                                                  \
  X(IRON_BLOCK, "iron_block")                                                  \
  X(DIAMOND_BLOCK, "diamond_block")                                            \
  X(EMERALD_BLOCK, "emerald_block")                                            \
  X(BIRCH_PLANKS, "birch_planks")                                              \
  X(SPRUCE_PLANKS, "spruce_planks")                                            \
  X(GRANITE, "granite")                                                        \
  X(DIORITE, "diorite")                                                        \
  X(ANDESITE, "andesite")                                                      \
  X(WHITE_WOOL, "white_wool")                                                  \
  X(RED_WOOL, "red_wool")                                                      \
  X(ORANGE_WOOL, "orange_wool")                                                \
  X(YELLOW_WOOL, "yellow_wool")                                                \
  X(LIME_WOOL, "lime_wool")                                                    \
  X(BLUE_WOOL, "blue_wool")                                                    \
  X(BLACK_WOOL, "black_wool")                                                  \
  X(QUARTZ_BLOCK_SIDE, "quartz_block_side")                                    \
  X(QUARTZ_BLOCK_TOP, "quartz_block_top")                                      \
  X(SEA_LANTERN, "sea_lantern")                                                \
  X(LAPIS_ORE, "lapis_ore")                                                    \
  X(REDSTONE_ORE, "redstone_ore")                                              \
  X(EMERALD_ORE, "emerald_ore")                                                \
  X(PODZOL_SIDE, "podzol_side")                                                \
  X(PODZOL_TOP, "podzol_top")                                                  \
  X(MOSS_BLOCK, "moss_block")                                                  \
  X(PUMPKIN_SIDE, "pumpkin_side")                                              \
  X(PUMPKIN_TOP, "pumpkin_top")                                                \
  X(JACK_O_LANTERN, "jack_o_lantern")                                          \
  X(TERRACOTTA, "terracotta")                                                  \
  X(RED_SAND, "red_sand")                                                      \
  X(PACKED_ICE, "packed_ice")                                                  \
  X(COARSE_DIRT, "coarse_dirt")                                                \
  X(DESTROY, "destroy")

typedef enum {
#define WC_X(id, name) T_##id,
  WC_TEXTURE_LIST(WC_X)
#undef WC_X
  T_COUNT
} WcTexture;

// id label shape layer solid occludes opacity emission tint wave replaceable inventory side top bottom front
#define WC_BLOCK_LIST(X)                                                                                                                          \
  X(AIR, "Air", WC_SHAPE_AIR, WC_LAYER_NONE, 0, 0, 0, 0, WC_TINT_NONE, WC_WAVE_NONE, 1, 0, T_STONE, T_STONE, T_STONE, T_STONE)                         \
  X(STONE, "Stone", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_STONE, T_STONE, T_STONE, T_STONE)                 \
  X(GRASS, "Grass Block", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_GRASS, WC_WAVE_NONE, 0, 1, T_GRASS_SIDE, T_GRASS_TOP, T_DIRT, T_GRASS_SIDE) \
  X(DIRT, "Dirt", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_DIRT, T_DIRT, T_DIRT, T_DIRT)                       \
  X(COBBLESTONE, "Cobblestone", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_COBBLESTONE, T_COBBLESTONE, T_COBBLESTONE, T_COBBLESTONE) \
  X(OAK_PLANKS, "Oak Planks", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_OAK_PLANKS, T_OAK_PLANKS, T_OAK_PLANKS, T_OAK_PLANKS) \
  X(BEDROCK, "Bedrock", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_BEDROCK, T_BEDROCK, T_BEDROCK, T_BEDROCK)    \
  X(SAND, "Sand", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_SAND, T_SAND, T_SAND, T_SAND)                       \
  X(GRAVEL, "Gravel", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_GRAVEL, T_GRAVEL, T_GRAVEL, T_GRAVEL)           \
  X(OAK_LOG, "Oak Log", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_OAK_LOG, T_OAK_LOG_TOP, T_OAK_LOG_TOP, T_OAK_LOG) \
  X(OAK_LEAVES, "Oak Leaves", WC_SHAPE_CUBE, WC_LAYER_CUTOUT, 1, 0, 1, 0, WC_TINT_FOLIAGE, WC_WAVE_LEAVES, 0, 1, T_OAK_LEAVES, T_OAK_LEAVES, T_OAK_LEAVES, T_OAK_LEAVES) \
  X(GLASS, "Glass", WC_SHAPE_CUBE, WC_LAYER_CUTOUT, 1, 0, 0, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_GLASS, T_GLASS, T_GLASS, T_GLASS)                  \
  X(WATER, "Water", WC_SHAPE_LIQUID, WC_LAYER_TRANSLUCENT, 0, 0, 1, 0, WC_TINT_WATER, WC_WAVE_NONE, 1, 1, T_WATER, T_WATER, T_WATER, T_WATER)          \
  X(COAL_ORE, "Coal Ore", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_COAL_ORE, T_COAL_ORE, T_COAL_ORE, T_COAL_ORE) \
  X(IRON_ORE, "Iron Ore", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_IRON_ORE, T_IRON_ORE, T_IRON_ORE, T_IRON_ORE) \
  X(GOLD_ORE, "Gold Ore", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_GOLD_ORE, T_GOLD_ORE, T_GOLD_ORE, T_GOLD_ORE) \
  X(DIAMOND_ORE, "Diamond Ore", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_DIAMOND_ORE, T_DIAMOND_ORE, T_DIAMOND_ORE, T_DIAMOND_ORE) \
  X(BIRCH_LOG, "Birch Log", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_BIRCH_LOG, T_BIRCH_LOG_TOP, T_BIRCH_LOG_TOP, T_BIRCH_LOG) \
  X(BIRCH_LEAVES, "Birch Leaves", WC_SHAPE_CUBE, WC_LAYER_CUTOUT, 1, 0, 1, 0, WC_TINT_BIRCH, WC_WAVE_LEAVES, 0, 1, T_BIRCH_LEAVES, T_BIRCH_LEAVES, T_BIRCH_LEAVES, T_BIRCH_LEAVES) \
  X(SPRUCE_LOG, "Spruce Log", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_SPRUCE_LOG, T_SPRUCE_LOG_TOP, T_SPRUCE_LOG_TOP, T_SPRUCE_LOG) \
  X(SPRUCE_LEAVES, "Spruce Leaves", WC_SHAPE_CUBE, WC_LAYER_CUTOUT, 1, 0, 1, 0, WC_TINT_SPRUCE, WC_WAVE_LEAVES, 0, 1, T_SPRUCE_LEAVES, T_SPRUCE_LEAVES, T_SPRUCE_LEAVES, T_SPRUCE_LEAVES) \
  X(SNOWY_GRASS, "Snowy Grass", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_GRASS_SIDE_SNOWED, T_SNOW, T_DIRT, T_GRASS_SIDE_SNOWED) \
  X(SNOW, "Snow", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_SNOW, T_SNOW, T_SNOW, T_SNOW)                       \
  X(ICE, "Ice", WC_SHAPE_CUBE, WC_LAYER_TRANSLUCENT, 1, 0, 1, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_ICE, T_ICE, T_ICE, T_ICE)                          \
  X(SANDSTONE, "Sandstone", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_SANDSTONE_SIDE, T_SANDSTONE_TOP, T_SANDSTONE_BOTTOM, T_SANDSTONE_SIDE) \
  X(CACTUS, "Cactus", WC_SHAPE_CUBE, WC_LAYER_CUTOUT, 1, 0, 0, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_CACTUS_SIDE, T_CACTUS_TOP, T_CACTUS_BOTTOM, T_CACTUS_SIDE) \
  X(TALL_GRASS, "Tall Grass", WC_SHAPE_CROSS, WC_LAYER_CUTOUT, 0, 0, 0, 0, WC_TINT_GRASS, WC_WAVE_PLANT, 1, 1, T_TALL_GRASS, T_TALL_GRASS, T_TALL_GRASS, T_TALL_GRASS) \
  X(DANDELION, "Dandelion", WC_SHAPE_CROSS, WC_LAYER_CUTOUT, 0, 0, 0, 0, WC_TINT_NONE, WC_WAVE_PLANT, 1, 1, T_DANDELION, T_DANDELION, T_DANDELION, T_DANDELION) \
  X(POPPY, "Poppy", WC_SHAPE_CROSS, WC_LAYER_CUTOUT, 0, 0, 0, 0, WC_TINT_NONE, WC_WAVE_PLANT, 1, 1, T_POPPY, T_POPPY, T_POPPY, T_POPPY)                \
  X(CORNFLOWER, "Cornflower", WC_SHAPE_CROSS, WC_LAYER_CUTOUT, 0, 0, 0, 0, WC_TINT_NONE, WC_WAVE_PLANT, 1, 1, T_CORNFLOWER, T_CORNFLOWER, T_CORNFLOWER, T_CORNFLOWER) \
  X(DAISY, "Oxeye Daisy", WC_SHAPE_CROSS, WC_LAYER_CUTOUT, 0, 0, 0, 0, WC_TINT_NONE, WC_WAVE_PLANT, 1, 1, T_DAISY, T_DAISY, T_DAISY, T_DAISY)          \
  X(DEAD_BUSH, "Dead Bush", WC_SHAPE_CROSS, WC_LAYER_CUTOUT, 0, 0, 0, 0, WC_TINT_NONE, WC_WAVE_PLANT, 1, 1, T_DEAD_BUSH, T_DEAD_BUSH, T_DEAD_BUSH, T_DEAD_BUSH) \
  X(SUGAR_CANE, "Sugar Cane", WC_SHAPE_CROSS, WC_LAYER_CUTOUT, 0, 0, 0, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_SUGAR_CANE, T_SUGAR_CANE, T_SUGAR_CANE, T_SUGAR_CANE) \
  X(FERN, "Fern", WC_SHAPE_CROSS, WC_LAYER_CUTOUT, 0, 0, 0, 0, WC_TINT_GRASS, WC_WAVE_PLANT, 1, 1, T_FERN, T_FERN, T_FERN, T_FERN)                   \
  X(RED_MUSHROOM, "Red Mushroom", WC_SHAPE_CROSS, WC_LAYER_CUTOUT, 0, 0, 0, 0, WC_TINT_NONE, WC_WAVE_NONE, 1, 1, T_RED_MUSHROOM, T_RED_MUSHROOM, T_RED_MUSHROOM, T_RED_MUSHROOM) \
  X(BROWN_MUSHROOM, "Brown Mushroom", WC_SHAPE_CROSS, WC_LAYER_CUTOUT, 0, 0, 0, 1, WC_TINT_NONE, WC_WAVE_NONE, 1, 1, T_BROWN_MUSHROOM, T_BROWN_MUSHROOM, T_BROWN_MUSHROOM, T_BROWN_MUSHROOM) \
  X(BRICKS, "Bricks", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_BRICKS, T_BRICKS, T_BRICKS, T_BRICKS)          \
  X(STONE_BRICKS, "Stone Bricks", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_STONE_BRICKS, T_STONE_BRICKS, T_STONE_BRICKS, T_STONE_BRICKS) \
  X(MOSSY_COBBLESTONE, "Mossy Cobblestone", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_MOSSY_COBBLESTONE, T_MOSSY_COBBLESTONE, T_MOSSY_COBBLESTONE, T_MOSSY_COBBLESTONE) \
  X(OBSIDIAN, "Obsidian", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_OBSIDIAN, T_OBSIDIAN, T_OBSIDIAN, T_OBSIDIAN) \
  X(BOOKSHELF, "Bookshelf", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_BOOKSHELF, T_OAK_PLANKS, T_OAK_PLANKS, T_BOOKSHELF) \
  X(GLOWSTONE, "Glowstone", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 15, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_GLOWSTONE, T_GLOWSTONE, T_GLOWSTONE, T_GLOWSTONE) \
  X(TORCH, "Torch", WC_SHAPE_TORCH, WC_LAYER_CUTOUT, 0, 0, 0, 14, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_TORCH, T_TORCH, T_TORCH, T_TORCH)               \
  X(LAVA, "Lava", WC_SHAPE_LIQUID, WC_LAYER_OPAQUE, 0, 0, 15, 15, WC_TINT_NONE, WC_WAVE_NONE, 1, 1, T_LAVA, T_LAVA, T_LAVA, T_LAVA)                   \
  X(CLAY, "Clay", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_CLAY, T_CLAY, T_CLAY, T_CLAY)                       \
  X(GOLD_BLOCK, "Block of Gold", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_GOLD_BLOCK, T_GOLD_BLOCK, T_GOLD_BLOCK, T_GOLD_BLOCK) \
  X(IRON_BLOCK, "Block of Iron", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_IRON_BLOCK, T_IRON_BLOCK, T_IRON_BLOCK, T_IRON_BLOCK) \
  X(DIAMOND_BLOCK, "Block of Diamond", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_DIAMOND_BLOCK, T_DIAMOND_BLOCK, T_DIAMOND_BLOCK, T_DIAMOND_BLOCK) \
  X(EMERALD_BLOCK, "Block of Emerald", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_EMERALD_BLOCK, T_EMERALD_BLOCK, T_EMERALD_BLOCK, T_EMERALD_BLOCK) \
  X(BIRCH_PLANKS, "Birch Planks", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_BIRCH_PLANKS, T_BIRCH_PLANKS, T_BIRCH_PLANKS, T_BIRCH_PLANKS) \
  X(SPRUCE_PLANKS, "Spruce Planks", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_SPRUCE_PLANKS, T_SPRUCE_PLANKS, T_SPRUCE_PLANKS, T_SPRUCE_PLANKS) \
  X(GRANITE, "Granite", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_GRANITE, T_GRANITE, T_GRANITE, T_GRANITE)    \
  X(DIORITE, "Diorite", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_DIORITE, T_DIORITE, T_DIORITE, T_DIORITE)    \
  X(ANDESITE, "Andesite", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_ANDESITE, T_ANDESITE, T_ANDESITE, T_ANDESITE) \
  X(WHITE_WOOL, "White Wool", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_WHITE_WOOL, T_WHITE_WOOL, T_WHITE_WOOL, T_WHITE_WOOL) \
  X(RED_WOOL, "Red Wool", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_RED_WOOL, T_RED_WOOL, T_RED_WOOL, T_RED_WOOL) \
  X(ORANGE_WOOL, "Orange Wool", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_ORANGE_WOOL, T_ORANGE_WOOL, T_ORANGE_WOOL, T_ORANGE_WOOL) \
  X(YELLOW_WOOL, "Yellow Wool", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_YELLOW_WOOL, T_YELLOW_WOOL, T_YELLOW_WOOL, T_YELLOW_WOOL) \
  X(LIME_WOOL, "Lime Wool", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_LIME_WOOL, T_LIME_WOOL, T_LIME_WOOL, T_LIME_WOOL) \
  X(BLUE_WOOL, "Blue Wool", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_BLUE_WOOL, T_BLUE_WOOL, T_BLUE_WOOL, T_BLUE_WOOL) \
  X(BLACK_WOOL, "Black Wool", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_BLACK_WOOL, T_BLACK_WOOL, T_BLACK_WOOL, T_BLACK_WOOL) \
  X(QUARTZ_BLOCK, "Block of Quartz", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_QUARTZ_BLOCK_SIDE, T_QUARTZ_BLOCK_TOP, T_QUARTZ_BLOCK_TOP, T_QUARTZ_BLOCK_SIDE) \
  X(SEA_LANTERN, "Sea Lantern", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 15, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_SEA_LANTERN, T_SEA_LANTERN, T_SEA_LANTERN, T_SEA_LANTERN) \
  X(LAPIS_ORE, "Lapis Lazuli Ore", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_LAPIS_ORE, T_LAPIS_ORE, T_LAPIS_ORE, T_LAPIS_ORE) \
  X(REDSTONE_ORE, "Redstone Ore", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_REDSTONE_ORE, T_REDSTONE_ORE, T_REDSTONE_ORE, T_REDSTONE_ORE) \
  X(EMERALD_ORE, "Emerald Ore", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_EMERALD_ORE, T_EMERALD_ORE, T_EMERALD_ORE, T_EMERALD_ORE) \
  X(PODZOL, "Podzol", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_PODZOL_SIDE, T_PODZOL_TOP, T_DIRT, T_PODZOL_SIDE) \
  X(MOSS_BLOCK, "Moss Block", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_MOSS_BLOCK, T_MOSS_BLOCK, T_MOSS_BLOCK, T_MOSS_BLOCK) \
  X(PUMPKIN, "Pumpkin", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_PUMPKIN_SIDE, T_PUMPKIN_TOP, T_PUMPKIN_TOP, T_PUMPKIN_SIDE) \
  X(JACK_O_LANTERN, "Jack o'Lantern", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 15, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_PUMPKIN_SIDE, T_PUMPKIN_TOP, T_PUMPKIN_TOP, T_JACK_O_LANTERN) \
  X(TERRACOTTA, "Terracotta", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_TERRACOTTA, T_TERRACOTTA, T_TERRACOTTA, T_TERRACOTTA) \
  X(RED_SAND, "Red Sand", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_RED_SAND, T_RED_SAND, T_RED_SAND, T_RED_SAND) \
  X(PACKED_ICE, "Packed Ice", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_PACKED_ICE, T_PACKED_ICE, T_PACKED_ICE, T_PACKED_ICE) \
  X(COARSE_DIRT, "Coarse Dirt", WC_SHAPE_CUBE, WC_LAYER_OPAQUE, 1, 1, 15, 0, WC_TINT_NONE, WC_WAVE_NONE, 0, 1, T_COARSE_DIRT, T_COARSE_DIRT, T_COARSE_DIRT, T_COARSE_DIRT)

typedef enum {
#define WC_X(id, label, sh, ly, so, oc, op, em, ti, wa, re, in, ts, tt, tb, tf) B_##id,
  WC_BLOCK_LIST(WC_X)
#undef WC_X
  B_COUNT
} WcBlock;

// lookup tables over every u8 id: ids past B_COUNT behave like stone
#define WC_BLOCK_IDS 256
#define WC_HOTBAR_SLOTS 9
extern const u8 wc_shape[WC_BLOCK_IDS];
extern const u8 wc_layer[WC_BLOCK_IDS];
extern const u8 wc_solid[WC_BLOCK_IDS];
extern const u8 wc_occludes[WC_BLOCK_IDS];
extern const u8 wc_light_opacity[WC_BLOCK_IDS];
extern const u8 wc_emission[WC_BLOCK_IDS];
extern const u8 wc_tint[WC_BLOCK_IDS];
extern const u8 wc_wave[WC_BLOCK_IDS];
extern const u8 wc_replaceable[WC_BLOCK_IDS];
extern const u8 wc_face_tex[WC_BLOCK_IDS][6];
extern const char *const wc_block_label[B_COUNT];
extern const u8 wc_block_in_inventory[B_COUNT];
extern const char *const wc_texture_name[T_COUNT];

#define WC_INVENTORY_COUNT 72
extern const u8 wc_inventory_order[WC_INVENTORY_COUNT];
extern const u8 wc_default_hotbar[WC_HOTBAR_SLOTS];

// ---- hashing / rng (32-bit wrapping multiplies) ----

force_inline u32 wc_hash2i(i32 x, i32 z, u32 seed) {
  u32 h = seed ^ ((u32)x * 0x27d4eb2du) ^ ((u32)z * 0x165667b1u);
  h = (h ^ (h >> 15)) * 0x2c1b3c6du;
  h = (h ^ (h >> 12)) * 0x297a2d39u;
  h ^= h >> 15;
  return h;
}

force_inline u32 wc_hash3i(i32 x, i32 y, i32 z, u32 seed) {
  u32 h = seed ^ ((u32)x * 0x27d4eb2du) ^ ((u32)y * 0x9e3779b1u) ^
          ((u32)z * 0x165667b1u);
  h = (h ^ (h >> 16)) * 0x85ebca6bu;
  h = (h ^ (h >> 13)) * 0xc2b2ae35u;
  h ^= h >> 16;
  return h;
}

force_inline f64 wc_hash2(i32 x, i32 z, u32 seed) {
  return (f64)wc_hash2i(x, z, seed) / 4294967296.0;
}

typedef struct {
  u32 a;
} WcRng;

force_inline WcRng wc_rng(u32 seed) { return (WcRng){seed}; }

// mulberry32, [0, 1)
force_inline f64 wc_rng_next(WcRng *r) {
  r->a += 0x6d2b79f5u;
  u32 t = r->a;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return (f64)(t ^ (t >> 14)) / 4294967296.0;
}

force_inline f32 wc_rng_nextf(WcRng *r) { return (f32)wc_rng_next(r); }

u32 wc_hash_string(const char *s);

force_inline i32 wc_floor_i(f64 x) {
  i32 i = (i32)x;
  return (x < (f64)i) ? i - 1 : i;
}

force_inline f64 wc_clamp(f64 x, f64 a, f64 b) { return x < a ? a : x > b ? b : x; }
force_inline f64 wc_lerp(f64 a, f64 b, f64 t) { return a + (b - a) * t; }
force_inline f64 wc_smoothstep(f64 e0, f64 e1, f64 x) {
  f64 t = wc_clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

// ---- noise ----

typedef struct {
  u8 *perm;       // 512
  u8 *perm_mod12; // 512
} WcSimplex;

void wc_simplex_init(WcSimplex *s, u32 seed, Allocator *alloc);
f64 wc_noise2(const WcSimplex *s, f64 x, f64 y);
f64 wc_noise3(const WcSimplex *s, f64 x, f64 y, f64 z);
f64 wc_fbm2(const WcSimplex *s, f64 x, f64 y, u32 octaves);
f64 wc_ridged2(const WcSimplex *s, f64 x, f64 y, u32 octaves);

// ---- terrain generation ----

typedef enum {
  WC_BIOME_OCEAN,
  WC_BIOME_DEEP_OCEAN,
  WC_BIOME_FROZEN_OCEAN,
  WC_BIOME_BEACH,
  WC_BIOME_SNOWY_BEACH,
  WC_BIOME_STONY_SHORE,
  WC_BIOME_PLAINS,
  WC_BIOME_FOREST,
  WC_BIOME_BIRCH_FOREST,
  WC_BIOME_TAIGA,
  WC_BIOME_SNOWY_PLAINS,
  WC_BIOME_SNOWY_TAIGA,
  WC_BIOME_DESERT,
  WC_BIOME_BADLANDS,
  WC_BIOME_MOUNTAINS,
  WC_BIOME_SNOWY_PEAKS,
  WC_BIOME_RIVER,
  WC_BIOME_FROZEN_RIVER,
  WC_BIOME_SWAMP,
  WC_BIOME_MEADOW,
  WC_BIOME_COUNT,
} WcBiome;

extern const char *const wc_biome_name[WC_BIOME_COUNT];

typedef struct {
  i32 height;
  f64 temp;
  f64 humid;
  u8 biome;
  f64 river;
  f64 mountain;
} WcColumnSample;

typedef struct {
  u32 seed;
  WcSimplex warp_x, warp_z, cont, ero, peaks, hills, detail, river, temp, humid,
      variant, cave_a, cave_b, cave_c, cave_mask, patch;
} WcTerrain;

#define WC_GEN_MARGIN 4
#define WC_GEN_SPAN (WC_CHUNK + WC_GEN_MARGIN * 2)
#define WC_CAVE_GX 5
#define WC_CAVE_GY_MAX 65

// per-lane generation scratch
typedef struct {
  i32 h[WC_GEN_SPAN * WC_GEN_SPAN];
  u8 biome[WC_GEN_SPAN * WC_GEN_SPAN];
  f32 temp[WC_GEN_SPAN * WC_GEN_SPAN];
  f32 humid[WC_GEN_SPAN * WC_GEN_SPAN];
  f32 mtn[WC_GEN_SPAN * WC_GEN_SPAN];
  f32 tun[WC_CAVE_GX * WC_CAVE_GX * WC_CAVE_GY_MAX];
  f32 cav[WC_CAVE_GX * WC_CAVE_GX * WC_CAVE_GY_MAX];
} WcGenScratch;

void wc_terrain_init(WcTerrain *t, u32 seed, Allocator *alloc);
WcColumnSample wc_sample_column(const WcTerrain *t, i32 x, i32 z);
// blocks: WC_CHUNK_VOLUME ids, climate: 16x16 (temp, humid) byte pairs
void wc_generate_chunk(const WcTerrain *t, WcGenScratch *s, i32 cx, i32 cz,
                       u8 *blocks, u8 *climate);
v3 wc_find_spawn(const WcTerrain *t);

// ---- chunks ----

typedef enum {
  WC_CHUNK_GENERATING,
  WC_CHUNK_GENERATED,
  WC_CHUNK_LIT,
} WcChunkState;

#define WC_NO_POOL 0xFFFFFFFFu

// one draw's worth of column geometry: sections packed bottom to top
typedef struct {
  u32 pool;
  u32 alloc_token;
  u32 base;
  u32 start[WC_SECTIONS + 1];
  u16 *meta; // WC_SECTIONS * WC_SECTION_META
} WcMeshPart;

typedef struct {
  i32 cx, cz;
  u32 uid;
  u8 state;
  u8 in_use;
  u8 mesh_started;
  u8 modified;
  u8 *blocks;
  u8 *light; // sky << 4 | block
  u8 climate[WC_CHUNK * WC_CHUNK * 2];
  u8 h15[WC_CHUNK * WC_CHUNK];
  i32 section_count[WC_SECTIONS];
  i32 sec_version[WC_SECTIONS];
  i32 sec_meshed[WC_SECTIONS];
  i32 sec_pending[WC_SECTIONS];
  // render side
  WcMeshPart parts[2];
  i32 surface_min;
  u32 render_index;
  u16 urgent_mask;
} WcChunk;

// ---- lighting ----

typedef struct {
  WcChunk *win[9];
  Allocator *alloc;
  u32 *q;
  u32 qcap, qh, qt;
  u32 *rq;
  u32 rcap, rh, rt;
  u32 writes;
} WcLightEngine;

void wc_light_init(WcLightEngine *e, Allocator *alloc);
void wc_light_chunk(WcLightEngine *e, WcChunk *center);
void wc_light_block_changed(WcLightEngine *e, i32 wx, i32 y, i32 wz, u8 new_id);
void wc_chunk_column_fill(WcChunk *c);
void wc_chunk_recount_sections(WcChunk *c);

// ---- meshing ----

#define WC_PAD 18
#define WC_PAD2 (WC_PAD * WC_PAD)
#define WC_PAD3 (WC_PAD2 * WC_PAD)
#define WC_MAX_SECTION_QUADS (WC_SECTION_VOLUME * 6)
#define WC_QUAD_U32 5
// culling buckets: plain cube faces by direction, flagged (cutout, waving, ice) cube faces by direction, the rest
#define WC_BUCKETS 13
#define WC_BUCKET_FLAGGED 6
#define WC_BUCKET_OTHER 12
// per section: quad offsets of buckets 0..WC_BUCKETS from the section start, then the mask of non-empty buckets
#define WC_SECTION_META 16
#define WC_META_MASK 14

#define WC_VF_CUTOUT 1u
#define WC_VF_WAVE_LEAF 2u
#define WC_VF_WAVE_PLANT 4u
#define WC_VF_WATER 8u
#define WC_VF_LAVA 16u
#define WC_VF_ICE 32u

typedef struct {
  u8 blocks[WC_PAD3];
  u8 light[WC_PAD3];
  u8 climate[WC_PAD2 * 2];
  u32 *col_grass;   // 256
  u32 *col_foliage; // 256
  u32 *col_water;   // 256
  u32 *opaque; // WC_MAX_SECTION_QUADS quads, bucket by bucket
  u32 opaque_quads;
  u32 *trans;
  u32 trans_quads;
  u32 *sort; // WC_MAX_SECTION_QUADS quads
  u16 buckets[2][WC_BUCKETS]; // quads per bucket, opaque then translucent
} WcMeshScratch;

void wc_mesh_scratch_init(WcMeshScratch *m, Allocator *alloc);
// meshes the padded volume in m; ox/oy/oz are the section's world origin
void wc_mesh_section(WcMeshScratch *m, i32 ox, i32 oy, i32 oz);

v3 wc_grass_color(f64 t, f64 h);
v3 wc_foliage_color(f64 t, f64 h);
v3 wc_water_color(f64 t, f64 h);

// ---- player edits (persisted, re-applied when a chunk regenerates) ----

typedef struct {
  u16 index;
  u8 id;
  u8 _pad;
} WcEdit;

typedef struct {
  i32 cx, cz;
  u32 count, cap;
  WcEdit *items;
} WcChunkEdits;

typedef struct {
  Allocator alloc; // tlsf: edit lists grow and move
  WcChunkEdits *chunks;
  u32 chunk_count, chunk_cap;
  u32 *table; // open addressing: chunk key -> index + 1
  u32 table_cap;
} WcEditStore;

void wc_edits_init(WcEditStore *s);
void wc_edits_clear(WcEditStore *s);
void wc_edits_record(WcEditStore *s, i32 cx, i32 cz, u16 index, u8 id);
const WcChunkEdits *wc_edits_for_chunk(const WcEditStore *s, i32 cx, i32 cz);

// ---- GPU mesh storage: column parts live in big storage-buffer pools ----

#define WC_MAX_MESH_POOLS 32
#define WC_POOL_QUADS (1u << 20)
#define WC_QUAD_BYTES (WC_QUAD_U32 * 4)

typedef struct {
  GpuBuffer buf;
  OffsetAllocator oa;
} WcMeshPool;

typedef struct {
  u32 pool;
  u32 token;
  u32 offset;
  u64 frame;
} WcMeshRetire;

typedef struct {
  const u32 *data; // quads * WC_QUAD_U32, or NULL for an empty section
  u32 quads;
  const u16 *buckets; // WC_BUCKETS counts, NULL for an empty section
  b32 changed;
} WcSectionUpdate;

typedef struct {
  Allocator alloc;
  WcMeshPool pools[WC_MAX_MESH_POOLS];
  u32 pool_count;
  GpuBuffer upload;
  u8 *upload_ptr;
  u32 upload_segment;
  u32 upload_segments;
  u32 upload_used;
  u32 upload_base;
  WcMeshRetire *retire;
  u32 retire_count, retire_cap;
  u64 quads_used;
  u64 frame;
} WcMeshStore;

void wc_mesh_store_init(WcMeshStore *ms, Allocator *alloc);
void wc_mesh_store_begin_frame(WcMeshStore *ms);
u32 wc_mesh_store_upload_space(const WcMeshStore *ms);
// false when this frame's upload space cannot hold the changed sections
b32 wc_mesh_store_rebuild(WcMeshStore *ms, WcMeshPart *part,
                          const WcSectionUpdate *sections);
void wc_mesh_store_free(WcMeshStore *ms, WcMeshPart *part);

// ---- world: chunk storage, streaming and lane jobs ----

typedef enum {
  WC_JOB_GEN,
  WC_JOB_LIGHT,
  WC_JOB_MESH,
  WC_JOB_KIND_COUNT,
} WcJobKind;

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
  u16 buckets[2][WC_BUCKETS];
  f32 ms; // run time, measured by the lane that ran it
} WcJob;

#define WC_LANE_RESULT_U32 (1u << 20)

typedef struct {
  ArenaAllocator *arena;
  Allocator alloc;
  WcGenScratch *gen;
  WcMeshScratch *mesh;
  WcLightEngine light;
  u32 *results;
  u32 results_used;
  u32 phase_jobs; // jobs this lane started in the current job phase
} WcLane;

typedef struct {
  i32 cx, cz;
  f32 d;
} WcWanted;

typedef struct {
  u32 chunks;
  u32 pending_gen;
  u32 pending_mesh;
  u32 meshed_sections;
  u32 jobs_last_frame;
  f32 light_ms;
} WcWorldStats;

typedef struct {
  ArenaAllocator *arena;
  Allocator alloc;
  WcTerrain terrain;
  WcEditStore *edits;
  WcMeshStore *meshes;

  WcChunk *chunks;
  u16 *grid;
  u16 *free_slots;
  u32 free_count;
  u32 next_uid;

  i32 render_distance;
  i32 center_cx, center_cz, center_r;
  b32 has_center;
  WcWanted *wanted;
  u32 wanted_count;

  u32 *claims;
  u32 claim_stamp;

  u16 *urgent;
  u32 urgent_count;

  WcJob *jobs;
  u32 job_count;
  u32 job_cursor;
  u64 job_start;
  f32 job_budget_ms;
  f32 job_round_end_ms; // deadline of the current round, in ms after job_start
  b32 job_near_first;   // latency over throughput: the player's surroundings are missing
  f32 job_est_ms[WC_JOB_KIND_COUNT]; // running average cost per kind, sizes each round
  b32 job_more;                      // main: another round follows; lanes read it after a sync
  u32 job_rounds;                    // rounds run in the last job phase
  WcJob *results;
  u32 result_count;

  WcLane *lanes;
  u32 lane_count;

  u16 *renderables;
  u32 renderable_count;

  // columns whose meshes changed since the renderer last looked (cx, cz pairs); overflow means "everywhere"
  i32 *mesh_changes;
  u32 mesh_change_count;
  b32 mesh_changes_overflow;

  WcWorldStats stats;
} WcWorld;

void wc_world_init(WcWorld *w, u32 seed, WcEditStore *edits, WcMeshStore *meshes);
void wc_world_reset(WcWorld *w, u32 seed);
// main lane, first in the frame: takes in job results the last job phase left and uploads their meshes
void wc_world_integrate(WcWorld *w);
// main lane, before the job phase: stream and schedule; near_first keeps work close to the player
void wc_world_update(WcWorld *w, f64 px, f64 pz, f32 budget_ms, b32 near_first);
// every lane: job rounds until the budget or the work runs out
void wc_world_run_jobs(WcWorld *w);
// main lane, after the frame rendered: the renderer has seen the mesh changes so far
void wc_world_clear_mesh_changes(WcWorld *w);

WcChunk *wc_world_chunk(const WcWorld *w, i32 cx, i32 cz);
u8 wc_world_block(const WcWorld *w, i32 x, i32 y, i32 z);
u8 wc_world_light(const WcWorld *w, i32 x, i32 y, i32 z);
b32 wc_world_set_block(WcWorld *w, i32 x, i32 y, i32 z, u8 id, b32 record);
b32 wc_world_is_ready(const WcWorld *w, f64 x, f64 z);
f32 wc_world_load_progress(const WcWorld *w, i32 radius);
b32 wc_world_idle(const WcWorld *w);

#endif
