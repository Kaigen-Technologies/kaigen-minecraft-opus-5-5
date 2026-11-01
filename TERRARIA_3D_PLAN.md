# Voxaria: Terraria 3D demo plan

Working title **Voxaria**. All art is original and procedurally generated.

Branch `terraria-3d` (game) on engine branch `terraria-3d`. Reference frames are in [docs/terraria3d/reference/](docs/terraria3d/reference/).

## 1. Goal

A browser-first 3D voxel demo that feels like Terraria. You dig down through a layered world into real darkness lit by your own torches. You fight cute-but-dangerous monsters, loot pots and chests, build a hut before nightfall, grow your movement (double jump, grappling hook, wings) and beat the Eye of Cthulhu.

It must look better than the reference video. We get there through stylized lighting and one coherent art direction, not through more content. It runs on a custom renderer written directly on `gpu.h`, on Windows D3D12 and on WebGPU (desktop and mobile).

## 2. The reference video

The video is a 65 s third-person flythrough of a 3D voxel Terraria remake. Its content (armor sets, banners, furniture, tile textures) appears to be built from Terraria's own sprites and textures. We cannot use those; all our art is original.

| Frame | Shows | Take-away |
|---|---|---|
| [ref_00.5s_world_overview](docs/terraria3d/reference/ref_00.5s_world_overview.jpg) | Island world in an ocean, voxel clouds, sky islands, mountain band at the horizon | A compact island with every biome in reach; sky islands visible from the ground |
| [ref_03.6s_biomes_hallow_snow](docs/terraria3d/reference/ref_03.6s_biomes_hallow_snow.jpg) | Biome borders | Each biome reads by colour alone |
| [ref_10.5s_sky_island_wings](docs/terraria3d/reference/ref_10.5s_sky_island_wings.jpg) | Floating island with a hut; winged player | Flight is the payoff; sky islands are landmarks |
| [ref_14.0s_town_street](docs/terraria3d/reference/ref_14.0s_town_street.jpg), [ref_59.5s_town_dusk](docs/terraria3d/reference/ref_59.5s_town_dusk.jpg) | NPC town, wall torches, pylons, day vs dusk | Dusk + torches + bloom carry the mood; noon looks flat |
| [ref_17.5s_interior_mannequins](docs/terraria3d/reference/ref_17.5s_interior_mannequins.jpg) | Armor on mannequins, weapon racks, furniture | Voxelized pixel sprites work for items and characters |
| [ref_21.0s_banner_wall](docs/terraria3d/reference/ref_21.0s_banner_wall.jpg), [ref_25.0s_furniture_rows](docs/terraria3d/reference/ref_25.0s_furniture_rows.jpg), [ref_27.0s_pylon_plaza](docs/terraria3d/reference/ref_27.0s_pylon_plaza.jpg) | Content showcases | Breadth from the original game's assets; not our route |
| [ref_30.5s_desert_pillars](docs/terraria3d/reference/ref_30.5s_desert_pillars.jpg), [ref_33.0s_snow_blizzard](docs/terraria3d/reference/ref_33.0s_snow_blizzard.jpg), [ref_36.0s_jungle_temple](docs/terraria3d/reference/ref_36.0s_jungle_temple.jpg) | Biome set pieces, blizzard fog | One landmark per biome |
| [ref_38.5s_shimmer_lake](docs/terraria3d/reference/ref_38.5s_shimmer_lake.jpg) | Iridescent liquid with sparkles | A magical liquid is a showpiece |
| [ref_42.0s_glowing_mushrooms](docs/terraria3d/reference/ref_42.0s_glowing_mushrooms.jpg) | Giant emissive mushrooms in a black cavern | Emissive biomes make the best shots |
| [ref_45.5s_digging_shaft](docs/terraria3d/reference/ref_45.5s_digging_shaft.jpg) | Drilling a shaft straight down, torches on the walls | "Dig to hell" is a core fantasy |
| [ref_49.0s_underworld](docs/terraria3d/reference/ref_49.0s_underworld.jpg) | Ash, lava, glowing hellstone, embers, obsidian ruin | Warm darkness and embers |
| [ref_53.0s_dungeon_combat](docs/terraria3d/reference/ref_53.0s_dungeon_combat.jpg) | Magic projectiles, debris, knockback, skeleton enemies | Combat lives on VFX juice |
| [ref_55.5s_empress_boss_dusk](docs/terraria3d/reference/ref_55.5s_empress_boss_dusk.jpg) | Prismatic boss over a skybridge at dusk, moon, stars | Sky plus boss VFX is the hero shot |

**Strengths:**
- content breadth;
- the emissive biomes;
- the dusk sky with moon and stars;
- magic VFX with bloom;
- winged flight;
- long view distance with haze.

**Where we beat it:**

| Weakness in the reference | Our answer |
|---|---|
| Daylight is flat: faint sun shadows, little AO. Interiors are evenly bright and torches don't pool light (14.0 s, 17.5 s). | Stylized sun and sky lighting with soft, colour-shifted shadows. Terraria-style coloured light, so every torch makes a warm pool and unlit caves are dark. |
| Pixel textures turn noisy and shimmer at distance, with no value hierarchy (0.5 s, 30.5 s). | Tile framing gives every block a readable shape. Palettes are designed to read at distance, plus per-biome grading and texel-AA sampling. |
| Mixed geometry: smooth caves beside cubic terrain (38.5 s, 45.5 s). | One consistent voxel language. |
| Stiff extruded characters. | Voxel puppets with procedural animation, squash and stretch, outlines, rim light and hit flash. |
| Generic haze; caves are just black voids. | Biome fog and grading, cave atmosphere (spores, dust motes, light shafts), lava heat haze. |
| Darkness is only decoration. | Light is a resource: torches, glowsticks, mining helmet, glowing ores and mushrooms. |

### 2.1 Every scenario in the video (the port list)

All 65 seconds are saved as reference frames: `docs/terraria3d/scenes/s_01.jpg` … `s_65.jpg` (one per second; `s_NN` is t = NN - 0.5 s), and the dig fall at 6 fps in `docs/terraria3d/scenes/dig/`. Shot cuts (ffmpeg scene > 0.2): 9.6, 13.1, 16.6, 20.1, 23.6, 26.1, 29.1, 32.1, 34.6, 37.6, 40.6, 43.6, 44.4, 50.6, 54.1, 58.6 s.

**Rule: these frames are the spec.** Every visual step opens the matching frames, captures our version from a matching camera, and puts the two side by side (`out/cmp_<scenario>.png`) before it is marked done. A step that has not been compared against its frames is not done. The demo is not done until every scenario below is ported (M7).

| # | Time | Frames | Scenario | What must be ported |
|---|---|---|---|---|
| S1 | 0–3.5 s | s_01–s_04 | Island overview from the sky | Island in ocean; desert with a giant sandstone tower (stacked cylinders), pillars, stepped pyramid, oasis pits, palms, cacti; sand with contour ridges; sky islands; voxel clouds; mountain silhouette ring |
| S2 | 3.5–5.5 s | s_04–s_06 | Biome-mix flyover | Hallow (cyan grass, pink/yellow trees, white sphere trees), crimson/corruption patches, town roofs, wooden greenhouse frames |
| S3 | 5.5–9.5 s | s_06–s_10 | Forest and jungle canopy flyover | Mahogany-style flat canopies, purple corruption trees, terraced stone cliff, canopy out to the ocean |
| S4 | 9.5–13 s | s_10–s_13 | Sky island and winged flight | Cloud-base island, grass top, small brick house, big tree; player flying with golden feather wings; distant floating islands |
| S5 | 13–17 s | s_14–s_17 | Town street | Third-person player in armor and wings; wood and red-brick houses with stone foundations, glass windows, wall torches, signs ("MARKET"); cobblestone road; NPCs at tables; crystal pedestals |
| S6 | 17–20 s | s_18–s_20 | House interior | Plank ceiling, gray brick walls; four armor sets on mannequins; weapons on wall racks; dwarf NPC; table, chairs, anvil |
| S7 | 20–24 s | s_21–s_24 | Banner wall | Long red-brick building with rows of monster banners, brick pillars with torches, grass and cobble ground, red mushroom |
| S8 | 24–26 s | s_25–s_26 | Furniture museum | Rows of colourful furniture sets on cobble roads in the forest |
| S9 | 26–29 s | s_27–s_29 | Crystal pedestal plaza | Paved avenue of crystal pedestals, sandstone torch pillars, cottages with signs, hallow trees, glass-frame greenhouse |
| S10 | 29–32 s | s_30–s_32 | Desert fortress | The sandstone tower and pillars up close, pyramid stairs, cacti, dune contour lines |
| S11 | 32–35 s | s_33–s_35 | Snow mountain | Ice/crystal mountain peak, boreal pines, ice crevasse, snow terraces |
| S12 | 35–38 s | s_36–s_38 | Jungle temple mountain | Stepped pyramid mountain of mossy stone, jungle trees, pond |
| S13 | 38–41 s | s_39–s_41 | Shimmer lake cave | Pale crystal cave with hanging stalactite blobs, glowing violet shimmer liquid with sparkles, black void |
| S14 | 41–44 s | s_42–s_44 | Glowing mushroom cavern | Giant blue mushrooms with emissive stripes, dark mud floor, small glowing mushrooms, flying player |
| S15 | 44–48 s | s_45–s_48, dig/ | Digging fall | Wide stepped shaft through grass, dirt, stone; camera looks down on the falling player; layer colours; lava glow at the bottom |
| S16 | 48–51 s | s_49–s_51 | Underworld | Ash/hellstone floor with glowing cracks, lava rivers, obsidian ruins with glowing seams, embers |
| S17 | 51–54 s | s_52–s_54 | Dungeon combat | Green dungeon brick corridor, skeleton enemies, weapon VFX trails, debris, gold chest, torches, benches |
| S18 | 54–59 s | s_55–s_59 | Dusk boss fight | Rainbow-winged fairy boss, prismatic projectile fans and crescents, sunset with god rays, moon, wooden frame structure |
| S19 | 59–62 s | s_60–s_62 | Town at dusk, aerial | Whole town, pedestals, lit windows |
| S20 | 62–65 s | s_63–s_65 | Title card | Logo over the blurred town with a subtitle line |

**Look measurements still to take from these frames (M7.0), not guessed:** texels per block face, outline width and colour, shades per material, saturation, tufts per grass block, leaf opacity, character height in blocks.

## 3. What makes it Terraria

1. **A layer-cake world.** Sky islands, surface, underground, caverns, underworld. Deeper means richer and more dangerous.
2. **Darkness underground, and light you bring.** Ores glint in torchlight.
3. **Everything pops.** Tiles crack and burst into dust, and items pop out and fly to you. Every hit shows damage numbers, knockback and a hit flash.
4. **Readable monsters with simple behaviours.** Hop, walk, swoop, flutter, plus one big boss.
5. **A loot loop.** Pots, chests, life crystals, fallen stars and ores feed crafting, which gives better tools, which take you deeper.
6. **A day/night rhythm.** Build shelter by dusk. Night brings zombies and demon eyes, and the most beautiful sky.
7. **Movement upgrades.** Double jump, then grappling hook, then wings.
8. **A bright, friendly palette** where every biome is unmistakable.

**The 30-minute journey (the demo script):**
- Spawn in the forest at noon with a copper shortsword, pickaxe and axe. The Guide greets you.
- Chop trees and kill slimes for gel. Craft a workbench and torches, and build a hut.
- Dusk turns the sky. Night brings zombies and demon eyes, and fallen stars streak down.
- Dig through dirt into stone. Ores glint in torchlight; there are pots, chests and a life crystal pulsing in a cavern. Find the glowing mushroom biome and an underground lake.
- Smelt iron, silver and gold at the furnace and craft tools at the anvil. Find a grappling hook and a Cloud in a Bottle.
- Reach the underworld: lava lakes, embers and a ruined obsidian tower.
- Climb to a sky island; its chest holds wings.
- Craft the Suspicious Looking Eye from demon-eye lenses. Summon the Eye of Cthulhu at night on a skybridge and win.

## 4. The look

**Six rules:**
1. **Pixel-art surfaces, lit like a diorama.** Crisp texels, soft light, strong silhouettes.
2. **Light is warm and coloured, and darkness is real.** Auto-exposure is clamped so caves stay dark.
3. **Shade is coloured, never grey.** Cool and purple by day, deep blue at night, warm at dusk. Only true underground darkness goes to black.
4. **Emissive means magic.** Lava, ore glints, mushrooms, gems, life crystals and spells all bloom.
5. **Each biome owns a palette** plus fog, grade, particles and a background silhouette set.
6. **Silhouettes first.** Characters, items and projectiles get a 1-pixel dark outline and rim light; blocks get tile framing.

**Textures:** 16×16 texels per face, painted on an 8×8 "art pixel" grid (Terraria draws at 2× pixel scale). Each material has a 3–5 shade ramp that shifts hue: darker shades lean cool, lighter ones warm. There are 3 variants per block, picked by position hash. Textures are fully procedural: the `wc_textures.c` generator is kept and its painters are rewritten. The normal and specular arrays are dropped; only albedo and emissive remain.

**Tile framing in 3D (the signature surface look):**
- When meshing, each face gets an edge mask (which of its 4 edges are exposed to air or to a different material) and a variant id. Both fit in the quad record's spare bits.
- The shader draws a 1-art-pixel dark border on exposed edges and a highlight on the top edge.
- Grass fringe hangs over the top of side faces.
- Where two materials meet (stone in dirt, ore in stone), a merge border is drawn, like Terraria's tile framing.

**Biome palettes (starting anchors for look-dev):**

| Biome | Surfaces | Light / fog | Particles | Landmark |
|---|---|---|---|---|
| Forest (spawn) | grass `#4DB33A`→`#2F7A2A`, dirt `#8A5A3C`→`#5E3A28` | clear blue haze | leaves, butterflies, fireflies at night | tall round-canopy trees, the Guide's hut |
| Desert | sand `#E9C982`→`#C99A55`, sandstone `#D8A866` | pale haze, heat shimmer | blown sand | pyramid (Tier B), cacti |
| Snow | snow `#F2F8FF` with blue shade `#BFD4EE`, ice `#8FD8FF` | cold white fog | snowfall, aurora at night | ice spire, boreal pines |
| Jungle | jungle grass `#8CD436`→`#4E9A1E`, mud `#4A3530` | humid green fog | spores, rain | giant mahogany, vines |
| Corruption | ebon grass `#7A5BA8`, ebonstone `#4E3E6B` | purple haze | dark motes | chasms |
| Ocean | water `#2DB7D3`→`#1A4E8C`, sand `#F2D49B` | bright, glints | spray | palm beach |
| Caverns | stone `#7D7F8C`→`#4F5160` | dark; torch light `#FFB45A` | dust motes | lakes, cabins |
| Glowing mushroom | mud `#2B2A3A`, glow `#3FA9FF`/`#8FE3FF` | blue haze | floating spores | giant mushrooms |
| Underworld | ash `#4B3A3E`→`#2E2326`, lava `#FF7A1A`→`#FFD04A`, hellstone `#FF3D1F` | red-orange fog, heat haze | embers | lava falls, obsidian ruin |
| Sky islands | cloud `#F5F8FF`, brick `#B5543F` | bright | drifting cloud wisps | hut with a chest |

**Postcards: the look acceptance shots.** Each is a `vx_view` preset captured on native and wasm and compared with its reference frame. You judge the side-by-side.

| # | Shot | Beats |
|---|---|---|
| P1 | Forest at noon, spawn hut | ref_14.0s |
| P2 | Forest at dusk, torches on the hut | ref_59.5s |
| P3 | Torch-lit cave with ore glints | ref_45.5s |
| P4 | Glowing mushroom cavern | ref_42.0s |
| P5 | Underworld lava lake and ruined tower | ref_49.0s |
| P6 | Night over the forest: moon, stars, a fallen star | ref_55.5s |
| P7 | Sky island, wings, world below | ref_10.5s, ref_00.5s |
| P8 | Ocean beach at golden hour | ref_00.5s |
| P9 | Snow biome with ice and aurora | ref_33.0s |
| P10 | Jungle | ref_36.0s |
| P11 | Eye of Cthulhu fight at night on a skybridge | ref_55.5s, ref_53.0s |
| P12 | Night fight: zombies, a demon eye and a slime closing in on a swing by the Guide's hut | ref_53.0s (S17) |
| P13 | Combat VFX: a gold broadsword kill, gore, sparkles and damage numbers | ref_53.0s (S17) |

## 5. Renderer

**The decision:** it is written directly on `gpu.h`, as WebCraft is, with no engine renderer. That gives full control of the stylized model, a voxel-specific light volume, and no overhead from features we don't use.

**From WebCraft:**
- **Kept:** chunk streaming, the quad pools, the compute cull-expand, the shadow cascades (including the cached far cascades), TAA and bloom.
- **Replaced:** the shading model, sky, lighting data, materials and post-processing.

**Frame (High preset):**

| # | Pass | Notes |
|---|---|---|
| 1 | Uploads | Mesh pools, light clipmap slabs and edited sections, dynamic light window, entity/particle instance buffers |
| 2 | Cull-expand (compute) | Unchanged |
| 3 | Shadow cascades (sun/moon) | Terrain plus entities; soft PCF (keep PCSS on Ultra only) |
| 4 | G-buffer | Terrain (opaque + cutout) and instanced entities. **2 targets** instead of WebCraft's 4: RT0 albedo + AO, RT1 octahedral normal in RGB10A2 + material id / emissive |
| 5 | Lighting (fullscreen) | Stylized sun/moon ramp × shadow, sky ambient, RGB block light from the volume, dynamic lights, emissive, entity rim light, biome fog, underground darkness |
| 6 | Sky and background | Gradient sky, pixel sun/moon/stars, parallax background ring, stylized clouds (temporal, ¼ res) |
| 7 | Forward translucent | Water, ice and glass, jelly slimes, particles, projectiles and trails, rain and snow |
| 8 | Post | Light shafts (½ res, optional), TAA, bloom, grade + tonemap + vignette, distortion (heat, underwater, hit), entity outline |
| 9 | UI | Engine UI, as now |

### 5.1 Coloured light: Terraria's "Color" lighting in 3D

- **Storage.** Light stays a CPU flood fill per chunk on lanes (the WebCraft job pattern) but becomes RGB. Each voxel holds 4 bits each of R, G, B block light plus 4 bits of sky light, so 16 bits per voxel; WebCraft today has 8 bits and monochrome.
- **Upload.** Changed sections stream into a **2-level 3D clipmap**: 128³ at 1 block and 128³ at 2 blocks, RGBA8, 16 MB total. A staging storage buffer feeds a compute shader that scatters into the 3D texture; the HAL has storage buffers, `image3D` and compute on both D3D12 and WebGPU. Moving the camera only uploads new slabs (toroidal addressing).
- **Sampling.** Each pixel takes a trilinear sample at the surface position plus 0.5 × normal. That gives smooth, sub-block coloured light with natural darkening in corners. Entities, particles and water sample the same volume, so everything is lit consistently.
- **Mesh savings.** Light leaves the quad record: 20 B becomes 12 B per quad, and placing a torch no longer remeshes up to 27 sections.
- **Dynamic light.**
  - The held torch, mining helmet and glowing projectiles get a per-frame flood fill in a 32³ window around the player, which respects walls.
  - Short-lived sparks use up to 64 analytic point lights with no occlusion.
- **Setting:** "Lighting: Color / White / Retro / Trippy", Terraria's own four modes. Retro samples the volume with nearest filtering and stepped values; Trippy hue-cycles the light.

### 5.2 Stylized shading

- **Sun and moon:** wrapped N·L through a soft 3-band ramp, times the cascaded shadow. The shadow side takes a hue-shifted tint from the time-of-day palette.
- **Ambient:** a sky hemisphere gradient (from the sky palette) × sky light from the volume × vertex AO. SSAO is an Ultra-only extra.
- **Block light:** RGB from the volume × albedo, with a small saturation lift near sources.
- **No GGX, SSR or metalness.** Specular appears only as stylized sparkles on water, ice, gems and ore glints (twinkling texels plus emissive).
- **Tonemap and exposure:** a stylized filmic curve that keeps saturation replaces AgX. Auto-exposure is clamped.

### 5.3 Sky and distance

- An art-directed gradient sky keyed by time of day and biome replaces the physical atmosphere LUTs.
- A pixel-art sun with rays, a moon with 8 phases, and twinkling stars.
- Fallen stars streak across the sky and land as pickups.
- An aurora over snow and a blood-moon variant (Tier B).
- **Clouds:** WebCraft's temporal raymarcher is kept but reshaped into chunky, voxel-quantized cloud forms with toon lighting.
- **Parallax background ring:** 3–4 cylinders of procedural silhouettes per biome (mountains, forest, dunes, ice peaks), tinted by fog and time of day. They cross-fade when you enter a biome. These are Terraria's parallax backgrounds, in 3D.
- **Fog:** distance plus height, coloured by biome and time. Underground fog is dark and tinted by the biome.

### 5.4 Liquids

- **Water:** colour ramped by depth, foam lines where it meets geometry, light refraction, pixel sparkles. Waterfalls get a scrolling pixel texture, foam and mist particles.
- **Lava:** emissive with a slowly drifting dark crust, heat haze and embers. It lights the world through the flood fill.
- **Honey:** amber and viscous.
- **Shimmer:** Tier B showpiece.
- **Levels:** liquid levels 0–8 per voxel give sloped surfaces and real flow (§6).

### 5.5 Particles and VFX

The engine's GPU particle system runs inside the engine renderer, so this game gets its own instanced particle pass. Sprites are baked at startup with the engine's `pg_bake_sprite_atlas`, and particles are lit by the light volume. The budget is 16k particles on desktop and 4k on mobile.

**Effects:**
- block dust in the tile's colours;
- item sparkle and pickup trail;
- torch flame, smoke and embers;
- footstep puffs, water splashes, rain and snow, falling leaves;
- fireflies, spores and lava embers;
- sword swing arcs, hit sparks, cute gore chunks, magic trails;
- fallen-star trails, the boss dash streak and the death burst.

### 5.6 Characters and items

- **Voxel puppets.** Box parts with pixel-art skins, drawn instanced into the G-buffer. Parts are rigid and animated procedurally: walk cycle, jump, tool swing, wing flaps. The player is customizable like Terraria's character creator (hair, skin, shirt and pants colours).
- **Slimes** are a forward-rendered jelly: translucent, with a held item visible inside, squashing and stretching on every hop.
- **The Eye of Cthulhu** is a sphere with a procedural iris and veins and trailing tendrils. Phase 2 opens into a toothy mouth.
- **Item sprites** come from parametric pixel-art generators: a shape grammar per item kind plus a palette ramp per material tier, the way Terraria's copper-to-gold tools are recolours. The held item is its icon sprite extruded into a 1-voxel-thick mesh.
- **Readability:** entities get the outline, rim light and a white/red hit flash.

### 5.7 Post-processing

- Light shafts (kept), TAA and bloom (kept, retuned for emissives).
- Grading per biome and time of day: lift, gamma, gain, saturation and hue shift, blended across biome borders.
- Vignette, and heat, underwater and damage distortion.
- Screen shake on big hits and on the boss.
- **Crisp pixels under TAA:** every pixel texture is sampled with texel-AA filtering. A non-TAA path (texel-AA + FXAA) is evaluated for mobile.

### 5.8 Budgets

| Platform | Target |
|---|---|
| PC D3D12, RTX 3060 class | 1440p High ≥ 144 fps |
| PC WebGPU | 1080p High ≥ 60 fps |
| Mobile WebGPU, single-threaded | Low/Medium ≥ 30 fps, aiming for 60 |
| Publish download | ≤ 500 KB brotli (today about 200 KB) |

The stylized frame drops SSR, GGX, the atmosphere LUTs, two G-buffer targets and 8 bytes per quad. That buys the light volume, entities and particles.

## 6. World

- **Scale (Terraria proportions):** the player is about 2.7 blocks tall and 0.8 wide. Doorways are 3 tall and tunnels are 1×3, as in Terraria.
- **Bounds:** a bounded island about 768 blocks across (48×48 chunks) with an ocean rim, inside WebCraft's existing streaming.
- **Height:** the world stays 256 blocks tall, because y is packed into 8 bits in the quad record, the light job and the edit index.

| Layer | y | Content |
|---|---|---|
| Sky | 200–240 | Floating islands (cloud blocks, brick hut with a chest holding wings), rain clouds |
| Surface | ~150 ± 20, sea level 140 | Biomes, trees, grass and flowers, surface caves and chasms |
| Underground | 150–120 | Dirt with stone, copper and tin, small caves, pots |
| Caverns | 120–48 | Stone, iron → silver → gold with depth, gems, life crystals, lakes, glowing mushroom pockets, cabins with chests |
| Underworld | 48–0 | Ash, hellstone, lava sea (y ≤ 14), ruined obsidian towers, lava falls |

**Biomes:**
- **Tier A:** Forest, Desert, Snow (with ice caves), Jungle, Corruption, Ocean, Sky islands, Caverns, Glowing Mushroom, Underworld.
- **Tier B:** Hallow, Shimmer lake, Pyramid, Dungeon.

**Generation:**
- **Rewritten:** `wc_gen.c` becomes the layered island generator.
- **Kept from WebCraft:** generation stays on the CPU, on lanes, deterministic and noise-driven.
- **Structures:** cabins, sky huts and underworld ruins are placed by a region hash and stamped per chunk, the same way trees already cross chunk borders.
- **Spawn:** the area is guaranteed flat forest.

**Liquids:**
- A cellular automaton with levels (fall, spread, settle) for water, lava and honey, with obsidian where water meets lava. Each frame processes only a bounded set of active cells.
- **This needs per-voxel state, which WebCraft lacks.** Add sparse per-section state bytes holding the liquid level, torch attachment side and door open/closed.

**Content:**
- About 50 blocks, plus furniture as small multi-block shapes: workbench, furnace, anvil, chest, door, chair, table, platforms.
- **Platforms are one-way:** jump up through them and press down to drop.

## 7. Gameplay slice

| System | Tier | Notes |
|---|---|---|
| Third-person orbit camera with collision, first-person toggle | A | Block targeting: ray from the camera, reach measured from the player, pixel outline on the target, smart-cursor assist |
| Movement | A | Fast acceleration; run, jump with coyote time, double jump, grappling hook (attach, pull, swing), wings (flight time and glide), swim, fall damage |
| Health / mana / death / respawn | A | 100 HP shown as hearts, +20 per life crystal; mana as stars, from mana crystals |
| Mining | A | Pick power and speed by tier; damage accumulates; crack overlay (WebCraft generates one but never uses it); dust; a drop on break. The axe fells whole trees |
| Items and inventory | A | Drops pop out, bob, merge and fly to you with a pickup sound and floating text. 50 slots + 10 hotbar, 3 armor, 3 accessories, stacks to 999 |
| Crafting | A | Stations by proximity (workbench, furnace, anvil); about 40 recipes from const tables; a scrolling craftable list |
| Building | A | Blocks, platforms, doors, wall/floor torches, furniture, chests that store items |
| Combat | A | Melee swing arcs (a sector test), wooden bow + arrows, a sparking magic wand; knockback, i-frames, crits, damage numbers |
| Enemies | A | 4 behaviours with biome variants. **Hopper:** slimes (green, blue, ice, jungle, lava, Pinky). **Walker:** zombie, skeleton. **Swooper:** demon eye, eater of souls. **Flutterer:** bats (cave, ice, jungle, hell). Spawns depend on time, depth and biome; despawn when far |
| Boss: Eye of Cthulhu | A | Summoned at night or arrives by event. Phase 1: hover, dash, spawn Servants. Phase 2: mouth, faster dashes. Event text, boss bar, boss music, loot |
| Loot | A | Pots (coins, potions, torches, rope), chests (wooden, gold, sky, shadow), life crystals, fallen stars, coins that auto-convert |
| Guide NPC | A | Tip dialogue; a first-night hint |
| Day/night | A | Terraria's 24-minute cycle (15 min day, 9 min night); night spawns and fallen stars |
| Minimap + full map | A | Top-down, revealed as you explore. Underground it switches to a horizontal slice at the player's depth |
| King Slime, Merchant + housing check, blood moon, saves | B | |

**Starting content list:**
- **Tools and weapons:** pickaxes, axes and swords in copper, iron, silver and gold (one generator each, palette-swapped by tier); wooden bow and arrows; wand of sparking.
- **Light:** torches, glowsticks, mining helmet.
- **Utility:** rope, platforms, workbench, furnace, anvil, chest, door, chair, table.
- **Materials:** blocks, gel, lens, ores, bars, gems, fallen star.
- **Progression:** life crystal, mana crystal, lesser healing potion, magic mirror (recall home), Suspicious Looking Eye.
- **Accessories:** grappling hook, cloud in a bottle, Hermes boots, wings.
- **Critters:** bunny, bird, butterfly, firefly.

## 8. UI and audio

**UI** (engine Clay UI in standalone mode, as WebCraft does):
- **Font:** the engine's UI font with outline and drop-shadow styling, so no third-party art ships.
- **Layout:**
  - hotbar top-left;
  - Terraria-like translucent blue inventory and crafting panels;
  - hearts and mana stars top-right, with the minimap below them;
  - event log bottom-left ("Eye of Cthulhu has awoken!");
  - boss bar at the bottom;
  - item tooltips with rarity colours.
- **Screens:** death screen ("You were slain…"), and a title screen with a golden-hour flyover and menu.
- **World-anchored text:** damage numbers and pickup text are projected from world to screen.
- **Icons:** CPU-rasterized from the item generators, like `wc_icons.c` today. The UI samples whole textures with a linear sampler, so crisp pixel icons need a custom per-element pipeline or a small engine change (§10).
- **Touch:** `wc_touch.c` is extended with a move stick, jump, use/attack, grapple, hotbar taps and inventory.

**Audio:**
- **SFX:** WebCraft's synthesized sounds plus the engine's synthesis kit (`pg_sfx_bank_*`).
  - **Mining and building:** dig per material, break, place, grass rustle, chest, door.
  - **Combat:** swing, hit, slime squish, player hurt, boss roar.
  - **Pickups and items:** item pop, coin jingle, potion.
  - **Movement:** jump and double-jump puff, grapple fire/latch, wing flap.
  - **World:** fallen-star whoosh, splash, lava bubbles.
- **Ambience:** the engine's ambience generator. Day birds and wind, night crickets, cave drips and drone, underworld rumble, ocean.
- **Music:** the engine has no sequencer, so we write a small game-side tracker (patterns + synth voices) that feeds the mixer's app-fed PCM ring. Six short loops: Title, Day, Night, Underground, Underworld, Boss. No audio files ship.

## 9. Code plan

`src/webcraft/` becomes `src/voxaria/` and the `wc_` prefix becomes `vx_`, in one mechanical commit in M0. The game diverges completely, and `master` keeps WebCraft.

| Area | WebCraft today | Plan |
|---|---|---|
| World streaming, lane jobs (`wc_world`) | Unbounded, 3 job kinds on lanes | Keep. Add the bounded island and per-section state bytes |
| Generation (`wc_gen`) | Minecraft terrain, 20 biomes | Rewrite: layered island, Tier A biomes, structures, loot placement |
| Light (`wc_light`) | Mono 4+4 bits, baked into quads | RGB + sky at 16 bits per voxel, clipmap upload, dynamic window |
| Meshing (`wc_mesh`) | 20 B quads with light | 12 B quads plus edge mask and variant; liquid levels; furniture shapes |
| Mesh store, cull-expand | Pools, compute expand | Keep |
| Textures (`wc_textures`) | 85 layers × 3 arrays (albedo, normal, spec) | Terraria tile painters; albedo + emissive only |
| Renderer (`wc_render`, shaders) | PBR deferred, physical sky, SSR | The stylized frame in §5 |
| Player (`wc_player`) | Creative flight, 1.8 tall, no health | Terraria movement set, health, third-person camera |
| Entities (`wc_entities`) | Particles + fireflies rebuilt on the CPU each frame | Instanced puppets, item drops, projectiles; GPU-instanced particles |
| Audio (`wc_audio`) | Synth SFX, no music | Plus the synthesis bank, ambience and the music tracker |
| UI (`wc_ui`, `wc_touch`, `wc_icons`) | Creative menus | Terraria HUD, inventory, crafting, map, touch actions |
| Settings (`wc_settings`) | PBR presets | Stylized presets plus lighting mode |
| Game loop (`wc_game`) | Creative loop, `wc_*` commands | Game state, systems, `vx_*` commands |

**New modules:**
- items, recipes and loot (const tables, like the block X-macro table, so `type_registry off` stays);
- inventory and crafting;
- combat;
- enemies (AI and spawning) and the boss;
- NPC dialogue;
- liquids;
- camera;
- puppets and animation;
- VFX;
- music tracker;
- map.

**Engine (needs your approval):** nothing is required, because every technique above uses existing HAL features. Two optional changes:
- UI image sub-rects plus a nearest-filter sampler, so pixel icons can share one atlas. Without it we keep one texture per icon as WebCraft does, or use a custom per-element pipeline.
- A persistent-storage API on web, needed only if we add saves (Tier B).

## 10. Steps

Status: `[ ]` open, `[x]` done. Work the steps in order and update the status here as each one completes.

- A "postcard" is a §4 shot captured with `vx_view` on native and on wasm.
- The last step of every milestone also requires a clean native build, clean publish builds, and run logs with no errors or asserts.
- Performance benchmarks are not part of these steps (§14).

### M0. Setup and look-dev harness
- [x] **M0.1 Rename.**
  - Move `src/webcraft` to `src/voxaria` and change the `wc_` prefix to `vx_` for files, symbols, types, shaders and commands.
  - Set the product name to Voxaria in `hzproject.hzt` and `web/index.html`.
  - *Done when:* native and both publish builds are clean and a scripted run reaches the world.
- [x] **M0.2 Look-dev commands.**
  - `vx_view <postcard>` sets camera, time and weather; `vx_time` sets the time.
  - *Done when:* scripted runs of both commands show the expected camera and time in screenshots.
- [x] **M0.3 Postcard capture.**
  - A script captures every defined postcard on native and wasm into `out/postcards/<platform>/`.
  - *Done when:* it produces P1, P2, P3 and P6 from today's world (the "before" set) on both platforms.

### M1. Stylized renderer and light
- [x] **M1.1 Tile textures.**
  - Procedural Terraria-style painters: an 8×8 art-pixel grid at 16×16, hue-shifted 3–5 shade ramps, 3 variants per block.
  - Block set: grass, dirt, stone, clay, sand, wood, planks, leaves, copper/iron/silver/gold ore, gems, mud, mushroom grass, ash, hellstone, obsidian, torch.
  - Only the albedo and emissive arrays remain; the normal and specular arrays are removed.
  - *Done when:* a `vx_dump_textures` contact sheet shows every layer.
- [x] **M1.2 Tile framing.**
  - A 12-byte quad record carries an edge mask and a variant.
  - The terrain shader draws exposed-edge borders, top highlights, grass fringe and material merge borders.
  - *Done when:* a close-up screenshot shows framed tiles.
- [x] **M1.3 RGB light.**
  - Flood fill with 4-bit R, G and B block light and 4-bit sky light per voxel.
  - Per-block light colours; incremental updates on edits.
  - *Done when:* a scripted scene with differently coloured sources shows coloured pools that stop at walls.
- [x] **M1.4 Light clipmap.**
  - A two-level 3D texture: 128³ at 1 block and 128³ at 2 blocks.
  - Toroidal slab streaming, and section uploads through a compute scatter.
  - Per-pixel trilinear sampling; light is removed from the mesh path.
  - *Done when:* screenshots across chunk borders show no seams, and a torch placement updates light without a remesh (logged).
- [x] **M1.5 Dynamic light.**
  - A 32³ flood-fill window around the player for held lights.
  - Up to 64 analytic point lights for short effects.
  - *Done when:* two screenshots in a dark cave show the held-torch pool following the player and stopping at walls.
- [x] **M1.6 Stylized shading.**
  - A two-target G-buffer.
  - Sun and moon light through a ramp with hue-shifted shade.
  - Sky ambient × sky light × AO; block light; emissive.
  - Clamped exposure and a stylized tonemap.
  - The SSR, GGX, metalness and atmosphere LUT passes are deleted.
  - *Done when:* P1 and P3 screenshots show the stylized model and no code for the removed passes remains.
- [x] **M1.7 Sky.**
  - Gradient sky keyed by time and biome; pixel sun with rays; moon with 8 phases.
  - Twinkling stars, falling-star streaks, aurora over snow.
  - *Done when:* noon, dusk and night screenshots show each element.
- [x] **M1.8 Clouds.**
  - The temporal raymarcher reshaped into chunky, voxel-quantized clouds with toon lighting.
  - *Done when:* day and dusk screenshots show them.
- [x] **M1.9 Fog and grading.**
  - Distance and height fog.
  - A per-biome grade (lift, gamma, gain, saturation, hue) blended across borders.
  - Vignette, heat and underwater distortion.
  - *Done when:* screenshots of two biomes show distinct fog and grade.
- [x] **M1.10 Lighting modes.**
  - A Color / White / Retro / Trippy setting.
  - *Done when:* four screenshots of one view show the four modes.
- [x] **M1.11 Pixel crispness and bloom.**
  - Texel-AA sampling for every pixel texture under TAA.
  - Bloom driven by emissives.
  - *Done when:* a close-up shows crisp texels and a distant view shows no moiré.
- [x] **M1.12 Look-dev scenes.**
  - A minimal generator variant with a forest surface, a torch-lit cave, a mushroom cavern and an underworld pocket.
  - `vx_view` presets for P1–P6.
  - *Done when:* P1–P6 are captured on native and wasm and compared in writing with their reference frames.
  - *Comparison (M1 close-out, `out/postcards/{native,wasm}/P1–P6`; native and wasm match frame for frame):*
    - P1 forest noon vs ref 0.5 s / 14.0 s: our trees, grass and flowers read as shapes at distance with toon shading and soft shadow; the reference's daylight is flatter. It still lacks the town, NPCs and sky islands (M2, M4).
    - P2 forest dusk with torches vs ref 59.5 s: violet twilight sky and warm torch pools on the grass carry the mood the reference gets from dusk + torches; no buildings yet (M2.6).
    - P3 torch-lit cave vs ref 45.5 s: coloured pools (warm torch, ice torch, pink crystal) bounded by rock, unlit rock goes dark; the reference lights its shaft evenly.
    - P4 mushroom cavern vs ref 42.0 s: glowing blue mushrooms and mushroom grass in a dark cavern; ours are smaller than the reference's giant mushrooms (giant variants are M2.4 content).
    - P5 underworld vs ref 49.0 s: lava sea, glowing hellstone and a red ceiling haze match its warm darkness; no ruins or embers yet (M2.4, M3).
    - P6 night sky vs ref 55.5 s: moon phase, pixel stars and a falling star; no boss VFX yet (M5).

### M2. The world
- [x] **M2.1 Island and layers.**
  - A bounded island about 768 blocks across with an ocean rim.
  - Terrain shape and layer heights as in §6.
  - *Done when:* an overview screenshot shows the island, and screenshots at each layer's depth show its materials.
- [x] **M2.2 Surface biomes.**
  - Forest, desert, snow, jungle, corruption and ocean beach, with their blocks and palettes.
  - *Done when:* there is one screenshot per biome.
- [x] **M2.3 Trees and foliage.**
  - Trees: round-canopy forest trees, boreal pines, mahogany with vines, palms, cacti.
  - Grass, flowers and mushrooms; wind sway.
  - *Done when:* screenshots show each tree type.
- [x] **M2.4 Caverns.**
  - Caves, tunnels and caverns.
  - Ores by depth, gems, life crystals, pots and chests.
  - Underground lakes, ice caves and cabins.
  - *Done when:* screenshots show ores, a life crystal, a pot, a chest and a cabin.
- [x] **M2.5 Glowing mushroom biome.**
  - *Done when:* P4 comes from the generated world.
- [x] **M2.6 Underworld.**
  - Ash, hellstone, a lava sea, ruined obsidian towers and lava falls.
  - *Done when:* P5 comes from the generated world.
- [x] **M2.7 Sky islands.**
  - Cloud blocks, and huts with chests.
  - *Done when:* P7 comes from the generated world.
- [x] **M2.8 Per-voxel state.**
  - Sparse per-section state bytes: liquid level, torch side, door state.
  - Edits preserve them.
  - *Done when:* a scripted edit survives chunk unload and reload (screenshots before and after).
- [x] **M2.9 Liquid simulation.**
  - Levels 0–8 with fall, spread and settle, for water, lava and honey.
  - Obsidian where water meets lava.
  - An active-cell budget per frame.
  - *Done when:* a scripted spill flows and settles, and water meeting lava forms obsidian.
- [x] **M2.10 Liquid rendering.**
  - Sloped surfaces.
  - Water: depth ramp, foam, refraction, sparkles. Waterfalls with mist.
  - Lava: crust, heat haze, embers, light. Honey.
  - *Done when:* there are screenshots of a lake, a waterfall and a lava lake.
- [x] **M2.11 Background ring.**
  - Parallax silhouettes per biome, with cross-fade between biomes.
  - *Done when:* screenshots in two biomes show their silhouettes.
- [x] **M2.12 Ambient particles.**
  - Leaves, spores, snow, embers, dust motes and fireflies.
  - *Done when:* biome screenshots show them.
- [x] **M2.13 World postcards.**
  - P1–P10 all come from the generated world.
  - *Done when:* they are captured on native and wasm and compared in writing with their references.
  - *Comparison (M2 close-out, `out/postcards/{native,wasm}/P1–P10`, seed 12345; native and wasm show the same spot and framing for every postcard, 0 WebGPU validation errors on wasm):*
    - P1 forest noon vs ref 14.0 s: the spawn house, forest, flowers and falling leaves match the reference's daylight village feel; the reference has a whole town and NPCs, which come in M4.9 (one guide) and are otherwise out of scope.
    - P2 forest dusk vs ref 59.5 s: the pink dusk sky and the house's torches match; the reference's lit windows and crowd are not reproduced.
    - P3 torch-lit cave vs ref 45.5 s: torches, crystals and gem glints in a dark cavern; the reference is a dug shaft with a lit player, which is M3 gameplay.
    - P4 mushroom cavern vs ref 42.0 s: glowing blue grass, mushrooms, giant mushroom stems and drifting spores match the reference closely; ours is darker between lights.
    - P5 underworld vs ref 49.0 s: lava sea with drifting crust, ash islands, ruined obsidian towers and lava falls, embers and heat shimmer; the reference's hellstone towers are taller and brighter.
    - P6 night forest vs ref 55.5 s: moon, stars and a falling star over dark trees; the reference's boss fight is M5.
    - P7 sky island vs ref 10.5 s / 0.5 s: floating island with hut, tree, pond and waterfall to the world below, layered horizon; wings are M3.3.
    - P8 beach at golden hour vs ref 0.5 s: palms, sand, ocean and the distant mountain ring; the reference is an overview of the whole map.
    - P9 snow and aurora vs ref 33.0 s: boreal forest, snow fall and aurora; the reference is a blizzard by day — our weather can do that, the postcard keeps the aurora night.
    - P10 jungle vs ref 36.0 s: elevated view over the jungle canopy with blue-grey mountains on the horizon, as in the reference; it has no jungle temple (not in the plan's structure list).

### M3. Player, camera, dig and build
- [x] **M3.1 Camera and targeting.**
  - A third-person orbit camera with collision, and a first-person toggle.
  - Targeting by camera ray with reach measured from the player; target outline; smart cursor.
  - *Done when:* screenshots in both camera modes show the target outline.
  - *Evidence:* `out/m3_cam3.png` (third person, outline on the grass in front of the player) and `out/m3_cam1.png` (after V, first person, outline); `out/m3_smart0/1.png` (Left Ctrl smart cursor picks a reachable block when the crosshair's block is out of reach).
- [x] **M3.2 Player character.**
  - A procedural voxel puppet with customizable colours.
  - Idle, walk, jump, fall, swing and fly animations.
  - *Done when:* screenshots show each pose.
  - *Evidence:* `out/pose_{idle,walk,jump,fall,swing,fly}.png` (one pose each, real input: W, Space, a 14-block drop, a left click, flight), `out/pp_{a,b,c}.png` (three colour/hair combinations), `out/pp_ui.png` (the Settings screen's CHARACTER section).
- [x] **M3.3 Movement.**
  - Terraria-scale run, jump with coyote time, fall damage and swimming.
  - Double jump, grappling hook, and wings with flight time and glide.
  - *Done when:* scripted runs show each movement.
  - *Evidence (logs `out/move_*.log`, scripted `--commands` runs on a `vx_fill` course):* run and step-up onto a one-block ledge, stopped by a two-block wall (`mv_step.png`, `mv_wall.png`, log `step-up`); variable jump: a 0.04 s tap peaks 2.3 blocks, a held jump 4.9; coyote: jump pressed 0.05 s after running off a 3-block ledge fires (log `jump (coyote)`); fall damage: 31-block fall takes 59, 45-block fall kills (`hp_hurt.png`); swimming (`mv_under.png` with breath bubbles, `mv_swim.png`); double jump with cloud puff (`mv_double.png`, log `double-jump` 5 blocks up); grappling hook fired at the crosshair point, chain, pull and hang on a wall (`mv_chain.png`, `mv_hang.png`); wings flapping and gliding (`mv_wings.png`, `mv_glide.png`).
- [x] **M3.4 Health and mana.**
  - Hearts and stars HUD, damage, death screen and respawn.
  - *Done when:* a scripted fall death shows the death screen and the respawn.
  - *Evidence:* `hp_hurt.png` (hearts after a 59-damage fall, red flash), `hp_dead.png` ("You were slain..." with the cause and countdown), `hp_respawn.png` (full life at the spawn after 5 s); logs for drowning (breath out at 20 s, then death) and lava (50 per hit, death on the second).
- [x] **M3.5 Mining.**
  - Pick power and speed by tier; accumulated damage with a crack overlay.
  - Dust bursts and block drops; the axe fells trees.
  - *Done when:* a scripted run shows cracks, dust, drops and a felled tree.
  - *Evidence (`out/mine.sh`, `out/mine2.sh`, logs `out/mine_fell.log`, `out/mine_pick.log`):* `m35_crack2.png` (crack stage over a stone block after two copper-pickaxe hits, with chips of stone dust), `m36_pickup.png` (the broken block's drop in the hole), `m35_crack.png` (a 7-log tree felled by the copper axe from one cut: leaves cleared, log drops popping), log `mining: felled a tree of 7 logs` and `items: picked up` 2+2+2+1 Wood.
- [x] **M3.6 Items and inventory.**
  - A const item table.
  - Item entities pop, bob, merge and fly to the player with pickup text and sound.
  - 50 slots, a 10-slot hotbar, 3 armor and 3 accessory slots, stacks to 999.
  - A `vx_give` command.
  - *Done when:* screenshots show drops being collected and the inventory.
  - *Evidence:* `m35_drops.png`/`m37_held3p.png` (collected stacks on the hotbar, world-anchored "Stone Block" pickup text over the player), `m36_inventory.png` (5x10 bag with the hotbar row, Head/Body/Legs, three accessory slots, Defense, Trash), `m36_tooltip.png` (right-click equipped Iron Helmet and Angel Wings, Defense 2, a rarity-coloured tooltip), `m36_cursor.png` (right-click took 3 of 250 dirt onto the cursor), `m36_trash.png` (stack dropped into the trash); `vx_give`, `vx_drop`, `vx_select` and `vx_point` commands.
- [x] **M3.7 Item art.**
  - Parametric pixel-art generators for tools, weapons, bars, ores, gems, potions, torches, furniture and accessories.
  - Icons, and extruded held items.
  - *Done when:* a `vx_dump_icons` contact sheet shows every item, and a screenshot shows a held item.
  - *Evidence:* `out/icons.png` (all 108 items: block items as their isometric icons, including ores, torches and the furniture blocks, then the generated art for swords, pickaxes, axes by tier, bow, arrow, wand, bars, gel, lens, stars, crystals, potion, mirror, eye, hook, bottle, boots, wings, helmets, armor, glowstick and the four gems), `m37_held1p.png` (copper pickaxe in first person), `m37_held3p.png` (in the puppet's hand). Furniture that M3.8 adds draws through the same block icon path.
- [x] **M3.8 Building.**
  - Block placement, one-way platforms, doors, wall and floor torches.
  - Multi-block furniture, and chests with storage.
  - *Done when:* a scripted run builds a hut with a door, platform, torch, table, chair and chest.
  - *Evidence (`out/hut.sh`, logs `out/hut_hut.log`, `out/hut_chest.log`):* walls by `vx_fill`, then every piece placed by clicks: `m38_door.png` (3-tall door with frame, crossbars and handle, closed), `m38_open.png` (opened by right click, log `building: door opened`), `m38_inside.png` (3x2 table, chair, chest, wall torch, platform), `m38_platform.png` (standing on the platform: log `jump` from 150 then `land at ... 153.0`, Shift drops through to `land at ... 150.0`), `m38_chest.png` (chest opened by right click, Deposit All moved 120 dirt and 7 iron bars in), `m38_chest_kept.png` and log `chest ... holds items and stays` (a non-empty chest refuses the pickaxe).
- [x] **M3.9 Crafting.**
  - Stations by proximity, about 40 recipes in a const table, and the crafting UI.
  - *Done when:* a scripted run crafts at each station.
  - *Evidence (`out/craft.sh`, log `out/craft_all.log`):* every craft clicked in the UI: `crafting: 1 x Work Bench at By hand`, `3 x Torch at By hand`, then the bench placed and `1 x Furnace at Work Bench`, `1 x Iron Anvil at Work Bench`, both placed, then `1 x Copper Bar at Furnace` and `1 x Iron Broadsword at Iron Anvil`; `m39_hand.png` (by-hand list), `m39_stations.png` ("Near: Work Bench, Furnace, Iron Anvil" with the recipe grid), `m39_placed.png` (the three stations).
- [x] **M3.10 HUD and UI.**
  - Hotbar, inventory and crafting panels, tooltips with rarity colours.
  - Minimap and full map, event log, world-anchored text.
  - Touch controls for the new actions.
  - *Done when:* there are screenshots of each panel and of the touch layout.
  - *Evidence (logs `out/m310*.log`):* hotbar with counts and the held item's name (`m310_hud.png`, `m37_held1p.png`), inventory with equipment, defense and trash (`m36_tooltip.png` shows a rarity-coloured tooltip), chest panel (`m38_chest.png`), crafting panel with recipe tooltip (`m39_stations.png`), minimap and the "Crafted Work Bench" event-log line (`m310_hud.png`), full map of the explored world with player and spawn markers (`m310_map.png`), world-anchored pickup text (`m37_held3p.png`), touch layout with Map, Hook, Heal, Jump, Down and the bag button (`m310_touch.png`, `m310_touch_crop.png`) and the touch inventory where a tap equipped a helmet and wings (`m310_touch_inv.png`). Not verified on a real phone: the compact layout sizes are computed for a 390-pixel-tall landscape screen.
- [x] **M3.11 Dig-and-build sounds.**
  - Synthesized sounds for mining, building, items and movement.
  - *Done when:* a scripted run's log lists each sound being triggered.
  - *Evidence (`out/sfx.sh`, log `out/sfx.log`, counts in `out/sfx_counts.txt`):* one run triggers and logs (`audio: <name>`) break, dig, place and step by material, swing, item pickup, tree fall, craft, chest open, door open and close, jump, double jump, land, hook fire, hook latch, wing flap, splash, hurt, drink, shimmer (mirror) and death. Verified by the trigger log only; the sounds were not listened to in this session.
- [x] **M3.12 Dig-and-build run.**
  - A scripted run chops a tree, crafts a workbench and torches, builds a hut, and digs to the caverns.
  - *Done when:* there are screenshots of each beat.
  - *Evidence (`out/m312.py`, run log of the passing run):* 14 trees felled (145 wood), Work Bench and 3 Torches by hand, door, table and chair at the bench, a 6 x 6 x 4 hut of 93 placed blocks (all accounted for by `out/hutcheck.py`), table, chair and wall torch inside, then a 2-wide shaft to y = 104 (cavern layer starts at 120). Shots: `m312_chop.png`, `m312_craft_hand.png`, `m312_craft_bench.png`, `m312_hut.png`, `m312_hut_inside.png`, `m312_shaft.png`. The gel for torches was given by command (slimes arrive in M4). Later reruns failed when real mouse input reached the focused window.

- [x] **M3.13 Silhouette outlines.**
  - A screen-space pass on depth and normal discontinuities: about one pixel per 720 lines, constant on screen, darkening the near side's own colour (refs 14.0 s, 25.0 s, 53.0 s show darker-hued lines, not black ones).
  - Applies to characters, items, furniture and other models, and to block silhouettes, fading with distance so far foliage does not turn to noise.
  - Runs at native resolution, stable under TAA jitter, on D3D12 and on both WebGPU builds.
  - *Done when:* side-by-side crops against those refs show matching outlines on the player, furniture and terrain edges, natively and on wasm.
  - *Evidence:* `shaders/vx_deferred.glsl` `outline_at`: silhouettes where the neighbour within r = res.y/540 px lies farther than the surface's own depth slope predicts, convex creases from the normal and the reconstructed position; the near side's colour × (0.26, 0.24, 0.32), fading out from 48 to 160 blocks. Mask in debug view 9 (`out/m313_mask.png`): lines on silhouettes and convex edges, none on flat faces. Crops: `out/cmp/m313_outline.png` (refs 14.0, 25.0, 53.0 s over ours: roof steps, canopy, trunks) and `out/cmp/m313_player_furn.png` (refs 14.0 and 53.0 s; ours with the player, anvil, pot and workbench on D3D12, and the player and canopy on WebGPU, `out/m313_furn_wasm.png`). Multi-threaded WebGPU was captured; the single-threaded build is covered by the M6.3 publish build, with no separate capture.

### M4. Fight and loot
- [x] **M4.1 Creatures.**
  - Instanced puppet rendering, voxel collision for every entity, hit and hurt boxes.
  - Procedural creature generators.
  - *Done when:* a screenshot shows several creature types.
  - *Evidence:* every puppet and creature box is one 64-byte `VxBoxGpu` record (camera-relative affine rows, sRGB tint with the hit flash in alpha, layer and flags) drawn by `vx_box`/`vx_box_shadow` as `gpu_draw(n*36)` from a storage buffer; the player puppet renders as before (`out/box_furn.png`). `src/voxaria/vx_creatures.c`: slime (6 variants), zombie, skeleton (plain and armoured, after the S17 skeletons), demon eye, eater of souls, bat (4 variants), each a box puppet on the shared puppet grain with procedural animation (hop squash, shambling walk, flapping wings, whipping nerve, snapping mandibles). Collision is the player's voxel AABB test (`vx_aabb_collides`) with sub-stepped per-axis moves; the hurt box is the creature's AABB, the contact hit box the same shrunk by 0.1. Shots: `out/m41_creatures.png` (in the forest), `out/m41_lineup.png` (green, lava and Pinky slimes, both skeletons, eater, cave and hell bats, demon eye).
- [x] **M4.2 Behaviours.**
  - Hopper: slimes (green, blue, ice, jungle, lava, Pinky).
  - Walker: zombie and skeleton.
  - Swooper: demon eye and eater of souls.
  - Flutterer: bats (cave, ice, jungle, hell).
  - *Done when:* a `vx_spawn` test shows every enemy type moving as designed.
  - *Evidence:* `vx_spawn` + `vx_mobs` runs (`out/look.log`, `out/m42_variants.log`): slimes of all six variants hop at the player (vertical launch 12–15.5 b/s, squash on landing, ground flag toggling, 2–4 blocks closed per 2 s); zombie and skeleton chase at 3.4 and 4.2 b/s, climbing one-block ledges and jumping walls; the demon eye and eater accelerate at the player, overshoot and land contact hits (`vitals: 18 damage (... Demon Eye.)`, `22 damage (... Eater of Souls.)`); all four bat variants flutter with a fresh heading jitter every 0.2–0.6 s. Shots `out/m42_t1.png`, `out/m42_t2.png`.
- [x] **M4.3 Spawning.**
  - Spawn rules by time, depth, biome and distance; despawn when far.
  - *Done when:* scripted day, night, cavern and underworld runs spawn the expected sets (log and screenshots).
  - *Evidence:* `vx_cr_spawn_update`: an attempt every 0.25–0.5 s on open ground (a floor with three free blocks, out of liquid) 26–58 blocks from the player, in the player's own layer, under a cap per environment counted within 72 blocks (surface day 4, night 8, underground 7, underworld 7); despawn beyond 110 blocks in 3D, and at dawn zombies and demon eyes stop chasing and leave beyond 40. Logs `out/m43_{day,night,cavern,underworld}.log`: day spawns green and blue slimes; night zombies and demon eyes (the player is slain by a zombie in 14 s, `out/m43_night.png`); the cavern at y 70 skeletons, cave bats and blue slimes; the underworld at y 15 Hellbats and Lava Slimes, while the surface slimes left behind despawn. Shots `out/m43_grid.png` (the four runs).
- [x] **M4.4 Combat.**
  - Melee swing arcs, a wooden bow with arrows, a wand of sparking with mana.
  - Knockback, i-frames, crits, damage numbers, hit flash, enemy health bars.
  - *Done when:* a scripted fight shows each weapon hitting.
  - *Evidence:* `src/voxaria/vx_combat.c`. Swords: a sector test (reach 3.3 broadsword / 2.5 shortsword, within 78° of the aim, point blank always) against each creature's box over the whole swing, one hit per creature per swing, drawn as a crescent of glowing sparkles in the plane facing the view (S17's weapon trails are sparkle streams). Bow: consumes Wooden Arrows, arrows (box shaft, head, fletching) fly at 30 b/s under gravity; wand: 2 mana per spark at 15 b/s, a burning wake, a point light. Shots leave the weapon hand toward the first block or creature on the crosshair ray, so the third-person shoulder camera aims true. Damage ±15 %, 4 % crits (orange, bigger numbers), defense halves, knockback scaled by resistance, 0.2 s creature i-frames, the player's 0.67 s, white hit flash, life bars under hurt creatures, an aim pose while shooting. Scripted fight `out/m44.log`: `Zombie hit by Copper Broadsword` ×3, `Blue Slime hit by Wooden Arrow` ×2 and `Zombie hit by Wooden Arrow` ×3, `Blue Slime hit by Wand of Sparking ... killed`, `Skeleton hit by Wand of Sparking` ×6; a crit in the previous run (`8 crit`). Shots: `out/m44_sword.png` (third-person crescent, health bar, damage number), `out/m44_sword1p.png` (first person), `out/m44_proj.png` (bow drawn with an arrow in flight; wand pointed), `out/m44_spark.png` (spark and wake in first person).
- [x] **M4.5 Kills and loot.**
  - Enemy deaths with gore and loot tables; coins that auto-convert.
  - *Done when:* a scripted kill shows gore and drops, and a coin stack converts.
  - *Evidence:* a death throws 10–20 tumbling lit chunks in the creature's own colours (instanced boxes in a 320-chunk debris pool that bounce, roll to a stop and shrink away; the S17 impacts throw cube debris), and drops loot: coins at Terraria's per-enemy value ±20 % in silver and copper, gel from slimes (8–12 from Pinky), a lens from a third of demon eyes. Copper, Silver, Gold and Platinum Coins (new items with a coin sprite) change up in whole hundreds as they enter the inventory, from any path. Run `out/m45.log`: with 95 Copper Coins held, a zombie kill drops 56 copper, picked up as `1 x Silver Coin` and 51 copper; a slime adds 2 Gel and 28 copper (79). Shot `out/m45_gore.png`: slime and zombie chunks on the grass, the gold sword's crescent, the `Copper Coin (28)` pickup.
- [x] **M4.6 World loot.**
  - Pots, and chest loot by depth and biome.
  - Life crystals (+20 max HP), fallen stars at night, the mana crystal recipe.
  - *Done when:* scripted interactions show each one.
  - *Evidence:* `src/voxaria/vx_loot.c`. Pots shatter into clay sherds (debris chunks) and spill coins scaled by depth (15–60 copper on the surface, 50–200 underground, 200–600 in the underworld) plus a chance of a potion, torches (ice torches in snow), arrows or glowsticks. A generated chest gets its record and loot the first time it is opened or struck, from a hash of its position: a prize by chest and depth (surface: Wand of Sparking, Wooden Bow with arrows, Copper Broadsword; underground: Magic Mirror, Cloud in a Bottle, Hermes Boots, Wand of Sparking, Mining Helmet; gold chests: Magic Mirror, Cloud in a Bottle, Hermes Boots, Grappling Hook), then bars by depth, potions, torches by biome, arrows, glowsticks and biome gems underground, and coins by depth; placed chests start empty. At night, on the surface, a star lands 14–44 blocks away every 20–60 s: a glowing crossed star with a continuous golden streak and a point light, falling through tree crowns, landing as a Fallen Star that twinkles and lights the ground, and fading at dawn. Logs: `out/m46.log` (`pot at 39 150 -14 broke with 30 copper`, surface Forest chest: Wand of Sparking, Tin Bars, potion, coins), `out/m46c.log` (gold chest at y 60: Hermes Boots, 6 Gold Bars, potions, 13 Torches, 35 silver), `out/m46d.log` (underground wooden chest: Cloud in a Bottle, Silver Bars), `out/m46e.log` (Snow chest: Wooden Bow with 48 arrows), `out/m46g.log` (Life Crystal mined, picked up and used: `max life to 120`; 5 Fallen Stars crafted: `Mana Crystal made`, `max mana to 40`), `out/m46h.log` (`fallen star landed`, `a fallen star faded at dawn`). Shots in `out/m46_grid.png`: a pot, its sherds and coin pop, the open gold chest, a star streaking across the night sky lighting the floating island gold, a landed star's glow on the grass.
- [x] **M4.7 Slime jelly.**
  - Forward-rendered translucent jelly with an item inside, squash and stretch.
  - *Done when:* a close-up screenshot shows a hopping slime.
  - *Evidence:* `shaders/vx_jelly.glsl` + a jelly pass after the liquids in `vx_render.c`: slime bodies are jelly boxes in their own record buffer (`VX_JELLY_CAP` 512), drawn front faces only; the fragment shader inverts the box's axes per vertex (cofactors, no `inverse()`, so it compiles for D3D12 and WebGPU alike) and intersects the view ray with the box in its own frame for the exact gel thickness, then refracts a copy of the lit scene (with liquids), absorbs it by Beer–Lambert in the body colour's complement, adds wrapped sun, sky and block light from the volume, a sky Fresnel rim, a hard pixel glint and Terraria's square highlight; the lava slime glows. Jelly casts shadows through the box shadow pipeline. Each slime (not lava) swallows an item at spawn (gel, copper or silver coins, torches or ice torches, arrows, a potion, glowsticks), drawn opaque inside as dropped items draw, turning slowly and dropped on death. Squash and stretch are the hopper's. Shots `out/m47_grid.png`: a Blue Slime mid-hop, stretched, its item seen through the gel (`out/m47_a.png`); three slimes, the pale Ice Slime refracted through the Blue (`out/m47_probe.png`). Residual: the renderer's TAA reprojects with camera motion only, so any moving creature, jelly included, leaves a faint one-neighbourhood trail while it moves.
- [x] **M4.8 Particles.**
  - GPU-instanced particles (16k desktop, 4k mobile).
  - An atlas baked with `pg_bake_sprite_atlas`; lit by the light volume.
  - Every effect in §5.5.
  - *Done when:* screenshots show the effects in combat, mining and biomes.
  - *Evidence:* `shaders/vx_vfx.glsl` plus a forward sprite pass in `vx_render.c` after the fog, before TAA. Sprites are instanced billboards (`VxVfxGpu`, 48 bytes, cap `VX_PARTICLE_CAP`: 16384 on desktop, 4096 on wasm) read from a structured buffer and drawn in one call, premultiplied so glows add and smoke covers. The atlas is baked once on the first frame by `pg_bake_sprite_atlas` (512², 4×4: puff, wisp, shard, spark, streak, flash, ring, mote, flame, bolt). A fourth shape, the crisp pixel square with a soft halo, is drawn analytically for the sparkles that S17 shows. Glowing kinds are self-lit; smoke, puffs, droplets and mist are lit like the water by the light volume (sky and coloured block light, dynamic lights, sun where the sky is open). Everything fades softly against a copy of the scene depth. Leaves, snow and gore stay lit pixel-art cards in the g-buffer pass, and block chips and debris stay as boxes. The CPU mote pool grew to the platform cap with per-kind quotas scaled to it. New kinds: smoke, flame, droplet, flash, ring, streak and sparkle. Emitters, against §5.5:
    - block dust in tile colours, and pot sherds;
    - item sparkle: coins and rare items glint in their rarity colour; a pickup trail while items fly in;
    - torch flame, smoke and embers: every torch in a deterministic 36×36 column sweep around the camera (the whole square every 20 frames), with an ice-blue flame on ice torches;
    - footstep puffs in the ground's colour when running;
    - water splashes: droplets and a spray ring on entering water fast;
    - snow, falling leaves, fireflies, spores (only into open air near the camera's height), and underworld embers, white-hot cooling to red;
    - sword arc sparkles, hit sparks, gore chunks, magic trails;
    - fallen-star trail plus a glowing streak;
    - the death burst: a white flash, a ring in the creature's colour, white sparkles and gore.
  - *Shots:* `out/m48_grid.png`, ours against s_52/s_54 (sparkles and sparkle trails), s_57 (streaks) and s_51 (embers): the gold sword arc of pixel sparkles; a zombie's death flash, ring and gore; torch and ice-torch flames in a dark room; the falling star's trail; the cyan spores over the mushroom terraces; embers over the underworld lava; block chips, pot sherds and a glinting coin from mining; fireflies at night.
  - *Not in a screenshot:* footstep puffs (scripted input cannot make the player run) and the splash. The splash fires (`audio: splash` in `out/m48w.log`), but its 0.7–1.1 s life is shorter than a screenshot's stall, so only the foam at the feet is captured (`out/m48_splashB.png`).
  - *Boss dash:* the streak kind is what M5.1's dash will emit. On screen now it is the falling star's streak.
- [x] **M4.9 Guide.**
  - A procedural NPC with idle wander and tip dialogue.
  - *Done when:* a screenshot shows the dialogue.
  - *Evidence:* the Guide is a friendly creature kind (`VX_CR_GUIDE`, behaviour `VX_BEH_TOWNSFOLK` in `vx_creatures.c`). Swords, arrows and aim ignore him; he never despawns and never counts toward the spawn caps. He is placed in the spawn hut whenever he is missing and its floor is loaded.
    - Art: the player's own box puppet (`vx_puppet_body`, split out of `vx_puppet_build`) in the Guide's clothes (brown hair, tan work shirt, brown trousers). It is posed from his stride, with a breath while standing, a wave when the player first comes near, and his head turned toward the player.
    - Behaviour: by day he strolls and stands within 9 blocks of home, stepping up ledges and turning at walls, drops and water. At night he keeps within 2.5 blocks, and when he is out past that and off-screen he goes home, as Terraria's townsfolk do.
    - Talk: right-click, or a tap on touch, along the view ray, unblocked and within 7 blocks. It opens Terraria's chat box at the top of the screen with his name, the line and how to go on. The first talk is a greeting; opening a talk at night gives the night warning; every right-click after that gives the next of 9 tips. Walking away or opening the inventory closes it. His name floats over his head while the player is near.
    - First-night hint: the first nightfall with him in the world puts *The Guide: Night has fallen…* in the event log.
  - *Shots:* `out/m49_grid.png`, ours against s_17 (the town's box-puppet NPCs): the Guide waving at the player in third person, a daytime tip, and the night line with the first-night hint in the event log (`out/m49_day3p.png`, `out/m49_talk2.png`, `out/m49_night.png`). The run logs `out/m49k.log` show the greeting, then the night line after walking away and back; `out/m49h.log` traces him strolling and standing for 22 s on his floor.
  - *Fixed on the way:* a stroll that turned at a wall tested its step-up along the new heading, away from the wall, so it passed on open floor and hopped him a block. The step-up is now tested toward what he bumped, and only while he is still heading that way.
  - *Not done here:* only the Guide exists; the other townsfolk in s_14–s_18 are outside this demo's scope, as §2.1 already records for P1.
- [x] **M4.10 Combat sounds and ambience.**
  - Combat and enemy sounds; ambience by biome and time from the engine ambience generator.
  - *Done when:* a run log shows the sounds firing and the ambience switching.
  - *Evidence:* the sounds are new named one-shots in the bank, built at start-up in `vx_audio.c` (`vx_make_combat_sounds`):
    - a punchy body hit, a wet slime hit and a bony skeleton clack;
    - Terraria's kill sound in place of the hit on the killing blow, with a slime burst for slimes;
    - the bow's twang, an arrow's thunk into blocks (quieter with distance) and the wand's rising zap;
    - a zombie's formant groan every 5–12 s, louder the nearer it is.
  - *Ambience:* one live mono stream mixes five beds, each sample's gain ramping smoothly as the camera's surroundings change:
    - day: the engine's ambience generator (`pg_ambience_render`), giving wind and chirping birds;
    - night: crickets (four insects, each with its own pulse count and period) over the generator's quieter bed;
    - caves: a low drone and drips that plink up in pitch, with an echo off the walls;
    - the underworld: a rolling rumble, lava blorps and crackle;
    - the ocean: surf swelling on a slow, wandering period.
  - The beds follow the game's smoothed depth, underworld and biome weights and the day clock. Rain hushes the birds and crickets; the rain bed stays as it was. Silent beds cost nothing.
  - *Logs:* `out/m410.log` has, in order: ambience day; kill (a zombie); slime hit; bone hit; zombie groan; bow; slime kill; spark cast; ambience night; ambience cave; ambience ocean. `out/m410b.log` has bow, arrow hit and ambience underworld.
  - *Levels:* a temporary per-block probe measured every bed (day 0.04 RMS, night 0.025, cave 0.025, underworld 0.07–0.10, ocean 0.13–0.14). The underworld and the ocean were then scaled down to the others' level; the new levels are computed from those factors, not re-measured.
  - *Not verified:* the sounds were not listened to in this session; the evidence is trigger logs and measured levels.
- [x] **M4.11 Fight postcards.**
  - A scripted night run with spawns, and a combat VFX postcard.
  - *Done when:* both are captured on native and wasm.
  - *Evidence:* two new postcards in the `vx_view` table, P12 and P13 (P11 stays the Eye of Cthulhu shot of M5.1).
    - P12, the night fight: third person at the P1 spot at night. The natural night spawner fills the forest, and three zombies, a demon eye and a slime are spawned 10 blocks out, 1.6 s before the shot, so they are closing in when the player swings.
    - P13, the combat VFX: day, the gold broadsword's swing killing a zombie and a slime while hitting a second zombie.
  - `tools/postcards.sh` gained per-postcard pre-shot commands (`pre_shot`), placed relative to the shot on native and on wasm alike.
  - *Shots:* `out/m411_grid.png`, native against wasm for both, with s_52/s_54 (S17's combat) beside them. `out/postcards/{native,wasm}/P12.png` show the swing's sparkle crescent, three zombies, the demon eye and the slime closing in, the Guide's lit hut, fireflies and the first-night hint. `P13.png` shows the gold crescent, damage numbers, both bodies bursting into gore chunks in their colours, hit sparkles and the hurt zombie's life bar.
  - *Logs:* `out/postcards/native/run_P1{2,3}.log` and `out/postcards/wasm/P1{2,3}.log` hold the hits and kills. The wasm logs show 0 WebGPU validation errors.
  - *Native against wasm:* same spot and framing. Wasm runs at 44–52 fps against native's 200+, so the swing is at a slightly later phase when the shot lands: the arc is fading and fewer chunks are in the air. Every effect is present on both.
  - The S17 dungeon setting itself is M7.18.

### M5. Boss, music and signature
- [x] **M5.1 Eye of Cthulhu.**
  - A procedural model: iris, veins, tendrils, and a phase-2 mouth.
  - Hover, dash and Servant spawning; faster dashes in phase 2.
  - Boss bar, event text, the Suspicious Looking Eye recipe, loot, and a `vx_boss` command.
  - *Done when:* a scripted fight shows both phases and the defeat.
  - *Evidence:* two new creature kinds, the Eye (`VX_BEH_BOSS`) and the Servant (a swooper), in `vx_creatures.c`.
    - *Model:* a voxel eyeball 6 blocks across (2.2× the player's height). It is built from the surface cells of a 13³ grid, drawn as boxes, about 530 per frame. It has a blue iris with a darker rim, a pupil, a self-lit wet glint, red veins wandering from a bloody back, and six whipping tendrils.
    - *Phase 2:* a cone is carved out of the front, giving a mouth whose flesh darkens down the throat and whose gape chomps with the hunt. Tapered fangs ring the lip.
    - *Fight:* Terraria's numbers: 2800 life, 12 defense and 15 contact damage, then 0 defense and 23 damage in phase 2. It takes no knockback, so a blow never stops a dash.
      - Hover: 11 blocks out and 5 up along its current bearing. Phase 1 sends a Servant every 0.8 s (at most 6; 8 life, 12 damage).
      - Volleys of three dashes: 21 blocks/s in phase 1; 27 blocks/s in phase 2, with shorter aims and hovers. Each dash plays a rush-and-growl sound and leaves a wake of glowing streaks, bloody in phase 2.
      - At half life it spins up for 2.4 s. At the peak the iris tears off as debris and blood, with a flash and a roar, and it spins down with its mouth.
      - At dawn, or when the player dies, it rises away and leaves.
    - *Defeat:* the purple event "Eye of Cthulhu has been defeated!", a roar, a 3.6-block flash and ring, 70 blood droplets and 24 radial streaks. Loot: 30–87 Demonite Ore, 5–15 Lesser Healing Potions and about 3 gold in coins. Coin drops now split into gold, silver and copper.
    - *Arrival:* the Suspicious Looking Eye (six lenses at a work bench, crafted with `vx_craft`) summons it at night with "Eye of Cthulhu has awoken!" in purple. By day the item is kept and a toast says it only stirs at night. At each dusk, a player with 200 life and a full iron set is warned, on a one-in-three roll, with "You feel an evil presence watching you..." in green; the Eye arrives 8 s later (2 of 5 test runs rolled it).
    - *HUD:* a boss bar above the hotbar: name, gold-framed red fill and life numbers. Its icon is an eyeball that turns into a maw in phase 2. Event lines can now carry a colour (`vx_event_rgb`).
    - *Commands:* `vx_boss` summons it at any hour; `vx_boss:hurt=<n>` deals a blow; `vx_look:boss=1`.
    - *Test tooling:* `vx_spawn:natural=0` turns off the natural spawner, `vx_give ... equip=1` wears armour, and item names resolve items before blocks.
  - *Shots:* `out/m51_grid.png` holds the dusk arrival, phase 1 with a Servant leaving the pupil (wasm), the transform burst, the phase-2 mouth, a phase-2 dash's streaks, the defeat burst and the loot pile, beside S18's boss frames (s_56, s_57).
  - *Logs:*
    - `out/m51_r1.log`–`out/m51_r3.log`: Servants sent, sword hits through its defense, contact damage, "transforms", "bares its mouth", "killed", the defeat event.
    - `out/m51_loot.log`: 82 ore, 5 potions, 34770 copper.
    - `out/m51_item.log`, `out/m51_craft.log`, `out/m51_craft2.log`: the summon item and its recipe.
    - `out/m51_ev1.log`–`out/m51_ev5.log`: the dusk roll.
    - `out/m51_wasm.log`: the same fight on wasm, with 0 errors.
  - *Not verified:* the roar and dash sounds were not listened to. Scripted runs cut most of the 2800 life with `hurt=`; a full sword-only fight was not played through.
  - *Against S18:* the reference's boss is Terraria's Empress of Light. Its prismatic skybridge fight is P11 (M5.7) and S18 (M7.19).
- [x] **M5.2 Music.**
  - A game-side tracker with synth voices, feeding the mixer's PCM ring.
  - Title, Day, Night, Underground, Underworld and Boss loops, crossfading by context.
  - *Done when:* a run log shows each loop starting in its context.
  - *Evidence:* `src/voxaria/vx_music.c`, a pattern tracker.
    - *Songs:* each is data: tempo, key and scale (major, minor or Phrygian), a chord per bar, a 128-step lead melody, and one-bar bass, arpeggio, kick, snare and hat patterns.
    - *Band:* a lead (square, triangle, detuned saw, horn or struck bell), with a delayed vibrato and its own low-pass; a bass following the chord root, third and fifth; a three-voice saw pad gliding between triads; plucked square arpeggios; a pitch-dropping kick, a noisy snare with a tone, and a short hat. The lead and arpeggio ring on in a three-sixteenth feedback echo.
    - *The six loops:*
      - Title: 84 bpm, C major, a slow horn over the pad.
      - Day: 120 bpm, D major, a bouncy square lead and backbeat.
      - Night: 92 bpm, A minor, a soft triangle, arpeggios, a sparse kick.
      - Underground: 104 bpm, D minor, bell motifs over running arpeggios.
      - Underworld: 96 bpm, E Phrygian, low saw riffs over heavy kicks.
      - Boss: 150 bpm, C minor, saw runs over a driving bass and 16th hats.
    - *Playback:* two players crossfade over 1.6 s. A song still fading out returns where it was; any other starts from its top. A gentle saturation follows, then one live mono stream on the music bus at 0.38.
    - *Context:* the title before play and in the menu; the boss over everything; then the underworld and underground by the smoothed depth; then night or day by the clock. A new context must hold 0.75 s (the boss and title cut in at once), so a depth passing through a band starts no song; this was found when a lava death briefly started the underground song.
  - The engine's default pool of 64 sources was full, so the stream was refused ("audio: source pool full"). The game now sizes the pool from what it creates.
  - *Logs:* `out/m52.log` has, in order: music day, title (menu), day, night, boss (summoned), night (Eye killed), underground, underworld. `out/m52_wasm.log` has day, night and boss on wasm, with 0 errors.
  - *Levels:* a temporary probe measured 0.14–0.27 RMS with peaks of 0.4–0.85 across all six loops, so nothing clips; the probe was removed.
  - *Not verified:* the music was not listened to in this session. Its musical quality is unjudged; the evidence is the switching logs and the measured levels.
- [x] **M5.3 Title screen.**
  - A golden-hour flyover, the logo and the menu.
  - *Done when:* there is a screenshot.
  - *Evidence:* until the first Play, the menu is the title (`vx_title_active`).
    - *Flyover:* the camera orbits the spawn at a radius of 34 blocks, turning at 0.03 rad/s and looking down across it. It stays 8 blocks above every column within 6 blocks of it and of its path a few seconds ahead, and at least 24 above the spawn; it rises fast and settles slowly. The render's clock is held at 0.472, low warm sun and a pink sky, while the world clock runs on. The player's puppet, the HUD and the stats overlay are hidden.
    - *Logo:* as S20: a full-width dark band, "VOXARIA" in cream, bold and thickened with an outline in its own colour, over a dark drop shadow, with a subtitle line below. It scales with the window (48–150 px). Below the band are Play, Settings and New World (since M7.21, one row pinned to the bottom edge).
    - Play (or `vx_play`) leaves the title for good; Esc later opens the ordinary pause menu. The title song plays until then.
  - *Shots:* `out/m53_grid.png`: S20 (s_64), native `out/m53_title.png` and wasm `out/m53_title_wasm.png`. `out/m53_play.png` is the hand-over to play, with the HUD and the third-person camera back.
  - *Against S20:* the band, the logo colour, its weight and its shadow match. The reference flies over a built town; ours flies over the spawn forest and the Guide's hut, because the world has no generated town. The town scene is S20's own step (M7.21).
  - *Logs:* `out/m53.log` and `out/m53_wasm.log`, 0 errors.
- [x] **M5.4 Wings payoff.**
  - Sky-island chests hold wings; flight is tuned.
  - *Done when:* a scripted run opens the chest, equips the wings and flies.
  - *Evidence:*
    - *Loot:* a gold chest standing on sunplate (only the sky huts are built of it, and placed chests are never filled) is a sky chest, and its prize is the Angel Wings, ahead of bars, potions, torches and coins.
    - *Flight:* 1.7 s of lift (Terraria's Angel Wings: 100 ticks). On the wing, flapping or gliding, the player moves 1.3× faster and steers at 7/s against 4.5/s for a plain jump.
    - *Art:* rebuilt as S4's golden feathered wings. Each side has a raised brown leading edge, three orange coverts, and eight broad feathers, orange at the root and yellow at the tip. The feathers fan from straight down at the body to out along the edge at the tip, longest outermost, so the tips scallop. They beat while lifting, hold open in a glide, and fold flat down the back with the fan closed. The item icon is gold to match.
    - *Test tooling:* `vx_look:chest=1` aims at the nearest chest; `vx_chest:all=1` is the chest's Loot All (the button and the command share `vx_chest_loot_all`); `vx_equip:item=<name>` moves an accessory or armour piece from the bag onto the body.
  - *Run (native `out/m54.log`, wasm `out/m54_wasm.log`):* the player starts inside the sky hut at site 2 and the chest is opened with a real interact: "sky chest at 55 204 -44 filled with 5 stacks", "took 1 x Angel Wings", "vx_equip: Angel Wings on". Then 6 wing flaps on a held jump off the island's edge. Wasm had 0 errors.
  - *Shots:* `out/m54_grid.png`: S4 (s_12), the open Gold Chest with the wings, native flight `out/m54_fly.png`, and wasm flight `out/m54_fly_wasm.png`. Also `out/m54_fold.png` (folded on the ground) and `out/m54_glide.png` (falling with the wings folded after the jump is released).
  - *Against S4:* the wing shape, colour and span match. The reference's player is seen flying high above the island; our run's camera follows from behind.
  - *Not verified:* the third-person `vx_look:chest` aim misses by the shoulder offset, so the run opens the chest in first person.
- [x] **M5.5 Terraria view.**
  - A side-on camera that clips at the player's plane and caps the cut with quads.
  - Movement and targeting constrained to the plane, and a toggle key.
  - *Done when:* screenshots show the cross-section on the surface, in a cave and in the underworld.
  - *Evidence:* `C` toggles it (`vx_cut:on=1|0 [cx= cy=]` in scripts). The work lives in the new `vx_cutaway.c`.
    - *Camera:* looks straight down −z from 44 blocks in front of the player's slab with a 40° FOV, framing about 32 blocks of height. It eases after the player. The mouse is unlocked and is the pointer.
    - *Cut:* everything past the plane is discarded in the shared g-buffer and water shaders, and the CPU cull skips those columns. The mesher always emits the +z face of every block on the slab, so the cut is capped by the block's own textured face. Liquids on the slab are capped too. Toggling or moving the slab remeshes only that chunk row, as urgent work.
    - *Light:* cut faces are lit as Terraria lights tiles. A 128×256 map follows the view: each open cell takes its voxel and held light, then two chamfer sweeps seep light into solid tiles, keeping 0.7 per tile. The deferred pass reads it for faces on the plane, giving full daylight scaled by the seeped sky light, with no sun shading or shadow. The map is refreshed on edits, on held-light changes, when the view moves more than 16 blocks, and every 0.5 s.
    - *Atmosphere:* the fog, light-shaft and underground-fog marches start at the plane, because the 44 blocks of air in front of it are not there. Without this, a grey veil covered the whole cut.
    - *Play:* the player is held to the slab. A/D (or the touch stick's sideways push) walk it, S drops through platforms, and facing follows the pointer while the mouse is down. The target is the tile under the pointer within reach. An empty tile with a block held builds against a solid neighbour (below, then the sides, then above). Swings, shots and the hook aim from the eye at the pointer.
  - *Shots:* `out/m55_grid.png` holds the surface (sunlit dirt fading to black), a cave lit by the held torch, the underworld with the lava sea and a ruined tower cut open, a tile mined in the plane, and the surface on wasm.
  - *Logs:* `out/m55_mine.log` shows "picked up 1 x Dirt Block" from the tile under the pointer, then "placed Dirt Block at 29 149 15" back into it. `out/m55_wasm.log` has the cutaway on wasm (the only errors are the page's usual 404).
  - *Not verified:* real mouse play was not tried; the pointer was scripted. Single-threaded wasm was not run for this step.
  - *Residual:* shadows on the scene behind the plane use the normal cascades, which are coarser at 44 blocks.
  - *Also fixed:* the music stream voice no longer asks to loop, which logged "loop ignored on a stream voice" twice at start.
- [x] **M5.6 Juice.**
  - Screen shake, plus feedback timing for hits, pickups and kills.
  - *Done when:* the combat and boss runs show it.
  - *Evidence:* the new `vx_juice.c`.
    - *Shake:* trauma based. Events add trauma, it bleeds away at 1.6 per second, and the rendered view shakes by its square with smooth three-sine noise per axis: up to 0.28 blocks of movement (0.06 in first person, so the eye never enters a wall) and 0.03 rad of yaw and pitch. It moves only the drawn view, so aim, targeting and the cutaway's pointer are untouched. "Screen shake" in Settings → Game turns it off.
    - *Hit-stop:* the simulation runs at 6% for a few frames. Bodies, swings, particles and the puppet hold still; the clock, weather, audio and interface run on. A melee hit holds 35 ms, a crit 55 ms, a kill 75 ms, the player being hurt 40 ms, the Eye's transform 120 ms and its defeat 200 ms.
    - *Shake amounts:* being hurt 0.25 + damage/60 (at most 0.6), crit 0.1, kill 0.16, an Eye dash within 16 blocks 0.12 (0.2 in phase 2), the transform roar 0.6, the defeat 0.8, and thunder up to 0.35 for near strikes.
    - *Pickups:* the existing world text, pickup sound and drop magnet already time pickups; unchanged.
  - *Shots:* `out/m56_grid.png` holds the transform roar, the defeat burst frozen in its hit-stop, and the same defeat on wasm.
  - *Logs:* `out/m56.log` (native) and `out/m56_wasm.log` show each event's "juice:" line beside its cause: zombie hits on the player, the zombie kill, the transform, a phase-2 dash, the Eye's defeat.
  - *Not verified:* the feel was judged from logs and stills, not played by hand.
  - *Also fixed:* the ambience stream voice no longer asks to loop, the second source of "loop ignored on a stream voice".
- [x] **M5.7 Journey run.**
  - Scripted runs cover every beat of the §3 journey.
  - *Done when:* there are screenshots of each beat, and P11 is captured on native and wasm.
  - *Evidence:* `out/m57_beats.png`, every beat from this session's build, and `out/m57_postcards.png` with all 13 postcards recaptured in one native run by `tools/postcards.sh`.
    - *1, spawn:* P1, the noon forest by the Guide's hut ("the Guide is at home" in the log; he greets at dusk with "Night has fallen…").
    - *2, chop, fight, craft:* P13 (a sword swing through zombies and a slime), and `out/m57_craft.png`. That run (`out/m57_craft.log`) crafts the chain: Work Bench, Torches, Furnace, 25 Iron Bars, Iron Anvil, Iron Pickaxe and Grappling Hook. The inventory reads "Near: Work Bench, Furnace, Iron Anvil", with a Cloud in a Bottle worn.
    - *3, dusk and night:* P2 (dusk sky), P12 (zombies, a demon eye and a slime at night), P6 (a falling star).
    - *4, underground:* P3 (a torch-lit cave with ores and pots), P4 (the glowing mushroom biome over an underground lake).
    - *5, smelt and gear:* the crafting run above.
    - *6, underworld:* P5, the lava sea and the ruined obsidian tower.
    - *7, sky island:* P7; the wings in its chest are M5.4's `out/m54_grid.png`.
    - *8, the Eye on a skybridge:* P11, new: an 8-wide wood-platform skybridge 35 blocks above the forest at nightfall, with the Eye hovering over it beside two sky islands. Captured on native (`out/postcards/native/P11.png`) and wasm (`out/p11_wasm.png`). `tools/postcards.sh` builds the bridge, armour and summon before the shot (`VX_SETTLE=12`).
  - *Fixed on the way:*
    - *Teleports counted as falls:* the cave, site, find and above placements of `vx_view` moved the player without resetting the fall peak, so a batch of postcards killed the player with "You fell to your death" (P3, P4). All four now reset it.
    - *The Eye filled the screen:* it hovered out along its bearing even when that lay behind the camera. Its hover bearing is now pulled round in front of the view. Creature boxes also carry a new near-fade flag: within 1.2–3.5 blocks of the camera they dissolve in an ordered dither, so a dash past the camera no longer fills it. The player's own puppet never fades.
    - *The red hurt wash:* a 27% full-screen red rectangle on every hit, which no Terraria frame has. It now shows only in first person, where no body shows the hit; elsewhere the puppet's hurt tint and the shake carry it.
  - *Not verified:* a continuous 30-minute playthrough was not played; the beats are separate scripted runs. Scripted `key_down:E` and `key_down:Space` produced no inventory toggle or jump in this run, so the inventory was opened with `vx_mode:inventory`. The cause was not chased (input plumbing, per the user's direction); `Mouse Left` presses do arrive.

### M6. Ship
- [x] **M6.1 Presets.**
  - Low, Medium, High and Ultra retuned for the stylized frame, with auto-downgrade thresholds.
  - The settings UI shows the new options.
  - *Done when:* there are screenshots of one view at each preset and of the settings screen.
  - *Evidence:* `out/m61_grid.png` shows P1 at Low, Medium, High and Ultra; `out/m61_settings.png` shows the settings screen.
    - *What each preset keeps:* the stylized frame's defining passes (outlines, tile framing, bloom, TAA and the coloured light volume) are on at every preset. The presets step only the costly extras: render distance 6/8/12/16 chunks, render scale 0.75/0.85/1/1, sun shadows off/1024/2048/3072 over 64/96/128/192 m, volumetric light from High, fancy clouds from High, SSAO and soft shadows at Ultra only.
    - *In the shots:* Low reads flatter under the canopy, with no sun shadows; Medium to Ultra differ mostly in distance and fine shadow detail.
    - *Auto-downgrade:* while auto quality is on, the median of 150 idle frames is checked; slower than 38 fps steps down one preset, with a toast, until Low. The first good check stops it.
    - *New options in the UI:* Lighting (Color, White, Retro, Trippy), Weather, Cloud coverage, and from M5.6 Screen shake, all shown.
  - *Not done:* the preset budgets were not re-measured. They are the pre-Voxaria values, kept because re-budgeting needs per-platform frame timing, and the goal rules out perf benchmarks.
- [x] **M6.2 Touch pass.**
  - Every new action works with touch.
  - *Done when:* a scripted touch run covers move, jump, mine, place, attack, grapple and inventory.
  - *Evidence:* one native touch script (`out/m62.log`) logs, in order: the jump button, a stick drag, the new "Side" pill turning the cutaway on, a held finger mining with the pickaxe ("picked up 1 x Dirt Block"), a tap placing dirt back ("placed Dirt Block at 28 148 15"), two sword taps on a zombie ("Zombie hit by Copper Shortsword" twice), the pill turning the cutaway off, the Hook button ("hook fired") and the inventory button. Shots: `out/m62_grid.png` (3D play with the touch HUD, the side view with the player swinging at the zombie, the open inventory, and the same open inventory on wasm, `out/m62_wasm.png`). The wasm run (`out/m62_wasm.log`, 0 errors) shows the hook firing and latching and the cutaway turning on by touch.
    - *Side view by touch:* a "Side" pill next to Map and Menu toggles the cutaway. In the side view a finger picks the tile on the slab under it (`vx_cut_pick`), and the stick, jump and hook buttons work. The touch input is now merged before the cutaway's early return.
    - *Slab hold:* `vx_cut_hold` pins the player to the slab after combat and creature pushes as well, which stopped a footstep loop from pushes along z.
    - *Fixed on the way:* three touch bugs that lost or stuck taps, two in the engine (engine change 4) and one in the game: a track whose finger lifted on a frame without touch play is now dropped.
  - *Not verified:* no real phone was used; the run is the desktop build with scripted touches. The wasm log does not show the `player:` lines, so the jump on wasm is unconfirmed by the log there.
- [x] **M6.3 Publish.**
  - The multi-threaded and single-threaded publish builds boot to the world in the browser harness with no errors.
  - The download size is measured and reported against the 500 KB target.
  - *Done when:* both boots pass and the sizes are printed.
  - *Evidence:* `hz\hzbuild wasm --publish` and `hz\hzbuild wasm ios --publish` both exit 0 with 0 error lines (`out/pub_mt.log`, `out/pub_st.log`). Each bundle was served by a local static server with the published `_headers` rules (COOP/COEP, brotli for `.br`) and booted in Playwright Chromium, which then clicked Play. `out/m63_grid.png` shows the multi-threaded build in the world (WebGPU, 16 threads, 60 FPS), the single-threaded build in the world (1 thread, 60 FPS), and the single-threaded build on a 844×390 phone viewport at 2× with touch, on the title over the live world. The only console error in every run is the missing `favicon.ico`.
    - *Sizes, from the server's request log (the runtime fetches until in play):*
      - Code, multi-threaded: `game.wasm.br` 302.6 KB, plus JS 161.5 KB raw (about 45 KB brotli) and `index.html` 3.9 KB, about **352 KB** brotli. Under the 500 KB target.
      - Code, single-threaded: `game.wasm.br` 299.0 KB, plus JS 97.6 KB raw (about 28 KB brotli), about **331 KB** brotli. Under the target.
      - Boot assets, both builds: the UI font pages (Regular, SemiBold and Bold page 0) 1032 KB, five font indices 289 KB, the asset catalog 83 KB and the family 0.4 KB, about **1.40 MB**. This is engine UI text (Inter MTSDF pages, about 4 MB raw each) and is over the target on its own.
      - Total to the world: about 1.75 MB (multi-threaded) and 1.73 MB (single-threaded), with JS counted at its brotli size.
    - *Fixed on the way (engine change 5):* before this step the boot fetched 15.75 MB, because every font page and index loaded raw: `asset_load_cb` dropped the type name, so the publish compression policy never applied, the font index type had no compressed siblings, and the font family's dependencies were untyped. The published `_headers` also lacked a brotli rule for `cooked/*.br`, so on the real host every compressed asset would have reached the engine still encoded.
    - *Game copy:* the pause menu and loading screen still said "Shaders Edition — a voxel sandbox" and listed Minecraft controls (break/place block, pick block, block inventory, fly). Both now use the title's line and the real bindings (`out/m63_menu.png`). The bold key column uses only ASCII, because its en dash alone pulled in a 328 KB glyph page.
  - *Not verified:* a deploy to the real CDN (the `cooked/*.br` header rule mid-path splat is per Cloudflare's single-splat rule but untested there); real phones. The `?commands=` parameter is ignored by publish builds, so the boot runs used natural spawning.
- [x] **M6.4 Final postcards.**
  - P1–P11 on native and wasm.
  - *Done when:* all are captured and compared in writing with their references.
  - *Evidence:* `out/postcards/native/P1–P11.png` (`tools/postcards.sh`, logs `out/m64_native_a.log`, `out/m64_native_b.log`: 0 error lines) and `out/postcards/wasm/P1–P11.png` (per-shot logs in `out/postcards/wasm/`, 0 errors besides the harness 404). Side by side, reference / D3D12 / WebGPU: `out/m64/a.png` (P1–P3), `out/m64/b.png` (P4–P6), `out/m64/c.png` (P7–P9), `out/m64/d.png` (P10–P11).
  - *Native against wasm:* the same image on every postcard: palette, outlines, fog, glow and HUD match. The only differences are the clock (wasm shots land a few seconds later, so the P2 and P6 Guide toast and the P10 framing differ) and, on P5, a green strip of surface terrain at the far left of the wasm underworld horizon that the native shot hides in darkness.
  - *Against the references, honestly:*
    - P1 (ref 14.0 s, S5 town street): the look is the video's (chunky clouds, outlined bark, mottled canopy, saturated grass), but the content is not: one hut in a forest against a street of two-storey wood and brick houses on cobble with lamps, signs and NPCs. Owner: M7.6.
    - P2 (ref 59.5 s, S19): the dusk sky, warm windows and fireflies read right; the town, pedestals and lit street are missing as in P1. Owner: M7.20.
    - P3 (ref 45.5 s, S15): torch-lit cave with ore glints and lit stone; the reference looks down a wide stepped shaft through grass, dirt and stone layers with the player falling. Owner: M7.16.
    - P4 (ref 42.0 s, S14): closest match: giant blue mushrooms with emissive stripes, spores, glowing small mushrooms. Our stems are thinner and fewer, the floor is lit blue mushroom grass where the reference has dark mud, and the caps are smaller. Owner: M7.15.
    - P5 (ref 49.0 s, S16): lava lake, ash ceiling, hellstone and ember glow are there; the reference's obsidian ruin with glowing red seams is a dark brick heap in ours, and its floor is ash with glowing cracks rather than lava to the horizon. Owner: M7.17.
    - P6 (ref 55.5 s, S18 dusk): night forest with moon and stars; the reference is dusk with god rays. Its scene is the boss fight, covered by P11. Owner: M7.19.
    - P7 (ref 10.5 s, S4): sky island with grass top, house and tree over the world; the reference's island has a white cloud base and a brick house and the player flies with golden wings; ours has a stone base and the player is not in frame. Owner: M7.5.
    - P8 (ref 0.5 s, S1): a beach and ocean at golden hour from low altitude; the reference is a high overview of an island with a giant sandstone tower and dune lines. Owner: M7.2.
    - P9 (ref 33.0 s, S11): snow biome at night with aurora; the reference is a daytime crystal mountain with boreal pines and a crevasse. Owner: M7.12.
    - P10 (ref 36.0 s, S12): jungle canopy and a sky island; the reference's stepped mossy-stone pyramid mountain is missing. Owner: M7.13.
    - P11 (refs 55.5 s and 53.0 s, S18): the Eye of Cthulhu over a skybridge at night with the boss bar; the reference is a rainbow-winged fairy boss with prismatic fans at sunset. Owner: M7.19.

### M7. Scenario parity (every scenario in §2.1)
Each step: capture our version from the matching camera, build `out/cmp_<S>.png` beside its frames, write the remaining differences, fix them, repeat until none of substance remain. Native and wasm.
- [x] **M7.0 Look parity measurements.** Measure the §2.1 list on the frames and on ours; one table of numbers; each gap becomes a fix in M7.1.
  - Measured on the frames (1280-wide, crop means) and on ours (P1 and matching views), before and after M7.1:

    | Item | Reference | Ours before | Ours after M7.1 |
    |---|---|---|---|
    | Texture texels per block face | no visible texel grid; about 4 features per block (4 plank rows, 4 brick courses, 3–4 cobbles across) | 8 art pixels with jitter on a 16-texel tile; per-block frame | 64 texels per block, smooth painted features at the reference's counts, world-continuous across blocks |
    | Block grid on same-material walls | none | framed tile per block | none: tiles sampled in world space over 2×2 blocks |
    | Sunlit grass top | (79,175,83), luminance std 21, sat 0.55 | (81,177,43), sat 0.76, std 4 | (68,167,89), sat 0.59, std 10 near / flecks visible |
    | Grass tufts | sparse, about 1 tuft per 5 blocks, tall thin blades | 45 % of grass blocks, dense clutter | 19 % tufts, about 1 % flowers outside patches |
    | Leaf canopy | opaque, (31,63,29), mottled, no lines | cutout, noisy holes | opaque occluders, dark mottled green, no line work |
    | Clear sky at 15–30° | (55,105,193), sat 0.73 | (98,139,186) | (77,107,182); zenith (2,69,185) |
    | Clouds | separate chunky puffs 10–30 blocks, blue between, lavender-grey undersides | few, 140-block sheets | separate puffs about 30 blocks across on 6×4×6-block cells |
    | Outlines | about 1.5–2 px at 720p, dark hue of the surface | none | 2 px at 1080p, surface colour × 0.26–0.32 |
    | Distance haze | heavy blue | light | forest fog density doubled; sky above the horizon band no longer fogged |
- [x] **M7.1 Base look.** Textures (feature size, shading, line work), silhouette outlines (M3.13), grass and foliage density, palette and light, all matched to the M7.0 numbers.
  - *Evidence:* `out/cmp/base_v1.png` (before) → `out/cmp/base_v6.png` (after) against s_21 and s_12; `out/tex_sheet.png` (every tile layer); `out/cmp/base_wasm.png` (P1 on D3D12 beside WebGPU: same look). Leaves as occluders cut P1 GPU time from 6.7 ms to 1.9 ms. Remaining differences belong to scenario steps: tree shapes and town content (M7.4–M7.10), the player's armour and wings (M5.4, M7.6).
- [x] **M7.2 Port S1.** See §2.1 row S1.
  - *Evidence:* `out/cmp_S1.png`: s_01 beside our overview, s_03 beside the close butte view, plus the WebGPU multi-threaded and single-threaded captures of the overview (same look). Sources are in `out/m72/S1/`. Native overview: 490K triangles and about 2.2 ms GPU on D3D12; wasm MT 58 fps (vsync), wasm ST 52 fps.
  - What it took: a far-field LOD (the whole ±384 island meshed at 2×2 blocks beyond render distance), an open-sea plane out to the mountain ring, desert landmarks (butte, pillars, small butte, stepped pyramid, two oases with palms) and the red-capped mesa block. Also pale contour sand, darker cacti, isles moved to the coast ring, taller voxel puffs on a column DDA, the mountain ring restored behind the sea, and blue desert haze.
  - Remaining differences: the reference haze over the land is still denser and bluer; the reference cacti are smaller and denser. On the single-threaded build the far field takes up to two minutes to fill in (it is complete at 120 s).
- [x] **M7.3 Port S2.** See §2.1 row S2.
  - *Evidence:* `out/cmp_S2.png`: s_04–s_06 above our biome-mix overview, a closer view of the town, and the WebGPU multi-threaded and single-threaded captures of the overview (same look). Sources are in `out/m73/S2/`. Native overview: about 2.0 ms GPU on D3D12; wasm MT 55 fps.
  - What it took: a Hallow biome (cyan grass, pale pearlsand blots, pearlwood trees with gold and violet clumps, cyan tufts) beside the corruption, snow and desert. Also a generated town on a damped plateau: wood, red-brick, cottage and dark keep houses with gable and crow-stepped roofs, cobble streets, a copper greenhouse frame, footings down to the rolling ground, and trees among the houses. Grass side fringes are deeper, so one-block terraces read as green bands.
  - Remaining differences: the reference town is more spread out and has more trees between the houses; its keep has a black roof. The greenhouse in s_05 is larger and nearer the Hallow. The town's interiors, banners and plaza are the S5–S9 steps.
- [x] **M7.4 Port S3.** See §2.1 row S3.
  - *Evidence:* `out/cmp_S3.png`: s_08–s_10 above our canopy view toward the sea, the terraced ridge (s_09's framing), and the WebGPU multi-threaded and single-threaded captures of the ridge (same look). Sources are in `out/m74/S3/`. Native: about 2.2 ms GPU on D3D12.
  - What it took: rich mahogany rebuilt as tall bare dark trunks (14–28 blocks, 1×1 or 2×2) under broad flat two-layer canopies with lower side tiers and vines, so the jungle reads as a canopy sea out to the ocean. Trees now reach 8 blocks past a chunk (trunks outside the scratch margin sample their own column), so wide canopies never seam. Jungle landmarks: two slate ridges stepped into six-block grassy ledges and two grass-capped slate spires, all standing above the canopy.
  - Remaining differences: s_07–s_08 show purple corruption trees mixed into the jungle canopy; ours keeps the corruption in its own sector (seen in S2). The reference canopy leaves are a little more yellow on top.
- [x] **M7.5 Port S4.** See §2.1 row S4.
  - *Evidence:* `out/cmp_S4.png`: s_11–s_13 above our third-person shot of the winged player flying beside a sky island, a still of the island alone, the ring of islands round the whole world, and the WebGPU multi-threaded and single-threaded captures of the flight shot (same look). Sources are in `out/m75/S4/`. Native: about 1.9 ms GPU on D3D12.
  - What it took: sky islands rebuilt as in the reference: a deep round bowl of white cloud with a lip, a flat meadow top with dense grass, a small house of mahogany walls in a dark log frame with paned windows (gold chest on a sunplate floor inside), a tall tree with a round crown and branch clumps, and a small tree. The pond and waterfall are gone. Cloud blocks carry a 0.3 self-light, so their shaded sides stay white. Far-field sides of a floating run show its base block, so distant islands keep white undersides.
  - Remaining differences: the reference island floats over the Hallow and the town, while ours stay on the coast ring (decision 156). The reference player wears gold armour (M7.6). The reference grass top reads as taller tufts.
- [x] **M7.6 Port S5.** See §2.1 row S5.
  - *Evidence:* `out/cmp_S5.png`: s_14–s_17 above our third-person shot down the main street from behind the gold-armoured winged player, a front shot with the Mechanic and Clothier at their stalls, and the WebGPU multi-threaded and single-threaded captures of the street shot (same look). Sources are in `out/m76/S5/`. Native: about 2.0 ms GPU on D3D12.
  - What it took: the town now has townsfolk. Besides the Guide there are four keepers, each at a market table in front of its house: the Merchant (white beard, brown coat), the Clothier (top hat, red coat), the Dryad (green hood and robe) and the Mechanic (ginger hair, red top, yellow trousers, blue shoes). Each faces the road and has its own lines. Houses get a real door and a wall torch either side of it. These are placed through per-chunk generation states, so furniture and torches keep their rotation and wall side. Worn armour now shows on the puppet: helmet, chest plate with pauldrons and gauntlets, greaves. Gold armour is new (helmet, chainmail, greaves, crafted from gold bars); the gold helmet carries the reference's crown of glowing rays. Wings at rest stay spread wide at shoulder height with eleven drooping feathers, as the reference carries them on the ground.
  - Remaining differences: the reference's "MARKET" signboard and the potions on the tables are not built. The crystal pedestals by the road belong to S9 (M7.10). The reference street ends at the dark keep; our keep sits at the cross street, behind this camera. The reference street is crowded with a dozen townsfolk; ours has five.
- [x] **M7.7 Port S6.** See §2.1 row S6.
  - *Evidence:* `out/cmp_S6.png`: s_18–s_20 above our first-person view from the armoury door (the four stands, plaques, torches, the smith behind his table), a corner view across to the anvil, and the WebGPU multi-threaded and single-threaded captures of the door view (same look). Sources are in `out/m77/S6/`. Native: about 2.1 ms GPU on D3D12.
  - What it took: the brick house east of the main street (8..20, -44..-33) is now the armoury. Its ground floor has a grey brick lining under the plank ceiling, two torches on the back wall, a table with two chairs, and an iron anvil. Four armour sets stand on wooden stands along the back wall, left to right: sun (gold-orange, with the crown of rays and gold wings, holding a broadsword), vortex (teal, bow), nebula (magenta, pink wings) and stardust (navy, ice wings, wand). Each has a closed visor with glowing eye slits and a glowing chest emblem. Five plaques hold weapons on the walls: the new Enchanted Sword, Night's Edge and Ice Blade, plus the gold and silver broadswords. The new blades also drop from chests. The Demolitionist, a red-bearded dwarf in a miner's helmet, keeps the table. Keepers now only step straight back to their spot, so they no longer drift.
  - Remaining differences: the reference room is wider, so its camera stands further back. The reference sets carry glowing trim lines and a spear on the sun set, and the reference wall pieces include a rainbow sword and a gun. There is no bottle on the table.
- [x] **M7.8 Port S7.** See §2.1 row S7.
  - *Evidence:* `out/cmp_S7.png`: s_21–s_24 above our third-person view along the banner wall from the grass (the reference framing), a lower view down the walk, and the WebGPU multi-threaded and single-threaded captures of the first view (same look). Sources are in `out/m78/S7/`. Native: about 2.3 ms GPU on D3D12.
  - What it took: the banner wall is a 56-block town lot east of the main street (x 35..42, z 6..62). It is grey brick with three rows of banners between red brick bands, under a red brick cap that overhangs the walk. A cobble walk runs at its foot, and a planked pillar on a grey base stands every eight blocks with a torch angled out from each side. Banners are a new cube block whose state names the face that carries the banner and one of 16 painted layers. The mesher puts that layer on that face and grey brick on the rest. Each layer holds two banners side by side (the block column picks one), so the wall shows 32 designs. Each design has a rod, a hemmed cloth in one of 16 colours with a notched foot, and a mirrored 7×8 monster pictogram with eyes. The brick under the banners is the plain wall's own painting, so the stones run on unbroken.
  - Remaining differences: the reference pictograms are recognisable Terraria monsters (slimes, skulls, eyes, worms), while ours are generated creatures. The reference red bands read as slanted ledges; ours are flush. The raised cobble plateau across the grass belongs to S9.
- [x] **M7.9 Port S8.** See §2.1 row S8.
  - *Evidence:* `out/cmp_S8.png`: s_25 and s_26 above our elevated shot down the furniture museum, a close shot of the sets, and the WebGPU multi-threaded and single-threaded captures of the elevated shot (same look). Sources are in `out/m79/S8/`. Native: about 2.5 ms GPU on D3D12.
  - What it took: six new furniture pieces (bed, bookcase, sofa, lamp, grandfather clock, piano) join the chair, table and door. A showpiece bit in the furniture state swaps the plain wood for one of sixteen furniture materials (ebon, pearl, mahogany, boreal, frozen, crystal, pumpkin, golden, bone and others), each with its own wood and cloth, picked per set so a set is one material. The museum is a town lot west of the main street: a levelled cobble pad with five double rows of eight-block sets placed back to back, three-wide aisles between them, and a stone post with a torch closing every set.
  - Remaining differences: the reference pieces are more varied (bathtubs, dressers, sinks, candelabras, statues), and its posts carry wooden name signs. Its museum sits in a jungle clearing with tall trees; ours sits beside the town between the meadow and the hallow.
- [x] **M7.10 Port S9.** See §2.1 row S9.
  - *Evidence:* `out/cmp_S9.png`: s_27–s_29 above our third-person shot from behind the gold winged player on the marble walk, an elevated overview of the avenue, and the WebGPU multi-threaded and single-threaded captures of the walk shot (same look). Sources are in `out/m710/S9/`. Native: about 2.5 ms GPU on D3D12.
  - What it took: three new blocks. Marble paving. The pylon: a carved stone base with glowing seams and four corner pins, over which the entity pass floats a gem stood on its point, spinning and bobbing, a translucent faceted shell round a self-lit core, with shards on the pins and twinkles circling it; the gem's colour (eight) is the block's state. The signpost: a post and board whose faces show a word lettered in pixel capitals (sixteen words, chosen by the block's state). The avenue is a town lot on the cross street east of the main street, beside the greenhouse: a signpost lawn, a marble walk, a cobble road, the marble strip of twelve pylons between grey brick borders, a cobble verge with brick torch pillars, and pearlwood trees on hallowed grass. Sandstone pillars with a torch on each side mark the walk, and a brick house closes the far end.
  - Remaining differences: the reference cottage beside the greenhouse has a stepped plank roof; ours is the wood house up the street. The reference pearlwood canopy is a single tall crown; ours has a gold crown with two violet clumps.
- [x] **M7.11 Port S10.** See §2.1 row S10.
  - *Evidence:* `out/cmp_S10.png`: s_30–s_32 above our view of the great butte from the desert to its southwest (`vx_view:landmark=0 dist=75 up=38 around=-0.5 fly=1 time=0.3 pitch=-0.35`), on D3D12 and on WebGPU multi-threaded and single-threaded (same look). Sources are in `out/m711/S10/`. S1 was recaptured on all three backends after the change (`out/cmp_S1.png`).
  - What it took: the great butte is now two drums of about equal height, the lower one wider. Each drum has a red rim course under a tan cap, and the walls are a new khaki sandstone block with block-sized red-brown hollows. Sand tops are cream. The sand step faces are a darker ochre with a dark lip under each block, so the dune terraces read as contour lines. Sand banks up against the foot of every butte and pillar. Cacti are shorter and about three times as dense.
  - Remaining differences: from far off, the reference desert is paler and bluer than ours, because the reference lays a strong blue haze over the whole island. The reference walls are smoother between hollows; ours show the vertical seams of the drum's block steps. The reference's purple chest-like props on the sand are not built.
- [x] **M7.12 Port S11.** See §2.1 row S11.
  - *Evidence:* `out/cmp_S11.png`: s_33–s_35 above our view of the snow peak from across the crevasse (`vx_view:x=-56 y=192 z=-162 yaw=-0.98 pitch=-0.3 fly=1 time=0.3 rain=0`), on D3D12 and on WebGPU multi-threaded and single-threaded (same look). Sources are in `out/m712/S11/`.
  - What it took: two snow landmarks. The peak is a steep four-faced cone 60 blocks tall, built block by block as a mosaic of stone, slate and blue ice, with snow down its gullies and a flank of pink ice. The crevasse is a long bent crack, pinched at its ends, 16 blocks deep and walled in a new pink ice block under a snow rim. Boreal pines are slimmer, taller and darker, and half as dense. Ice is a deeper blue.
  - Remaining differences: our crevasse shows only as a pink notch at the bottom of this framing; the reference trench runs right across the foreground. The reference peak's faces are flatter and its mosaic is finer. Our clouds are blockier, and ambient snowflake sprites show where the reference has none.
- [x] **M7.13 Port S12.** See §2.1 row S12.
  - *Evidence:* `out/cmp_S12.png`: s_36–s_38 above our view of the jungle mountain (`vx_view:landmark=7 dist=58 up=50 around=-0.4 fly=1 time=0.3 rain=0 pitch=-0.45`), on D3D12 and on WebGPU multi-threaded and single-threaded (same look). Sources are in `out/m713/S12/`.
  - What it took: the jungle terrace landmark is now a steep round mountain, 60 blocks tall on a 36-block radius, stepped into four-block slate courses that each carry a narrow jungle-grass ledge, with a gentle wobble to the rings. Slate is a lighter blue-grey, and the mahogany canopy is thinner so its trunks show.
  - Remaining differences: the reference courses bulge outward with grass spilling over their lips; ours are straight-sided. The reference shows a waterfall pond at the mountain's foot. Our canopy reads flatter and brighter.
- [x] **M7.14 Port S13.** See §2.1 row S13.
  - *Evidence:* `out/cmp_S13.png`: s_39–s_41 above our view across the shimmer lake (`vx_view:yaw=-1.5708 pitch=0.12 site=7 fly=1 time=0.3 rain=0`), on D3D12 and on WebGPU multi-threaded and single-threaded (same look). Sources are in `out/m714/S13/`.
  - What it took: a new liquid, shimmer, and a new stone, aetherium. Shimmer is drawn by the liquid pass as slow violet, lilac and blue swirls that glow on their own, with a pale rim at the shore and twinkling pixel sparkles; it gives off lavender block light. Aetherium is pale lilac stone cracked into big rounded cells with dark violet seams. The cave is one pocket under the central forest, on the bearing farthest from the mushroom pockets: a 46-block bowl of shimmer rising to a beach, under a dome hung with rounded lumps. Only the shimmer lights it, so the lump bottoms glow pale and the dome and far wall fall into black. `vx_view:site=7` stands over its west edge. The creature, loot and falling-star checks now test for any liquid, not a fixed list.
  - Remaining differences: the reference lumps are smooth, rounded and lit right up; ours are stepped voxels and darker at their tops. The reference beach is a broad pale rim at the camera; ours is narrow and far off. The reference lake is a deeper, redder violet.
- [x] **M7.15 Port S14.** See §2.1 row S14.
  - *Evidence:* `out/cmp_S14.png`: s_42–s_44 above our view through a mushroom pocket (`vx_view:x=-66 y=84 z=62 yaw=2.1 pitch=-0.3 fly=1 time=0.3 rain=0`), on D3D12 and on WebGPU multi-threaded and single-threaded (same look), plus a third-person shot of the gold-winged player under the caps (`ours_native_player.png`, taken before the stem-flare tweak). Sources are in `out/m715/S14/`.
  - What it took: giant mushrooms are now placed per site across chunks. The site sets the cap's height, and each column runs the stem down to the floor, so a mushroom spanning chunks is built the same everywhere. Caps reach 4–7.5 blocks in radius on stems 3–5 wide, which flare toward their feet. Mushroom grass is dark mycelium strewn with glowing spores. The pocket's block light is a neutral cool white, so gold armour reads gold and not green. Pots are placed before the mushrooms and never sit on a cap.
  - Remaining differences: in our framing the caps crowd the view; the reference shows long bare stems rising into caps far overhead. The reference cavern beyond is a black void; ours shows its mud walls. In the reference the flying player sits mid-frame between the stems.
- [x] **M7.16 Port S15.** See §2.1 row S15.
  - *Evidence:* `out/cmp_S15.png`: s_45–s_48 above our fall down a dug shaft. In the script, `vx_shaft:r=5 bottom=30` is dug under the player at (40.5, 40.5), then `vx_view:x=40.5 y=150 z=40.5 yaw=0.3 pitch=-1.45 fly=0 cam=3 dist=7` drops the player in; the shot is 1.2 s into the fall. Captured on D3D12 and on WebGPU multi-threaded and single-threaded (same look); native also has a later frame, past a dirt layer. Sources are in `out/m716/S15/`.
  - What it took: a `vx_shaft` command digs a wide, ragged shaft as a player digging to hell would. Each layer's radius wobbles with noise and flares out near the surface, so the walls step. Stone is now warm grey, which was measured against s_47. Water pockets, dirt layers and the lava glow at the bottom come from the world as generated.
  - Remaining differences: the reference walls are smoother and more cratered, and they have torches on them. The reference has motion blur. Its player is larger in frame, and the camera is closer to the falling player.
- [x] **M7.17 Port S16.** See §2.1 row S16.
  - *Evidence:* `out/cmp_S16.png`: s_49–s_51 above our underworld by a ruined tower (`vx_view:x=-43 y=21 z=-30 yaw=0 pitch=-0.15 fly=1 cam=3 dist=6`), on D3D12 and on WebGPU multi-threaded and single-threaded (same look). Sources are in `out/m717/S16/`.
  - What it took:
    - The towers are dark purple obsidian brick inside a grid of solid red-hot hellstone brick: bands every five blocks, posts at the corners and middles. The obsidian crumbles before the frame, so far ruins read as red grids in the haze, as in the reference.
    - The ash floor is a flat mosaic of tan ash and dark purple hellstone, with small clusters of glowing cracks. Lava rivers wind across it flush with its top. The lava sea now covers far less of the floor, and there are about 2.5 times fewer lava falls.
    - The light is a warm under-glow that keeps some green and blue, so the purple and tan read through, under a dense dark-red haze. The far island and the sea are no longer drawn from the underworld; before, they showed through the haze as a pale band.
  - Remaining differences: the reference towers are wider and stepped (a wider lower block). The reference lava is finer and brighter; ours shows blocky blotches up close. Our embers are larger squares; the reference's are fine specks.
- [x] **M7.18 Port S17.** See §2.1 row S17.
  - *Evidence:* `out/cmp_S17.png`: s_52–s_54 above our dungeon hall (`vx_view:yaw=-1.5708 pitch=-0.12 site=8 cam=3 dist=4.5`, gold armour, wings and the Enchanted Sword, three skeletons spawned down the hall, one swing), on D3D12 and on WebGPU multi-threaded and single-threaded (same look). Sources are in `out/m718/S17/`.
  - What it took:
    - A dungeon is buried at a fixed depth across the world from the cabin: a grid of long brick halls (7 high, 7 wide) under a shaft that climbs from the central crossing to the surface.
    - Dungeon brick is a dark teal stack-bond tile with black joints and a raised-diamond panel variant, as in the reference.
    - Halls are lined with wooden posts that hold torches every six blocks, gold chests and pots along the walls, and cobwebs at the ceiling. Cobweb and wood post are new blocks.
    - The site sits far down a hall from the shaft, so only torchlight reaches it.
    - Fixed a light-volume upload loss that tinted the single-threaded WebGPU frame teal near the camera (decision 219).
  - Remaining differences:
    - No benches or tables: furniture with state is out of scope for this step.
    - The only enemy is our skeleton; the reference has several dungeon enemy kinds.
    - The reference has more spell effects in flight.
    - Our hall is narrower, so the camera sits closer.
- [x] **M7.19 Port S18.** See §2.1 row S18.
  - *Evidence:* `out/cmp_S18.png`: s_55–s_57 above our Empress of Light at dusk over a timber frame by the Hallow, mid bolt fan (script in `out/m719/S18/script.txt`: `vx_frame` and `vx_boss:kind=empress` at time 0.53). Captured on D3D12 and on WebGPU multi-threaded and single-threaded (same look). Sources are in `out/m719/S18/`.
  - What it took:
    - A new boss, the Empress of Light. She is a luminous gowned figure that trails away to a wisp, with a gold sunburst crown and arms she raises to cast. Her four wings are stained glass: teardrop panels of colour running cyan at the root, through gold, to pink at the upper tips, leaded with dark ribs.
    - Her fight cycles four attacks:
      - prismatic bolt fans, which spread out in a ring and then turn on the player;
      - thrown crescents;
      - a second bolt fan;
      - a charge with a rainbow wake.
    - Below half life she casts faster. Her projectiles pass through blocks and hurt the player.
    - She lights the arena round her. After the Eye of Cthulhu is beaten, she arrives on the first dusk over the Hallow.
    - New wooden beams, and a `vx_frame` command that builds the open timber frame of a player's sky arena.
    - The sky:
      - A low moon gets a halo, rays fanning up through the cloud gaps, and a warm peach horizon beneath it.
      - A 0.53 dusk key gives the mauve sky and warm low light measured from s_55.
  - Remaining differences:
    - The reference wings are softer and translucent; ours are opaque glowing cells.
    - Our clouds are lower and blockier, and they often hide the moon's disc; the reference has a clear horizon band with a bright disc and a stone tower.
    - Our player stays between the camera and the boss, because her hover centres on the camera's view.
    - Crescents that pass close to the camera are seen edge-on and read as bars.
- [x] **M7.20 Port S19.** See §2.1 row S19.
  - *Evidence:* `out/cmp_S19.png`: s_60–s_62 above our elevated view down the pylon avenue into the town at late afternoon (time 0.46), the same view at dusk (0.49), and the WebGPU multi-threaded and single-threaded captures (same look). The script is in `out/m720/S19/script.txt`. Sources are in `out/m720/S19/`.
  - What it took:
    - The town round the avenue now has the reference's layout. A white keep (new `VX_TP_WHITE_KEEP`: marble walls framed by dark obsidian bands, corners and battlements) closes the avenue's axis just past its west end. A red-brick house flanks the axis on the left and the existing wood house on the right. A second, dark keep north of the cross street adds to the skyline.
    - The walk carries the reference's second row: a new Light Box block (a dark steel rim round a grey panel with a lit seam, a glowing pale-cyan top, cyan block light) every four blocks along the walk's road edge, between the sandstone pillars. This also builds the "glowing grey boxes" M7.10 had listed as missing.
    - Dusk brightness: the 0.44 and 0.50 sky keys are darker (direct, ambient and ground light, and the zenith and horizon), so the late afternoon falls below the exposure ceiling and reads dim, as in the reference. Measured: sky 106 against the reference's 91, and grass 63 against 45. Before, they were 145 and about 95. Torches and light boxes now read as lights.
    - The camera is 8 blocks above the avenue's west half, looking down it at the keep, with the sun out of frame.
  - Checked: S9 (time 0.3, the same avenue) was recaptured on all three backends after these changes, with the same lighting and the light boxes now on its walk (`out/cmp_S9.png`). S18 and S20 sit exactly on the unchanged 0.53/0.56 keys, and every other scenario runs at 0.25–0.3.
  - Remaining differences: the reference foreground left has a wooden longhouse and a nurse's house; ours has a pearlwood tree and the greenhouse frame on the right. Our light boxes are full blocks; the reference boxes are slightly smaller, on low stone plinths.
- [x] **M7.21 Port S20.** See §2.1 row S20.
  - *Evidence:* `out/cmp_S20.png`: s_63–s_65 above our title screen (script `0.1:vx_seed:12345;10:screenshot`, in `out/m721/S20/script.txt`), on D3D12 and on WebGPU multi-threaded and single-threaded (same look). Sources are in `out/m721/S20/`.
  - What it took:
    - The title camera hangs 20 blocks over the pylon avenue's east half, looking steeply down it and swaying slowly. The avenue of pylons and light boxes runs up under the logo band, and the white keep, the houses and the forest rise above the band, as in the reference. The town reports the avenue through `vx_town_avenue`. A world without a town keeps the old orbit round the spawn.
    - The title is lit at 0.56, early night after the sun has gone. The scene is dark navy and green with lamps lit, like the reference. At 0.5–0.53, looking west into the sunset washed the frame orange.
    - The band stands alone, as in the reference. Play, Settings and New World are one row pinned to the bottom edge of the screen, not stacked under the band over the town.
  - Remaining differences: the reference's left foreground is a house roof; ours is a pearlwood crown.

## 11. Decisions

1. **Scale (decided).** Terraria proportions: the player is about 2.7 blocks tall.
2. **World height.** Keep 256 for the demo. 384 would need y packing changes in the quad record, the light job and the edit index.
3. **Lighting data (decided).** Flood-filled RGB light in a 3D texture for world lights; analytic point lights only for short-lived effects. The alternative is RGB baked into quads: +8 B per quad, a remesh on every light change, and no moving light that respects walls.
4. **Art and name (decided).** Original, procedurally generated art only, with no Re-Logic assets. The working title is Voxaria. The Eye of Cthulhu and the Guide keep their names until we publish.
5. **Code layout (decided).** Rename to `src/voxaria` and `vx_` in M0.
6. **The Terraria view (2D cutaway mode).** Build it in M5. It is the one thing a 3D Terraria can do that the reference doesn't.
7. **Music.** A game-side procedural tracker. Authored QOA tracks would add megabytes to the download.

## 12. Risks

- **Light uploads on mobile during fast wing flight.** Mitigation: throttle slab uploads and fall back to the coarse level while flying.
- **Procedural item and creature art** can look generic. Mitigation: a shape generator per item kind, palette ramps, and look-dev reviews per milestone.
- **Mobile frame time** with entities, particles and clouds together. Mitigation: per-preset budgets and the auto-downgrade, retuned.
- **Third-person block targeting** can feel imprecise. Mitigation: smart cursor and reach from the player, tuned in M3.
- **Liquid simulation cost** when large lakes are disturbed. Mitigation: an active-cell budget per frame, as WebCraft's water already has.
- **Memory.** RGB light doubles light memory (+64 KB per loaded column). Mitigation: cap the loaded radius on mobile.

## 13. Logs

Kept current during implementation; printed in full at the end for review.

### Decisions

Decisions made during implementation, newest last. The decisions in §11 were made with the user.

| # | Step | Decision | Reason |
|---|---|---|---|
| 1 | M0.2 | Postcards are a const table of `vx_view` argument strings (`vx_view:P1`), plus a `cave=<y>` option that drops the camera into the nearest sky-dark pocket | One parser for presets and ad-hoc views; cave shots stay valid when the generator changes |
| 2 | M0.3 | `tools/postcards.sh` captures every postcard in one native run, and one wasm run per postcard | The wasm runner takes a single screenshot per run |
| 3 | M1.1 | Tiles are painted on an 8×8 art grid upscaled ×2 into 16×16 layers; the 3 variants of a block are consecutive layers, picked per block by a position hash | Variant pick costs one add in the shader; no lookup table |
| 4 | M1.1 | Leaves are painted as 5 rounded clumps lit from the top-left, with holes only in crevices; leaves opacity 1 instead of 2 | Old leaves read as noise; opacity 2 left trunks under canopies near-black |
| 5 | M1.2 | Framing is computed in the terrain shader from 4 edge bits in the 12-byte quad (exposed rim, lit top band, merge seam) and fades with the art-pixel footprint | No extra geometry; distant tiles do not shimmer |
| 6 | M1.3 | Light word is u16 R4|G4|B4|Sky4; block-light levels are logarithmic (each level ×0.86) and light colours are given in log levels | Linear levels turned the −1 per block falloff into a hue shift (torch tails went red/olive); log levels fade every channel by the same ratio |
| 7 | M1.3 | Mushroom grass emits a dim blue light (2,6,11) and its texture glows faintly | Terraria mushroom biomes are lit by their grass; without it P4 was black between mushrooms |
| 8 | M1.4 | L1 (2 blocks/texel) is a max-downsample of L0; each level is valid only once every slot has been written (sticky), until then the shader uses the other level or sky-full; upload budgets 128 sections/frame native, 48 wasm, plus 2048 uniform sections | Max keeps small lights visible at the coarse level; the sticky fill avoids sampling garbage while streaming |
| 9 | M1.4 | In compute shaders, storage images take the lowest binding numbers and buffers come after | D3D12 maps storage images to UAV registers u0..n-1; another order made CreateComputePipelineState fail |
| 10 | M1.5 | Held light is a 32³ FIFO flood fill (cost 1 + opacity per step) cached on (cell, item, world edit serial) and uploaded as an rgba8 window | Refills only when the player crosses a block or the world changes; walls stop it like block light |
| 11 | M1.6 | Shadowed areas keep 32% of the sun where the sky is open, and ambient is hemisphere × sky curve × AO with neutral ambient colours | Full black shadows and blue ambient made the stylized look muddy |
| 12 | M1.6 | SSAO and soft (PCSS) shadows are on only in the Ultra preset; the lighting mode is not part of presets | Their cost is not worth it below Ultra; lighting mode is a taste choice, not a quality one |
| 13 | M1.7 | Falling stars are driven by the game (start direction, heading, progress in the frame uniforms) instead of hashed in the shader | A game-side event can be scripted for postcards and later tied to gameplay (Fallen Star pickups) |
| 14 | M1.8 | Clouds are a voxel DDA over 12×6×12-block cells in 5 layers from y 236, column height from a baked weather map | Chunky voxel clouds match the world; the old volumetric raymarcher was the wrong style and cost more |
| 15 | M1.9 | Art direction lives in one table (`vx_look.c`): 9 time-of-day keys and per-biome fog/grade, blended by biome weights; exposure clamped to 0.75–1.4 | One place to tune the look; a narrow exposure range stops caves washing out and dusk going dim |
| 16 | M1.12 | The real island generator (pulled ahead from M2) replaces the "minimal generator variant"; postcards find their spots with `cave=<y>`, `mushroom=1`, `biome=<id>`, `stage=1|2`, `day=`, `fallstar=<s>` and `light=<mode>` options, and P4 is pinned to a logged pocket | Look-dev on the real world avoids tuning against content that will be thrown away |
| 17 | M2.1 | The world is bounded at ±384 blocks (`vx_in_world`): outside is solid to collision and edits are refused, while terrain keeps streaming to the horizon | A Terraria world has edges; streaming past them keeps the view from ending in a void |
| 18 | M2.4 | Structures (cabins, lakes, ruins, lava falls, sky islands, the spawn hut, ponds, hives) are stamped per chunk from coarse cell grids whose sites depend only on position and seed | Any chunk can be generated alone, in any order, on any lane, with no cross-chunk pass |
| 19 | M2.4 | Underground lakes are carved basins with a dam ring that blocks carving at or below their level; surface ponds sit one block below the lowest point of their rim | Generated water never hangs over a drop, so no flowing is needed at load |
| 20 | M2.4 | Pots and chests are box models (`VxBox` lists, up to 6 per-face textures) carried by a third quad word: the box bounds in 4-bit block sixteenths | Non-cube props without a mesh format; the same 12-byte quad and culling path as cubes |
| 21 | M2.5 | The mushroom biome is 4 seeded pockets (ellipsoids in the cavern layer) with hollowed cores instead of a noise zone | The noise zone covered half the underground and its caves were too low to read |
| 22 | M2.5 | G-buffer RT1.b packs glow (6 bits) and glint (4 bits); glint is multiplied by incoming light in the deferred pass | Gems and ice sparkled in total darkness like stars |
| 23 | M2.6 | The underworld adds a warm uplight term (`under_glow`) to ambient, fading with height above the lava sea; obsidian brick and hellstone brick blocks were added for ruins | Ruins read as black silhouettes against the lava without it |
| 24 | M2.8 | Per-voxel state: a 4 KB state page per 16³ section, allocated from a world-owned pool only for sections that hold a non-zero state, on the main lane; edits store their state byte and regenerated chunks get their states back in job integration | Most sections never need state; the page pool keeps memory flat and the main lane owns all page allocation |
| 25 | M2.8 | State byte meaning: liquids 0 full or 1..7 eighths missing; torches 0 floor or 1..4 the wall they hang on; models 0..3 quarter turns (chests face the player) | One byte serves every stateful block; model rotation reuses the box list with remapped face textures |
| 26 | M2.9 | Liquids are a finite cellular automaton over an active set (ring queue + open-addressing set, 32K cells, 512 cells per step at 20 Hz): fall first, then level out with neighbours 2+ eighths lower; lava steps every 3rd tick, honey every 2nd; water meeting lava makes obsidian; cells in unloaded chunks are never written | Cost scales with moving liquid only and a step is bounded; finite volume matches Terraria; generated water is static until touched |
| 27 | M2.10 | Liquid tops slope: each top corner averages the levels of the cells sharing it, packed as 4 × 5-bit heights in the quad's third word; sides beside lower liquid of the same kind show | Sloped flows with no extra vertex data |
| 28 | M2.10 | Falling sheets and splash surfaces are quad bits; ambient motes (embers, mist, leaves, spores, snow, dust) live in a 640-entry pool with per-kind quotas, fed by emitters that a 12-column scan around the camera finds (lava tops, waterfall feet) and by biome rates; the scan and update cost 0.05 ms in a debug build | A random-cell sampler almost never hit a 1-wide waterfall foot; quotas stop long-lived leaves starving the mist |
| 29 | M2.10 | Heat haze reuses the final pass shimmer, driven by max(underworld depth, open lava near the camera from ember emitters) | No extra pass or g-buffer flag; nearby lava pools shimmer too |
| 30 | M2.10 | The entity vertex shader now forwards its per-vertex tint (it was dropped), so particles, fireflies and motes are tinted | Ember and spore colours depend on it |
| 31 | M2.10 | Honey: jungle bee hives (hive block with a comb texture, hollow shell half full of honey) under the jungle surface | The plan lists honey; nothing generated it |
| 32 | M2.11 | The background ring is drawn in the deferred sky branch as 4 cylinders (radius 700 × 1.45^n round the world origin), intersected per sky pixel so they parallax as the camera moves; skylines are stepped on a 1600-step angular grid; shapes and colours are blended by the smoothed biome weights (two new frame uniforms); the farthest ring is a blue-grey mountain range behind every biome | Real parallax and biome cross-fade with no geometry; stepped edges match the pixel-art look; the reference frames show distant mountains whatever the biome |
| 33 | M2.11 | Ring pixels are flagged with scene alpha 0.5 and the atmospherics pass skips its distance fog there; the ring applies its own per-layer haze | Sky pixels are fogged to the horizon distance, which erased the ring entirely |
| 34 | M2.12 | Particles are a game-side billboard pass into the g-buffer: CPU-simulated, one storage buffer of 32-byte records, 6 vertices each pulled by vertex index, cap 16384 native / 4096 wasm; sprites are pixel-art tiles painted into the block tile array (mote, puff, spark, flake, leaf) instead of the engine's `pg_bake_sprite_atlas`; alpha is a 4×4 ordered dither that TAA resolves; glow is written as g-buffer emission | The baked atlas is soft painterly art that clashes with the 8×8 art grid; g-buffer billboards get the deferred lighting, light volume, fog and TAA for free with no sorted blend pass |
| 35 | M2.13 | P10 is reframed over the jungle canopy (above 26, pitch -0.3) and the jungle fog is lighter and less green (density 0.0016) | The old framing sat inside a mahogany canopy; the reference is an elevated view in clear humid air |
| 36 | M2.13 | Tile textures declare their own variant count (3 for natural materials, 1 for props, liquids, torches and particle sprites); array layer bases are an enum counted at compile time, and a static_assert keeps the array at 256 layers or fewer | Adding the sprites took the array to 261 layers, past WebGPU's portable 256 limit: every wasm frame failed validation and rendered black |
| 37 | M3.1 | The camera is a third-person orbit 6 blocks behind a chest-high pivot offset 0.75 to the right, pulled in by five voxel rays (centre and a near-plane cross) that stop at solid or view-blocking blocks only; it snaps in and eases out at 4/s. Reach (6 blocks) is measured from the player's chest, not the camera, and the target is the first block on the camera ray within that reach | Terraria measures reach from the player; a camera ray can pass blocks behind the player that must not be minable. Snapping in avoids ever showing the inside of a wall |
| 38 | M3.1 | Smart cursor (Left Ctrl toggle, Terraria's default) picks, among exposed breakable blocks within reach, the one nearest the view ray within about 20 degrees; sprint moved off Left Ctrl since Terraria has no sprint | Exposure is checked on the face toward the camera so it never targets buried blocks |
| 39 | M3.1 | Exposure, fog, eye light and the underwater look follow the camera position, not the player's eye; `vx_view` defaults to first person when a `yaw` is given so the M1/M2 postcards keep their composition, `cam=3`, `orbit=` and `dist=` compose third-person shots | In third person the image is seen from the camera; the postcard set must stay comparable to its references |
| 40 | M3.2 | Entity vertices carry their own normal (16 floats, was 13 with a face index and one rotation uniform per draw) | A posed puppet has many independently rotated boxes; a per-draw rotation cannot light them. The held item's normals are now rotated on the CPU too, and the uniform block is gone |
| 41 | M3.2 | The player's puppet is built every frame from boxes on hip/shoulder/neck pivots with a whole-body tilt about the middle, tinted per part from a near-white grain texture (`T_PUPPET`, one art pixel per 1/8 block); colours are linearised before tinting | One texture and per-vertex tints make every colour combination free; tints multiply the albedo after its sRGB decode, so sRGB tints washed the colours out |
| 42 | M3.2 | Poses (idle breathing, walk from distance walked, rising, falling, swing, flight, swim, hanging) are targets eased at 16/s; the swing drives the right arm directly and turns the body to the view | Terraria's sprite poses snap; easing hides the change of state in 3D without lagging the swing |
| 43 | M3.2 | The puppet casts shadows into the two per-frame cascades only (new `vx_entity_shadow` pipeline), also in first person where it is not drawn | The far cascades are cached and would keep stale silhouettes; a first-person player with no shadow looks detached |
| 44 | M3.2 | Character colours live in `VxSettings` as palette indices (6 skin tones, 16 dyes, 4 hair styles) with a CHARACTER section in Settings and a `vx_character` test command; the settings dropdown array is now sized by `VX_DROPDOWN_COUNT` | It fixes an existing overflow: `VX_SETTINGS_DROPDOWNS` was 4 for 5 dropdowns, so the Lighting dropdown wrote past the array |
| 45 | M3.3 | Movement constants: run 8.5 blocks/s, jump 16 blocks/s with 0.28 s of reduced gravity while held (tap 2.3 blocks, held 4.9), gravity 72, terminal fall 40, coyote 0.1 s, jump buffer 0.12 s, automatic one-block step-up; hitbox 1.1 x 2.7 x 1.1 | Terraria's run is faster (about 11 tiles/s), but in 3D a slower run reads better against nearby geometry; the jump height matches Terraria's roughly 5 tiles |
| 46 | M3.3 | The grappling hook is a rope: it reels in at 17 blocks/s and only pulls, so gravity and steering swing the player under it; it flies from the hand toward the point under the crosshair, and view rays start level with the player | Aiming along the camera direction from the hand missed the crosshair through third-person parallax, and a camera inside foliage behind the player hit a leaf at distance 0, so the hook shot backwards; the same fix keeps targeting from selecting blocks between the camera and the player |
| 47 | M3.3 | Double jump, hook and wings are player abilities set by a `vx_ability` test command until M3.6 accessories grant them; wings give 1.5 s of lift while jump is held after the jump's rise, then a 3.5 blocks/s glide | Movement had to be verifiable before items exist |
| 48 | M3.4 | Life 100 as five hearts of 20, mana 20 as one star; fall damage 10 per block past 25, lava 50 per hit, 20 s of breath then 15 life/s; 0.67 s hit immunity; regeneration starts 3 s after a hit and grows to 4/s; respawn after 5 s at the spawn with full life, keeping the inventory (Terraria's softcore) | Terraria's own numbers where they fit; no coins exist to drop |
| 49 | M3.5 | Mining is Terraria's tile damage: each hit adds pick power x the block's rate (stone 100, dirt and sand faster, ores and hellstone slower) and the block breaks at 100; damage heals after 5 s idle, at most 16 damaged blocks are tracked; a pick below a block's required power only chips dust | Matches how Terraria's pickaxe tiers feel, with copper needing 3 hits on stone |
| 50 | M3.5 | The axe fells a tree: a breadth-first walk over the 26 neighbours of the cut log, never below it, caps at 640 logs; leaves within 4 blocks of no remaining log are removed after the fell | Branches leave the trunk diagonally, so face neighbours miss them; the cap bounds the work on a huge tree |
| 51 | M3.5 | Cracks are a cutout shell of the existing three-stage crack texture drawn as entity geometry 0.004 outside the block | No terrain remesh per hit, and the shell depth-tests cleanly against the block |
| 52 | M3.6 | Item ids share one space with blocks: ids below `B_COUNT` are the blocks themselves (placeable), items follow from a const X-macro table with kind, art, material tier, power, damage, use time, stack size, rarity and mana | One id type for the inventory, drops and recipes; no mapping table between block and item |
| 53 | M3.6 | Drops: up to 384, a 0.55 s pickup delay so they are seen to pop, a 5.25-block magnet (Terraria's grab range) that speeds up as it closes in, merging every 0.25 s within 1.2 blocks; pickups raise floating text that accumulates per item while fresh, and a synthesized pop | Terraria's behaviour; merging keeps a felled tree from becoming dozens of entities |
| 54 | M3.6 | The hotbar stays bottom-centre and the bag opens as a centred panel: 5x10 grid whose top row is the hotbar, armor and accessory columns, Defense, Trash; left click picks up, drops, merges or swaps, right click takes one or equips; closing stows the cursor stack and drops what does not fit; accessories cannot be equipped twice | Terraria's top-left hotbar would sit under the performance overlay; the click rules are Terraria's |
| 55 | M3.6 | Movement abilities now come from accessories (Cloud in a Bottle, Grappling Hook, Angel Wings, Hermes Boots x1.45 run); `vx_ability` still forces them for tests | M3.3's interim test switch becomes equipment |
| 56 | M3.7 | Item art is generated, not drawn: one generator per art kind paints palette slots on a 16x16 canvas (thick-stroke, disc and shaded-ball primitives), recoloured by a material ramp that cools as it darkens, then a dark outline traced around the silhouette; ores, torches and furniture are blocks and use the isometric block icons | One generator covers every tier of a tool; strokes keep diagonals gap-free, which Bresenham lines did not |
| 57 | M3.7 | Item art lives in its own 16x16 texture array with coverage-preserving mips (the block textures' bleed and alpha fit, factored out), bound to the entity pipelines and selected by vertex flag 8; held and dropped items are four stacked alpha-tested slices, one art pixel thick | No second pipeline or draw; the slices read as a solid extruded sprite from any angle |
| 58 | M3.7 | Gem ores are renamed "Amethyst/Sapphire/Ruby/Emerald Stone" and drop the gem items | Terraria's gem tiles drop gems, and the item names would otherwise clash with the block names |
| 59 | M3.8 | Furniture is one block id per piece over w x h cells (one deep): each cell stores its quarter turn and part index in the block state, the piece's boxes are authored over the whole footprint and cut to each cell at mesh time (faces on a cut are dropped); pieces turn to face the player, centre on the target cell, need solid ground under the bottom row, break whole from any cell and drop one item | Reuses the existing box mesher (4-bit box coordinates per cell) and chunk edit storage with no new per-piece records; a piece crossing a chunk border needs nothing special |
| 60 | M3.8 | Doors are 1x3 and swap between a solid closed id and a non-solid open id on right click (touch: tap), keeping state; the open panel lies against the hinge side inside its own cells; a door will not close on the player | The open door stays in its own cells, so opening never fails for lack of room |
| 61 | M3.8 | Platforms are non-solid for everything except a falling player whose feet cross their top from above; holding Shift (touch: Down) drops through | Terraria's one-way platforms, done in the swept vertical move so fast falls cannot tunnel |
| 62 | M3.8 | Chests keep 40 slots in a table of up to 1024 chest records keyed by position, created when first opened; the chest panel opens over the bag with Loot All and Deposit All (which keeps the hotbar); a chest holding items cannot be broken; the panel closes beyond 7 blocks | Terraria's chest rules; in memory only, as saves are Tier B |
| 63 | M3.8 | Trees drop Wood that places as plank blocks (log blocks are no longer obtainable); model and furniture icons and held/dropped models are drawn from their boxes, fitted to the footprint | Terraria's Wood is the building block; a cube icon misrepresented every piece of furniture |
| 64 | M3.9 | Crafting stations are Work Bench, Furnace and Iron Anvil, found by scanning 9 x 6 x 9 blocks around the player each update; 40 recipes in a const table with up to 3 inputs; the grid lists only recipes craftable now (Terraria's rule) and a click puts the result on the cursor, merging with a matching stack | Terraria's crafting model; the scan is 486 block reads, cheaper than tracking station placement |
| 65 | M3.9 | Recipes without their Terraria station or ingredient use the nearest stand-in: Lesser Healing Potion and the Suspicious Looking Eye at the Work Bench (no Placed Bottle or Demon Altar), Grappling Hook from 6 Iron Bars at the anvil (no Hook drop or Iron Chain item) | Keeps every item obtainable without adding stations and drops that nothing else uses |
| 66 | M3.10 | The map is a persistent 768 x 768 texture, one texel per world column (top block colour from its texture mean, relief shading from the height step to the north-west, darker deep water), revealed as chunks load and redrawn when a chunk's block or light versions change, at most 12 chunk regions (16 x 16 uploads) per frame, nearest first; the minimap and full map are the same texture under a clipped, offset window | Terraria's explored map in a top-down form that fits a 3D island; region uploads keep wasm upload traffic to a few KB a frame |
| 67 | M3.10 | The event log keeps the last 8 lines bottom left for 10 s (crafts, deaths); M toggles the full map, H drinks a healing potion (quick heal); touch gets Map, Hook (only with a hook equipped) and Heal (only with potions) buttons, and a tap on armor or an accessory in the bag equips it | Touch has no right click, keys or chat |
| 68 | M3.11 | One-shot sounds are a named table built at start-up: the engine's procedural voices where they fit (whoosh for swings, hook and wings, jump, land, metal and wood impacts, the damage cue) and local synthesis for the rest (gulps, shimmer partials, saw-wave creaks with a knock, a crack plus low rumble for a falling tree, a falling cry for death); mining hits replay the material's break sound quieter and higher; every play logs its name when scripted | Reuses tested engine voices; one table keeps every sound named for the trigger log |
| 69 | M3.13 | Added at the user's prompt: the plan's art direction already called for entity outlines (Silhouettes first; post pass row 8), but no step owned them and none were rendered. Crops of refs 14.0 s, 25.0 s and 53.0 s show the line on characters, furniture and block silhouettes, with constant screen width and a darker shade of the surface colour, so it is a screen-space depth/normal edge pass, not inverted-hull meshes | The reference is the spec; a screen-space pass costs one fullscreen tap set and covers every kind of geometry |
| 70 | M3.12 | The scripted hut is built from wood the run gathered: grass is cut with the sword first (Terraria blades cut grass), each wall block is placed from the interior cell next to it, corners from a hover, and the shaft is dug by cycling the aim over the four blocks under the 2-wide player | Shallow rays from the middle caught floor plants and corner neighbours; the smart cursor deepened one column while the player stood on the other three |
| 71 | §2.1 | Added the full scenario catalogue (S1–S20, all 65 frames saved) and milestone M7, one port step per scenario | The user's direction: not done until every scenario is ported; frames are the spec |
| 72 | M7.1 | Tile textures are 128×128 layers covering 2×2 blocks (64 texels per block, 8 mips, aniso); cube and model faces sample them in world space (`fract(world·basis / 2)`), sprites one block of it; items keep their own 16×16 array with texel-AA | The reference shows smooth painted tiles with no per-block grid; world mapping lets stone, planks and bricks continue across blocks at no extra cost |
| 73 | M7.1 | The per-face tile framing (edges/merge bits) is gone from the mesher and shader; M3.13's screen-space outline draws the edges instead | The frame drew a box round every block, the Minecraft look the user rejected |
| 74 | M7.1 | Leaves are opaque occluders | The reference canopies are solid; culling their inner faces cut P1's GPU time from 6.7 to 1.9 ms |
| 75 | M7.1 | Clouds read a baked puff field (weather map blue channel: tileable voronoi domes, 48 per 1400 blocks) gated by coverage; 6×4×6-block cells, 200 DDA steps, 2000-block reach; cloud shadows use the same occupancy | The old map made 140-block sheets; the reference has separate puffs. One texel per step keeps the cost of the old trace |
| 76 | M7.1 | Sky pixels are fogged only in the horizon band (fades out by dir.y = 0.15), applied identically in the world-edge blend | Fogging the sky over 3000 blocks washed out the zenith the gradient already paints; denser terrain haze needed it |
| 77 | M7.0–7.1 | Done before M4, out of the listed order | Every later step's capture depends on the base look; doing it after would redo all those comparisons |
| 78 | M4.1 | Puppets and creatures draw as instanced boxes: one 64-byte record per box in a storage buffer, 36 vertices expanded in the vertex shader (`vx_box`, `vx_box_shadow`) | The CPU used to write 24 vertices × 64 bytes per box; a record is 16× less upload and the box count is bounded only by the buffer (8192) |
| 79 | M4.1 | The particle and box vertex shaders split each quad as triangles 0 1 2 / 0 2 3; the old index map gave the second triangle corners 3 and 4 (4 collapsing to 3), so every GPU particle drew as half a quad | Found when the first boxes rendered as spikes; the fix restores the full particle sprites as well |
| 80 | M4.1 | Creatures reuse the player's voxel AABB test (`vx_aabb_collides`), with moves sub-stepped under 0.45 blocks per axis | One collision rule for every entity; sub-steps keep fast knockback and swoops from tunnelling |
| 81 | M4.1 | A spawned creature rises out of any solid block it was placed in (up to 24 blocks) | Spawn points inside trunks left creatures stuck forever |
| 82 | M4.2 | A contact hit gives the player 0.3 s of knockback during which steering acceleration drops to 1.5/s | Ground acceleration (10–14/s) cancelled a knockback within 0.1 s; Terraria's knockback carries the player |
| 83 | M4.2 | Creature stats follow Terraria's pre-hardmode values (e.g. green slime 14 HP / 6 damage, zombie 45 / 14 / 6 defense); damage taken is damage − defense / 2, at least 1 | Familiar difficulty, and the published formula |
| 84 | M4.3 | Spawn tables: surface day slimes (eaters in the corruption, a 1.2 % Pinky), night zombies 55 % / demon eyes 35 % / slimes, underground bats 65 % / slimes with skeletons below y 100, underworld Hellbats 60 % / Lava Slimes; slime and bat variants follow the biome (snow → ice, jungle → jungle) | Terraria's pre-hardmode sets for each layer and time |
| 85 | M4.3 | A spawn is only accepted in the player's own layer and the cap counts creatures within 72 blocks in 3D | Surface creatures left above must not block the caves' spawns, and caves under a surface player stay empty |
| 86 | M4.4 | Melee arcs are drawn in the plane facing the view, a block ahead of the chest, overhead from the weapon side | Any plane containing the aim is seen edge-on by a camera looking along it; Terraria's swings are in the screen plane |
| 87 | M4.4 | Projectiles leave the weapon hand toward the first block or creature on the camera's crosshair ray (marched to 64 blocks) | Firing along the camera's forward from the body misses what the crosshair is on in third person (parallax) |
| 88 | M4.4 | Combat effects are new mote kinds (spark, trail, gore) in the existing mote pool, raised to 2048 | One particle path for all effects until M4.8 moves them to the GPU |
| 89 | M4.4 | The hit flash mixes 70 % toward a 2× white instead of fully to 3× | Full white erased the creature's features at close range |
| 90 | M4.5 | Gore is real tumbling boxes (a debris pool drawn through the instanced box path), not sprites | The reference's impacts throw lit cube debris; boxes take the world's light and shadows like everything else |
| 91 | M4.5 | Coin change-up happens inside `vx_inventory_add`, converting at most once per add and storing what the next tier cannot take | Every pickup and give path converts; a full bag cannot loop or lose coins |
| 92 | M4.6 | A generated chest gets its record and loot on first touch, seeded by its position; a placed chest gets an empty record on placement (placement refused when the record table is full) | Generation stays a pure block function, loot is stable per world, and a player's chest can never be mistaken for a loot chest |
| 93 | M4.6 | Fallen stars are a projectile kind (straight line, through tree crowns, settling on liquids) that becomes a Fallen Star drop; the drops fade at dawn | Reuses sub-stepped flight, block tests and lighting; Terraria's stars pass trees and vanish at daybreak |
| 94 | M4.6 | Test commands `vx_interact` (one right click on the crosshair target) and `vx_craft:item=` (crafts as the panel does) | Scripted mouse input does not reach the game; these drive the real code paths directly |
| 95 | M4.7 | Jelly thickness is the exact ray–box path computed in the box's local frame, not a back-face depth pass | One pass, no extra target, exact for the box shapes slimes are built from |
| 96 | M4.7 | The jelly pass re-copies the scene after the liquids when any jelly is visible | A slime in front of water refracts the water too; the copy costs nothing when no slime is on screen |
| 97 | M4.8 | Effects are split: glows, smoke, splashes and flames are forward sprites after the fog; leaves, snow and gore stay lit pixel-art cards in the g-buffer | Additive glow and soft alpha need blending the g-buffer cannot do; opaque pixel pieces want the deferred lighting and outlines the world gets |
| 98 | M4.8 | Sprites draw after the fog and before TAA, faded against a copy of the depth | The fog would otherwise eat glow, TAA still smooths the edges, and soft depth fade removes hard intersection lines |
| 99 | M4.8 | Sparkles are analytic pixel squares with a halo, not atlas sprites | S17's sparkles are hard-edged squares; a baked soft sprite cannot stay crisp at every size |
| 100 | M4.8 | The atlas painters' RGB is clamped to zero in the sprite shader | Some painters return negative detail outside the coverage; under additive blending that subtracted light (magenta and black on the floor) |
| 101 | M4.8 | Emitters are found by a deterministic column sweep instead of random sampling | A torch placed or uncovered lights within a third of a second, not after seconds of luck |
| 102 | M4.8 | Mote quotas are shares of a 16384 pool scaled to the platform's | On wasm (4096) the swing trail alone could otherwise take the whole pool |
| 103 | M4.8 | `vx_spawn` takes `hp=` | One blow can show a death; test tooling kept to one line |
| 104 | M4.9 | The Guide is a creature kind with a townsfolk behaviour, not a separate NPC system | He reuses creature physics, collision, streaming and the build pass; one flag check keeps combat, despawn and spawn caps off him |
| 105 | M4.9 | He is drawn with the player's puppet (`vx_puppet_body`), not a creature box set | S5's townsfolk are built exactly like the player; one builder keeps them alike as either changes |
| 106 | M4.9 | Talk picks along the view ray with a slab test and an unblocked check, before blocks | Right-click is also doors and chests; the Guide standing in front of them must win, and a wall must hide him |
| 107 | M4.9 | Tips are original lines about this game's systems | No Re-Logic text; each tip names a mechanic that exists here (work bench, torches, pots, H to heal, fallen stars, mushrooms, the hook) |
| 108 | M4.9 | At night, out of range and off-screen, he goes home at once | Without pathfinding he cannot find the door from outside; Terraria moves its townsfolk home the same way |
| 109 | M4.9 | The first-night event line is a short form of the night tip | The event log line holds 95 characters; the dialogue keeps the full line |
| 110 | M4.9 | `vx_look` takes `guide=1` | A scripted talk needs the view on him while he walks; one argument |
| 111 | M4.10 | Ambience is one game-side live stream mixing five beds, with the engine generator as the day bed | The engine generator is one room tone (noise bed plus sine chirps): right for wind and birds, wrong for crickets, drips, rumble and surf. One stream with per-sample gain ramps crossfades without voices starting and stopping, and silent beds skip their synthesis |
| 112 | M4.10 | The beds are driven by the smoothed state the renderer already keeps (underground, underworld, biome weights) and the day clock | The sound changes exactly as the fog and exposure do, with no second classification |
| 113 | M4.10 | The killing blow plays the kill sound instead of the hit | Terraria's NPC_Killed replaces NPC_Hit on death; both together smear the moment |
| 114 | M4.10 | Positional sounds (groans, arrow thunks) are scaled by distance only | The mixer is mono with no panning, so distance is the cue it can carry |
| 115 | M4.11 | Fight postcards add their spawns and the swing from `tools/postcards.sh`, timed back from the shot, not from options in `vx_view` | Creatures and swings are actions over time, not camera state; one table of offsets serves the native and wasm runners |
| 116 | M4.11 | P12 and P13 are captured in separate native runs | Night creatures within 40 blocks stay into the day, so P12's zombies would walk into P13 |
| 117 | M5.1 | The Eye is a voxel sphere of surface cells, not a smooth mesh | It matches the world's voxel language; per-cell colours draw iris, veins and the mouth with no textures; about 530 boxes is cheap against the 8192 cap |
| 118 | M5.1 | The Eye passes through blocks but never sinks below the player's feet | Terraria's Eye ignores tiles, but in 3D a body inside the terrain is invisible; a dash at the chest from above would bury it every time |
| 119 | M5.1 | It hovers 11 out and 5 up along its current bearing, not straight above the player | Terraria's above-the-player spot is off-screen for a third-person camera; out along its bearing keeps it in view |
| 120 | M5.1 | The dusk event needs 200 life and 7 defense (a full iron set), not Terraria's 10 | Our best pre-boss armour is the iron set's 7; at 10 the natural arrival could never happen |
| 121 | M5.1 | Knockback is skipped entirely for a creature that resists all of it | The old code zeroed its horizontal velocity on every hit, which would stop the Eye's dash on any blow |
| 122 | M5.1 | The Suspicious Looking Eye stays a work-bench recipe | There is no Demon Altar block; the work bench is the station every player has by the first night |
| 123 | M5.1 | Natural-spawn off switch and armour equip in the test tooling | The night spawner kept 8 creatures on the scripted player, so the hurt flash tinted every boss shot red |
| 124 | M5.2 | The music is a game-side pattern tracker with synth voices, not audio files | The plan rules out shipped tracks for download size; a song is a few hundred bytes of pattern strings |
| 125 | M5.2 | Two players crossfade, and a fading song resumes rather than restarts | Walking in and out of a cave should not restart a loop each time; one spare player covers any change |
| 126 | M5.2 | A context must hold 0.75 s before the music changes, except the boss and the title | The smoothed depth passes the underground band on any teleport or respawn; without a dwell that starts a song for a moment |
| 127 | M5.2 | The audio source pool is sized from the game's own source count | The engine default of 64 was already full, so a new stream silently failed |
| 128 | M5.2 | Night is one shared test, `vx_is_night` | Creatures, the boss, the dusk event and the music must agree on the hour |
| 129 | M5.3 | The title holds only the render's clock at golden hour, not the world's | The world and its creatures keep their real time, so pressing Play does not jump the clock |
| 130 | M5.3 | The flyover's height comes from a disc of columns around the camera and its path ahead | Sampling the path alone dropped the camera between two crowns and under the canopy |
| 131 | M5.4 | A sky chest is a gold chest on sunplate, not a new block | Only the sky huts use sunplate and a placed chest is never filled, so the test is exact with no new block or save data |
| 132 | M5.4 | The wings are golden feathers, not white | S4, the visual spec, shows golden feathered wings; the name Angel Wings stays |
| 133 | M5.5 | The cut is capped by the slab's own +z faces, emitted by the mesher, not by separate cap quads | The block's real texture, AO and material come for free; only one chunk row remeshes on a toggle |
| 134 | M5.5 | Cut faces read a 2D light map with light seeping into solids, not the 3D light volume | The volume is zero inside solid blocks, so every cut tile was black; Terraria lights tiles by seeping light, which reads as the reference's lit edges |
| 135 | M5.5 | Cut faces get full daylight scaled by seeped sky light, with no sun term | Sun and shadow on a wall nobody built read as streaks; Terraria's tiles show plain light levels |
| 136 | M5.5 | Every atmosphere march starts at the plane | The removed air in front of the plane is 44 blocks of fog and shafts that painted a grey veil |
| 137 | M5.5 | The cutaway is a toggle on the 3D game, not a separate mode | Every system (combat, building, creatures) keeps working; only the camera, input and targeting change |
| 138 | M5.6 | Shake moves only the rendered view, never the logical camera | Aim, targeting and the reach ray stay steady, so a jolt never makes a swing or a placed block miss |
| 139 | M5.6 | Hit-stop slows the simulation to 6%, not to zero, and leaves the clock and interface running | A full stop reads as a hitch; time of day, weather, audio and UI pausing would read as a frame drop |
| 140 | M5.6 | First-person shake is a fifth of third-person movement | The eye sits near walls in first person; rotation carries the jolt there |
| 141 | M5.7 | The skybridge is 8 platforms wide, not Terraria's 1 | In 3D the Eye's knockback is horizontal in any direction; a 1- or 3-wide bridge threw the player off within seconds. The reference's bridge is a wide deck too |
| 142 | M5.7 | The Eye's hover bearing is blended toward the camera's view | A boss behind the camera is a boss the player cannot see; Terraria's camera always frames it |
| 143 | M5.7 | Near-camera fade is a per-box flag set only by creatures, dithered in the g-buffer | Stays in the opaque deferred path (no sorting, works with TAA); a distance test alone also dissolved the player's head in normal third person |
| 144 | M5.7 | The full-screen red hurt flash is first-person only | Terraria shows a hit on the character; with a visible puppet the wash only hid the scene |
| 145 | M6.2 | The side view gets its own touch pill, not a gesture | A gesture would collide with look-drag and mining holds; a pill sits with Map and Menu where a thumb already goes |
| 146 | M6.2 | In the side view a finger picks the slab tile under it, not a ray from the eye | The cutaway camera is orthogonal to the slab, so the screen point maps straight to one tile, as in Terraria's touch controls |
| 147 | M6.2 | Touch tracks are reconciled against live touches every frame | A finger lifted while a menu was open left a track that hijacked the next touch with the same id |
| 148 | M6.3 | The 500 KB target is reported for code; the UI fonts are reported separately and not cut | The font pages are the engine's shared Inter MTSDF atlas; shrinking them is an engine UI decision for the owner, not a game change |
| 149 | M6.3 | The font index is a compressed asset type, cooked with .br/.zst siblings like font pages | It is read once at boot and compresses about 5x; the cook and the loader keep one list of compressed types |
| 150 | M6.3 | Menu copy rewritten from the real bindings | Players saw Minecraft controls and a "Shaders Edition" subtitle in a Terraria game |
| 151 | M7.2 | Beyond render distance the island is a far-field LOD: 48×48 chunk tiles of 8×8 cells (2×2 blocks), each cell up to 3 solid runs at block-precision heights. Regions of 8×8 chunks are meshed into greedy tops/bottoms and interval-subtracted sides in a 3-u32 quad format, skipped per chunk by a 64×64 near-coverage mask | S1 shows the whole island and its sea from the sky; full-resolution chunks to ±384 would cost about 100× the memory and triangles. Tiles are reduced from loaded chunks when they exist, otherwise from a surface-only generation pass |
| 152 | M7.2 | A run's last slot always holds the ground run, and far water side faces are opaque textured | Exhausting the slots on isles, trees and falls stretched the last run down to y = 0 as pillars under every far isle; deferred water shading on vertical faces turned waterfalls into white poles |
| 153 | M7.2 | The open sea is one camera-centred plane at the water surface, discarded over near-meshed chunks and past the mountain ring (2100 blocks) | One draw replaces thousands of ocean chunks. Stopping at the ring lets the sky pass draw the distant range as in s_01 |
| 154 | M7.2 | The horizon keeps only the outermost ring (the blue-grey mountain range, 0.06·R tall, 38% hazed); the nearer biome rings are dropped | The far field and the sea now fill those distances; a nearer ring would draw land in front of real sea |
| 155 | M7.2 | Desert landmarks are fixed spots at the desert sector's centre (butte r 24/18.5 with a stepped base, four pillars, a small butte, a stepped pyramid, two oases), each validated as inland desert at reseed | S1's composition is specific; a noise field never produced a stacked-cylinder tower |
| 156 | M7.2 | Sky isles sit on a ring 300–355 blocks out, over the coast and sea | The reference rings the island with them; over land the voxel skylight under a 50-block isle went cave-black |
| 157 | M7.2 | Clouds: 8 layers of 6×4×6 cells, flat bases, marched by a 2D column DDA (one weather fetch per column, the ray meets a column's side or top analytically, 520 steps to a 1500-block faded reach). Cloud shadows test the same 8 layers | Taller puffs match the reference. The 3D DDA at 8 layers cost about 3.7 ms more; the column march costs the same as the old 5-layer trace. Lifted bellies aliased into speckle at distance (columns under a pixel), so the bases stay flat |
| 158 | M7.2 | Desert palette: pale sand (0xFCF4DE→0xD6C49C), cream mesa walls with no red courses, a red-blotched mesa cap, darker cacti, neutral grade, blue-white desert haze, global haze density ×1.1 | s_01–s_04's sand is near white with grey contour shadows; the warm grade and sepia fog read as Minecraft |
| 159 | M7.3 | A Hallow biome takes sector 4: hallowed grass with pearlsand blots from patch noise, pearlwood trees with gold or violet clumps, and a smaller central forest radius (74) next to it | s_04–s_05 show cyan grass, pale round patches and pink/yellow trees between the corruption and the desert |
| 160 | M7.3 | The town is a fixed part table in world space near spawn (roads, five house kinds, a greenhouse) stamped per chunk. Its plateau damps the hills at half strength, each lot floor is the highest sampled ground plus one with footings below, and roads follow the sampled ground | A fixed plan gives S2 and the later town scenes (S5–S9, S19) one consistent town; a per-chunk stamp keeps generation chunk-local and thread-safe |
| 161 | M7.3 | The greenhouse frame uses three new model blocks (a copper post and two copper beams) | s_05's frame is thin bars, not full blocks |
| 162 | M7.3 | Grass side fringes reach about 0.6 of the block | At the reference's flyover distance, one-block terraces must read as green bands, not dirt stripes |
| 163 | M7.4 | Rich mahogany: a tall bare trunk under broad flat canopy discs (radius 4–7, two layers) with smaller side tiers, and a dark brown trunk texture | s_08–s_10's jungle is a sea of flat umbrella canopies on dark trunks; the old 2×2 red trunk with a small dome read as Minecraft jungle |
| 164 | M7.4 | Tree placement reaches 8 blocks past the chunk; trunks outside the 4-block scratch margin sample their own column | Wide canopies otherwise stop at chunk borders. Growing the scratch margin to 8 would sample 1.8× the columns per chunk; the extra trunks cost about nine samples |
| 165 | M7.4 | Landmarks carry their biome: the jungle sector gets two slate ridges (elliptical, six-block ledges with grass lips) and two slate spires, placed like the desert's. No trees grow on jungle landmarks | s_09's stepped ridge and its spire stand above the canopy; trees on the ledges hid them |
| 166 | M7.4 | A dark blue-grey slate block for jungle rock | s_09's cliff is near-black slate, not the light cave stone |
| 167 | M7.5 | Sky islands: radius 14–19, a cloud bowl about 0.62·r deep with a cloud lip past the grass, a flat meadow top, a 7×7 mahogany house, a tall and a small tree; no pond or waterfall (the ISLE_FALL site kind is removed) | s_11–s_13: a puffy white bowl under a grassy top with a brick cube house and a tall tree; the white waterfall poles read as artefacts from afar |
| 168 | M7.5 | Cloud tiles emit 0.3 of their albedo | Cloud scatters light into its shaded side; under voxel skylight alone the bowl's underside went grey, where the reference stays bright white |
| 169 | M7.5 | A far-field run that floats (bottom above y 0) shows its bottom block on its sides below the top three rows | Distant islands showed dirt sides all the way down; the near mesh shows a white bowl |
| 170 | M7.6 | Generation records block states (`VxGenState`, up to 512 per chunk) that the main lane applies where the block still matches; player edits apply after and win | Doors, tables and wall torches need a rotation or wall side at generation time; chunk blocks carry no state byte |
| 171 | M7.6 | Townsfolk are `VX_CR_GUIDE` creatures whose variant is a `VxTownRole`; keepers stand at a table before their house and do not roam | One creature path keeps talk, waving and respawn shared; s_15–s_17 show traders at stalls along the street |
| 172 | M7.6 | Worn armour draws on the puppet (helmet, plate, greaves); gold armour is new, and a gold helmet carries a crown of self-lit rays | s_14–s_17: the player is gold from head to foot with a radiant crown |
| 173 | M7.6 | Wings at rest stay spread, edge 1.55 long, drooping 25°, eleven feathers; they fold only in water | The reference carries open drooping wings while walking; folded wings read as a small backpack |
| 174 | M7.7 | The armoury is a town lot kind whose ground floor is lined in grey brick and furnished at generation time | s_18–s_20: a grey brick room under a plank ceiling inside the town; a lining keeps the house's brick-and-plank outside |
| 175 | M7.7 | Armour stands and weapon plaques are entity geometry placed from the town plan (the armoury's floor row is planned with the town), drawn within 64 blocks | Stands need the posed puppet and the item art; neither is a block shape, and a fixed display needs no simulation |
| 176 | M7.7 | The puppet gains a closed visor with glowing slits, a glowing chest emblem and per-set wing colours | The reference sets show glowing eyes, emblems and pink, gold and ice wings |
| 177 | M7.7 | Keepers walk only straight back to their spot when more than 0.6 blocks off it | With zero roam the stroll logic sent them on jittered walks that carried them into the player's face |
| 178 | M7.8 | Banners are a cube block (`B_BANNER`) whose state holds the banner face (3 bits) and a painted layer (5 bits); the mesher picks that layer for that face only | One block id covers every design and orientation; the wall stays one opaque cube mesh with no extra geometry |
| 179 | M7.8 | A banner layer paints two banners side by side over the grey brick painting, each two blocks tall from an even row | Tiles span 2x2 blocks in world space: the column picks the banner, the row parity the half, and the brick continues across the wall |
| 180 | M7.8 | The banner wall is a town lot east of the main street with a cobble walk and torch pillars | s_21–s_24 show the hall beside the town on flat grass |
| 181 | M7.9 | Furniture state bit 64 (`VX_FURN_SHOW`) marks a showpiece; its material (one of 16) hashes from the set origin, so wood and cloth layers become `base + material` | Sixteen material sets with no new block ids; player-built furniture stays plain wood |
| 182 | M7.9 | Bed, bookcase, sofa, lamp, clock and piano are box-model furniture blocks like the chair and table; the lamp gives light | One mesh path, one break and place path, and the parts keep their rotation through the generation states |
| 183 | M7.9 | The museum is a town lot of back-to-back double rows: low pieces at the front, tall pieces behind, three-wide aisles | s_25–s_26 show rows packed edge to edge with narrow cobble aisles and torch posts |
| 184 | M7.10 | A pylon is a model block; its gem is entity geometry drawn from a per-chunk list of pylon cells (32 per chunk), rebuilt when the chunk generates and when an edit places or breaks a pylon | Quads are axis-aligned boxes, so a gem on its point cannot be meshed; a list kept by the chunk draws every pylon in range, placed by the town or the player, with no per-frame block scan |
| 185 | M7.10 | The gem is a jelly shell lit by the world round a self-lit core; colour from the block state | A self-lit shell read as a flat rhombus; the lit shell shows facets while the core glows through |
| 186 | M7.10 | A box face can be flagged local (quad bit 21): it samples one block's quarter of its tile, wherever the block stands | Sign lettering must not shift with the sign's world position; world-mapped faces show a different part of the tile in each block |
| 187 | M7.10 | Sign words are painted into texture variants in a 3x5 pixel font (two texels a pixel where a line fits); the state picks the word | Pixel lettering matches the reference's signboards and costs nothing at run time |
| 188 | M7.10 | The pylon avenue is a town lot on the cross street east of the main street, with the greenhouse on its left and a brick house closing it | s_27–s_29 show the avenue running past the greenhouse to a red-roofed house |
| 189 | M7.11 | Sand step faces use their own texture (`T_SAND_SIDE`): ochre, darker than the tops, with a dark lip under each block's top edge | s_30–s_32 read as contour lines on every dune step; one texture on top and sides read as flat noise |
| 190 | M7.11 | Butte and pillar walls are a new block (`B_BUTTE`, khaki sandstone, one course a block, one in three slots a red-brown hollow); `B_MESA` is the one-block red rim under the tan cap | The reference walls are darker than the sand and pitted with hollows a block wide, with red bands only at each lip |
| 191 | M7.11 | The great butte is two equal drums (r 24 and 19.5, heights 21 and 44) | s_30–s_32 show two stacked drums of about equal height; ours had a 12-block foot under a 42-block tower |
| 192 | M7.11 | A sand cone banks every butte and pillar: height k²·h1·(0.4 butte, 0.25 pillar), k falling to 0 over 0.8 r (butte) or 1.2 r (pillar) past the wall; it raises the ground without marking a landmark, so it stays sand | The reference dunes climb the foot of each rock |
| 193 | M7.11 | Desert cacti: 2–4 blocks with arms from 3; 60% of the desert flora rolls are cacti (was 22%) | s_30–s_32 are dotted with many short cacti |
| 194 | M7.11 | Desert palette, superseding 158: sand 0xFFF2C4→0xDCB274, cap 0xF6DCA6→0xC09458, rim 0xD8805A→0x7A3422, wall 0xC8B488→0x7A6646 | The S10 close shots are the true sand colour (median 238,213,147 in s_30); the paler S1 overview comes from the reference's distance haze, not from the sand |
| 195 | M7.12 | Snow landmarks: a peak (four faces on a seeded bearing, 0.2 round + 0.88 square falloff, slightly concave profile) and a crevasse (a bent crack pinched at its ends, its rim columns marked so their walls are pink ice) | s_33–s_35 centre on a steep crystal peak with a pink-walled crack in the snow before it |
| 196 | M7.12 | The peak is a per-block mosaic from two 3D noises and a block hash: snow above a threshold (lower on ledges), blue ice above another, pink ice where a second noise is high, otherwise stone or slate | The reference peak reads as single blocks of different materials, not one textured surface |
| 197 | M7.12 | Landmark placement respects every earlier landmark, so the crevasse sits on the far side of the peak's footprint (75 blocks away) | The probe ring rejects spots inside another landmark |
| 198 | M7.12 | Boreal pines: 12–18 tall, cone tiers from radius 2.5 down; density 0.1–0.4; needles 0x264C40→0x061614; ice ramp 0x9ED2F6→0x3A72BE | The reference pines are slim, near-black spires, scattered over open snow |
| 199 | M7.13 | The jungle terrace landmark is a round stepped cone: rise (1−e)(1.25−0.25(1−e))·h1, quantised to four-block courses, rings wobbled by low-frequency noise (0.07, ±0.06) | s_36–s_38 show a tall conical mountain ringed by evenly spaced grass ledges; the old ridge read as a low hill under the canopy |
| 200 | M7.13 | Slate 0x7C86A8→0x262A3E; jungle mahogany density 0.55 (was 0.75) | The reference courses are blue-grey, not near-black, and its canopy shows trunks between crowns |
| 201 | M7.14 | Shimmer is a liquid block marked on its quads by bit 23 (free for liquid quads); the liquid shader draws it as an emissive swirl with sparkles instead of refracted water | The quad flag field (6 bits) is full; a per-quad bit keeps the mesh format unchanged |
| 202 | M7.14 | The shimmer cave is a fixed pocket (centre, level, radius 46) under the central forest at y = VX_UNDERGROUND_Y − 6, carved by a post-pass like the hives; lumps are per-cell spheres subtracted from the dome | Surface heights there stay above the dome; the bearing farthest from the mushroom pockets keeps the two cave systems apart |
| 203 | M7.14 | Aetherium has no emissive glow; the cave is lit only by the shimmer's block light (13, 11, 15) | A glowing stone lit the far wall and the dome, hiding the black void between the lumps that defines s_39–s_41 |
| 204 | M7.14 | Liquid checks in creatures, loot and falling stars use vx_shape == VX_SHAPE_LIQUID | A fixed id list misses every new liquid |
| 205 | M7.15 | Giant mushrooms are sites on a 14-block grid inside a pocket's hollow (q < 0.7): the cap at a hashed height, the stem built per column downward through air (or pots and glowing mushrooms) to the floor | The old in-chunk giant mushroom was limited to a 4-block cap; the reference caps span a dozen blocks over thick stems |
| 206 | M7.15 | Mushroom-zone light: grass (4, 6, 9), glowing mushrooms (7, 10, 13), caps (9, 11, 14); grass top is dark mycelium (0x34406A→0x0A0E1E) with glowing spores | Saturated blue light turned the gold armour green; the reference floor is near black with blue specks |
| 207 | M7.16 | A `vx_shaft:r bottom` command digs a ragged shaft under the player (per-layer radius r + noise·0.9, flaring 0.7 a block over the top 5 blocks) | S15 is a player digging straight down; the scene needs the shaft the player dug, not a natural cave |
| 208 | M7.16 | Stone palette 0xACA49E→0x5A534E (was cool 0xA4A6B2→0x55576A) | The s_47 shaft wall measures (66, 57, 52), which is warm; ours measured (55, 56, 76), which is blue |
| 209 | M7.17 | Ruined towers: obsidian brick body on a hellstone-brick grid (k%5 rows, corner and middle posts); hellstone brick is red-hot brick that glows whole (emit 0.85, light 9,2,1); obsidian crumbles at 0.8, the frame at 0.4, from height+2 | s_49–s_51 show dark purple brick framed by glowing red bands; the distant ruins are bare red grids |
| 210 | M7.17 | Underworld floor: 16.5 + fbm·4 with lava rivers where |noise(0.012)| < 0.05 (floor at VX_LAVA_Y − 2); the top two blocks are hellstone where patch noise(0.18) > 0 | The reference floor is flat ash/purple mosaic with narrow lava rivers flush with it; ours was mostly lava sea and ash hills |
| 211 | M7.17 | Hellstone texture: purple rock (0x7A5A86→0x2A1A34) with glowing cracks only in sparse blots of fine 16×16 cells; light (5, 2, 1) | The reference cracks are small scattered spots; glowing every joint lit the whole floor red and read as glyphs |
| 212 | M7.17 | Underworld look: fog 0x4A1408 at density 0.018, under-glow 0xFFA888 × 0.7 (floors 0.45, walls 0.65), gain 0xFFE4E4; lava light (15, 9, 3) | Measured: the s_50 tower is (39,10,15) and the s_49 floor (62,26,28), both with blue; ours were orange with zero blue |
| 213 | M7.17 | The far terrain and the sea are not drawn when the camera is in the underworld (underworld weight > 0.5) | The surface far field can never be seen from under the underworld's roof; drawn, it leaked through the haze as a pale band |
| 214 | M7.17 | The ruin site stands on dry ash: it searches along z for a column whose floor is above the lava | With rivers, the fixed y = 30 spot dropped the player into lava |
| 215 | M7.17 | Lava falls in 18% of 40-block cells (was 43%) | The reference shows no falls; eight in one view read as a curtain |
| 216 | M7.18 | Dungeon layout: a grid of halls 7 wide and 7 high, pitch 22, floor at y 92, across ±44 blocks; a shaft from the central crossing to the ground; site kind 8 sits 11 blocks down a hall from the crossing | s_52–s_54 show long straight brick halls with torches on posts; a site near the shaft let daylight wash the bricks out |
| 217 | M7.18 | The dungeon is placed opposite the cabin (angle + π) at radius 120 | It keeps it away from the other sites and the spawn |
| 218 | M7.18 | New blocks: dungeon brick (teal stack bond 0x2C6E5A→0x0A2420, joints 0x061210, a diamond-panel variant), cobweb (cross, cutout), wood post (model); posts carry a torch every 6 blocks, gold chests and pots line the walls | Measured against the s_52 walls and props |
| 219 | M7.18 | The light-volume header and data buffers are created once at their per-frame budget size and never regrow | The data buffer grew mid-frame on the first heavy upload; on WebGPU single-threaded that frame's sections (the ones nearest the camera) never reached the texture, so they kept no light and read teal. The CPU slot records matched what the world wanted (probed), and a teleport away and back cleared it |
| 220 | M7.19 | The Empress of Light is a new boss (5500 life, 40 contact damage, 30 defense), drawn 1.5× its 4.6-block art | s_55–s_59: she fills about a third of the frame from the skybridge |
| 221 | M7.19 | Her wings are stained glass: teardrop panels of 0.12-block glow cells laid along each wing's own axis, with a leaded rim, two curved ribs and a cross vein; hue runs root cyan → gold → pink (upper) or gold (lower) | Round fans and polar ribs read as a dotted rainbow blob; axis-aligned cells give clean lead lines like the reference's panels |
| 222 | M7.19 | Attack cycle: bolt fan (30 bolts/s for 1.3 s, golden-angle ring facing the player, homing after 0.45 s), crescents (3, or 5 in phase 2), bolt fan, charge; phase 2 below half life | The reference shows fans of prismatic bolts and thrown crescents |
| 223 | M7.19 | Boss projectiles are a new hostile kind in the combat list (cap raised to 256): through blocks, damage minus half defense, short thin trails | Long, thick trails formed ribbons that hid the fan |
| 224 | M7.19 | The Empress comes on the first dusk over the Hallow once the Eye is beaten; `vx_boss:kind=empress` summons her for scripts | A natural trigger in the game's own progression, without a new summoning item |
| 225 | M7.19 | Wooden beams (X and Z) reuse the copper beam models with plank textures; `vx_frame` builds rails every 2 blocks, cross beams every 4 and posts every 6 | s_55's open timber frame |
| 226 | M7.19 | Sky: a low moon (elevation under 0.5) adds a halo, upward rays and a peach horizon band; a 0.53 dusk key (zenith 0x3E3450, horizon 0xA8786E, warm direct 0xFFC08A × 0.45); the Empress carries a 28-block point light | Measured: the s_55 sky is (100,80,90) with a (205,149,126) horizon at the disc, and its beams and player catch warm light |
| 227 | M7.20 | A white keep (new kind `VX_TP_WHITE_KEEP`, x 47–56, z −8–1) closes the pylon avenue's axis; a brick house (x 44–56, z 2–13) flanks it; a dark keep (x 37–45, z −27 to −18) fills the lot north of the cross street | s_60–s_62: the pylons lead between a brick and a wooden house to a white keep with dark bands; our avenue ended in an open lawn. S5's keep is dark navy, so the white one is a second kind, not a change to it |
| 228 | M7.20 | New Light Box block (cube: steel rim, lit seam, glowing pale-cyan top, light 10/13/15) every 4 blocks along the walk's road edge | s_60 and s_27 show a second row of glowing grey boxes beside the pylons |
| 229 | M7.20 | The 0.44 and 0.50 sky keys are darker (0.44: direct ×0.42, ambient ×0.3, ground ×0.16, zenith 0x1A3C94, horizon 0x8C9CC0; 0.50: direct ×0.4, ambient ×0.32, ground ×0.18) | Measured: s_60 was about 1.5× darker than ours. Auto exposure absorbs small key changes, so the dusk light had to fall below the exposure ceiling. S18/S20 sit on unchanged keys, and S9 at 0.3 was recaptured unchanged |
| 230 | M7.20 | The S19 camera is 8 blocks over the avenue's west half, pitch −0.55, looking at the keep, with the sun out of frame | The reference shows no sun; seen through gaps in the voxel clouds, the sun read as a stepped patch |
| 231 | M7.21 | The title camera hangs 20 blocks over the avenue (10 blocks east of its centre), aiming 16 blocks past the centre; the orbit round the spawn remains for a world without a town | s_63–s_65: the avenue under the band, the keep and the forest above it |
| 232 | M7.21 | The title is lit at 0.56 instead of 0.472 | The reference title is early night with lamps lit; 0.5–0.53 looked west into the sunset and washed orange |
| 233 | M7.21 | The title menu is one row floating at the bottom edge, outside the band | The reference band carries only the logo and subtitle over the town |
| 234 | M7.2 | Aerial perspective past 80 blocks (density 0.004) toward a blend of the horizon and zenith colours, at any height; a daylight key at 0.40 so the darker 0.44 dusk key no longer dims mid-afternoon | s_01: the island fades into a clear blue over distance; the height-limited fog left our far land unhazed. S1 at 0.36 had dimmed after decision 229 |
| 235 | M7.2 | A second desert pass scatters small one- and two-block cacti (3% of sand columns); the cactus ramp is darker (0x2C6028 to 0x0A2412) | s_01 and s_31 show many small dark cacti between the saguaros |
| 236 | M3.13 | The outline is 3 px at 720p (res.y/270, 4 px at 1080p, up to 8), on silhouettes and creases alike | Measured on s_14 and s_18: the line round the player, the wings and the armour is 3–4 px at 720p and about 0.4× the surface colour; ours was 1–2 px at 1080p and vanished on characters and props |
| 237 | M5.4 | Phoenix wings: roots a saturated orange (1.0, 0.32, 0.0), coverts and edge darker; feather tips and the gold crown's rays use a new box flag `VX_BOX_HOT_GLOW` (16 → g-buffer flag 256), which writes the full emission range (4.0) so they bloom | Measured s_14/s_16: tips clip to yellow (255, 219–241, 71–122) with a halo, roots (149–190, 83–95, 3–18); ours had mustard roots (183, 126, 0) and unlit tips. Self-lit's fixed 2.5 left only a faint halo. After: tips (255, 230, 98), roots (184, 106, 0) |

### Failures

| # | What went wrong | Cost | Rule from now on |
|---|---|---|---|
| 1 | The video was reduced to 18 stills early and visual steps were marked done without comparing against them; the result reads as Minecraft with small voxels, not the video | M1–M3 look work does not match the reference | §2.1: all frames are the spec; every visual step ships a side-by-side |
| 2 | Hours went into making a 400-command input script replay reliably (command-line limits, real mouse input stealing scripted holds) | Time and money with no visible progress | Test scripts stay short; flakiness gets minutes, not hours |
| 3 | Silhouette outlines were in the art direction but no step owned them | Missing in every screenshot | Every §4–§5 look item maps to a step (M3.13, M7.1) |
| 4 | M7.2: a scripted edit sliced the wrong `main()` and corrupted `vx_clouds.glsl`; `git checkout` then restored a stale staged version over hours of uncommitted work | The voxel-cloud shader had to be rewritten from an earlier printout | Never checkout/restore files in the game repo; back up before scripted edits, and assert each replaced string is unique |

### Engine changes

Changes to the engine worktree (`src/engine`, `src/lib`, `src/os`, engine shaders, build tools), newest last.

| # | Step | Change | Reason | Tests run |
|---|---|---|---|---|
| 1 | M1.12 | `src/os/wasm/gpu_webgpu.ts` `gpuFormatBytesPerPixel`: `rg8unorm` is 2 bytes and `rgba16unorm` 8 (both fell through to the default 4) | `gpu_make_texture_mips` with an RG8 array (the emissive tile array) computed a row pitch twice the data size, giving 15 WebGPU validation errors and no emissive texture on wasm | wasm P1–P6 postcard runs: 0 validation errors; no automated engine test covers the WebGPU texture upload path |
| 2 | M2.13 | `src/engine/gpu/gpu_webgpu.c` `gpu_update_buffer`, `GPU_BUFFER_USAGE_CPU_WRITE` path: the data is staged into every frame slot first, and the JS writes run only once the buffer exists; while its create is still queued (a recreate behind a queued destroy of the same handle index) the queued create seeds the slots from staging | A buffer destroyed and recreated larger in the same frame reused the handle index, so the immediate JS write went into the old, smaller buffer (10 validation errors in the P8 wasm run: "Write range ... does not fit in vx light data (262144)") | `hz_tests: hzbuild wasm test run --filter buffer` 29/29 pass; `--filter gpu`: all pass except `test_gpu_taau` (5.33% diff), which fails with the identical pixel count with this change reverted, so it predates it; P1–P10 wasm postcards 0 validation errors |
| 3 | M3.12 | `src/entrypoint/hz_engine.c`: `hz_touch`, `hz_key_event`, `hz_char_event`, `hz_scroll_event` and `hz_mouse_move` drop platform input while a `--commands` script is active (as `hz_set_mouse_pos` already did) | Real mouse clicks and motion on the focused window released scripted button holds and turned the camera, so scripted runs depended on who touched the machine | Verified at M7.21. Scripted `key_down`/`touch_*` actions write into the frame events directly, not through these entry points, so the gate drops only platform events. A scripted W hold moves the player (`out/m721/keys.png`). `hz_tests: hzbuild test run --filter input` 10/10, `--filter command` 2/2 |
| 4 | M6.2 | `src/engine/input.c`: a TOUCH_END no longer clears `started_this_frame`, and `touch_find_slot` skips a touch already lifted this frame | A tap pressed and lifted inside one frame (a slow frame, or a quick tap) was never seen as started, so buttons missed it; and a reused touch id in the same frame matched the finished slot, leaving the new touch stuck down for the rest of the run | `hz_tests: hzbuild test run --filter input` 10/10 pass; the M6.2 native touch run now registers every tap, including two taps that landed in one frame after a screenshot stall |
| 5 | M6.3 | `src/engine/assets/assets.h` `asset_load_cb` passes `.type_name = #T` like `asset_load`; `src/engine/assets/asset_loaders.c` marks `UIFontBlobAsset` compressed and types the font family's dependency entries; `src/exporter/exporter.c` `is_compressible_hza` adds `UIFontBlobAsset`; `build/hzbuild.c` wasm publish `_headers` adds `/cooked/*.br` with `Content-Encoding: br` | Publish builds fetched every font page and index uncompressed (boot 15.75 MB raw), and on the real host the `.br` assets would have arrived still encoded | `hz_tests: hzbuild test run --filter asset` 20/20, `--filter font` 13/13; both publish builds boot into play with fonts drawn and only the `.br` font files requested |

## 14. Outside this plan

These need the user and are not steps:
- Performance benchmarks against the §5.8 targets, once the machine is ready.
- Testing on real mobile devices. The steps only verify the single-threaded build in the desktop browser harness.
- A trademark check on the name before going public.
- Engine owner: on WebGPU single-threaded, a `GPU_BUFFER_USAGE_CPU_WRITE` buffer destroyed and recreated larger mid-frame still lost that frame's `gpu_update_buffer` data for the following dispatch, despite engine change 2. The game no longer regrows that buffer (decision 219); the engine path was not root-caused.
- The Tier B backlog: King Slime, Merchant and housing, blood moon, Hallow, shimmer lake, pyramid, saves.
