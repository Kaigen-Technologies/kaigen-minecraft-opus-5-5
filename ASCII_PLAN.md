# WebCraft ASCII: design

Branch `ascii` of this project, from `7d37d2f` (the Sep 24 release). The engine stays at `176704ae5`, with no engine changes planned.

The brief, in the owner's words:

> "the first and the most beautiful ASCII version of minecraft in the world, high quality ASCII visuals, clearly readable as minecraft"
>
> "assume ASCII is the end renderer mode"
>
> "the UI then needs to be converted to native ASCII, everything is ASCII"

## 1. Research: what exists and what we take from it

| Reference | How it works | What we take |
|---|---|---|
| **ASCII CITY**, Grow Now! Games (viral, Aug–Sep 2026) | One ray per character cell. Each glyph comes from the surface material and where the ray hit it, not from converting an image. Saturated colours on black; near is bright and far fades out. | Glyphs tied to the world's materials. Black background. |
| **Alex Harri**, "ASCII characters are not pixels" | Six sample circles per cell make a "shape vector", matched to the nearest of the 95 printable glyphs. Ten circles outside the cell sharpen edges (exponent 7), plus a global contrast curve (exponent 3). | The shape matcher, for edges, silhouettes, sky and clouds. |
| **Codrops**, Lunardi, Sep 2026 | The same matcher on the GPU: one fragment per cell and 7-tap disc samples. Glyph vectors are normalised per sample point. The scene target is sized from the cell grid, and the glyph index rides in the alpha channel. | Pass layout and cost model. Per-point vs global normalisation gets tested. |
| **Acerola**, `AcerolaFX_ASCII.fx` | Difference-of-Gaussians plus Sobel edges, voted per 8×8 tile, and depth/normal edges. A 10-level fill ramp. | Geometry edges from the g-buffer. Brightness alone misses green-on-green silhouettes. |
| **"Making the ASCII MMORPG of my dreams"** (the owner's reference videos: boss fight, water reflection, spell test) | Read frame by frame in section 2a. The matcher runs over the whole lit image, on black. Ink coverage follows brightness, and texture never reaches the matcher below cell size. | **The target look.** Section 2a is the model this design now follows. |
| **Roguelikes** (Dwarf Fortress, Brogue, NetHack, Cogmind) | Fixed glyph meanings: `"` grass, `#` rock, `~` water, `&` foliage, `*` ore. | The material vocabulary, readable at a glance. |

**We are not first.** Minecraft ASCII mods already exist:

- BouncingElf10's "ASCII Shader" (Iris/OptiFine, 22.7k downloads);
- "Ascii Text Renderer" (vanilla core shaders, 8.3k downloads);
- 5weetdev's ASCII shader;
- a texture pack that paints characters onto block textures.

The shaders are post-filters: they convert Minecraft's finished image by brightness, sometimes with edge glyphs. They keep the normal UI, and their own galleries look dim and noisy.

Being a game rather than a filter lets us do what a filter can't:

- know what every cell is made of;
- render a true text grid;
- use the Minecraft typeface;
- convert the whole UI to ASCII.

The bar is "the most beautiful", not "the first".

## 2. The look

### Font: Monocraft

Monocraft (Idrees Hassan, OFL-1.1, no reserved font name) is the Minecraft UI typeface redrawn as a monospace font.

- **Glyphs:** all 95 printable ASCII glyphs are 5×7 pixel art in a 6×9 cell. Eight of them (`; @ _ g j p q y`) are 8 tall, with a 1-pixel descender.
- **Source:** the atlas is built from Monocraft's source pixel data (`src/characters.json`), not from a TTF. That makes it pixel-exact, with no MSDF and no font runtime.
- **Licence:** OFL text ships next to the glyph table. The menu credits "Font: Monocraft by Idrees Hassan".

### One character grid

The whole screen is one grid of 6×9 cells.

- **Scale:** each font pixel is a whole number of device pixels, so glyphs are always crisp.
  - Desktop default: about 2 CSS px per font pixel, which is 160×60 cells at 1080p.
  - Phones: finer, about 100 columns.
  - A setting changes it (Large / Medium / Small).
- **DPR:** the game raises its DPR cap so the grid lands on device pixels. The 3D scene's cost depends on the grid, not on the screen resolution (section 3).

### 2a. How the reference renders (measured from the owner's videos)

The owner's reference is Ansenjeo's "Making the ASCII MMORPG of my dreams" (JavaScript, solo, unreleased).

The technique is not published anywhere:

- X is paywalled.
- TikTok and YouTube expose no captions.
- The only public hint is a third-party remark that "a converter takes the shader and translates it into ASCII".

Everything below was measured from the three videos: frames extracted with ffmpeg, numbers from numpy over regions, glyphs read in 6× crops.

1. **The fill is a brightness → density ramp, not a shape matcher.**
   - On one castle wall:
     - lit faces are `@ # $ M W & Q % 8`;
     - the shaded window recess is `r c s z x ( | \`;
     - the darkest parts are `. ' -` or empty.
   - The lit stone path is `Q 0 8 @ d a c`.
   - Every fill glyph seen is on the classic 70-step density ramp `$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\|()1{}[]?-_+~<>i!lI;:,"^'.`.
   - Because each cell takes its mean brightness:
     - texture noise below cell size averages out;
     - lighting shows as density;
     - neighbouring cells of slightly different brightness pick neighbouring ramp glyphs, which is the "texture" of a surface.
2. **Edges are oriented strokes.** Water ripple rings are drawn as `( ) / \ ~ -` following the ring; silhouettes are strokes. This is an edge pass on top of the fill, as in Acerola's ASCII shader.
3. **Colour: the scene's own colour, on a dark background tinted by the surface.**
   - Empty floor is a constant dark navy, rgb (0.035, 0.035, 0.077).
   - On surfaces, the gap between glyphs carries the surface colour, about 0.27× the glyph:
     - dragon gap (0.096, 0.034, 0.06) under glyphs (0.35, 0.23, 0.25);
     - grass gap (0.04, 0.07, 0.03).
   - The image is dark overall: median luma 0.05–0.2, brightest glyph pixels about 0.3–0.45. Brightness is never normalised.
4. **Figure and ground separate by value.** A lit object is dense and bright; its shadow and its shaded side are sparse and dim; hue names the material (the dragon is dim red, the grass bright green).
5. **Glyphs stay fixed to the world.** Consecutive frames during a pan show the same pattern shifted by whole cells, because the top-down orthographic camera moves in cell steps. A first-person camera cannot snap, so here per-cell hysteresis does that job.
6. **Characters use a finer grid** (the ogre is about half the environment's glyph pitch), so they read as solid figures.
7. **Effects sit on top, unquantised:** fire glow, spells and lightning are smooth halos and lines over the glyphs.
8. **The UI is monospace text in box-drawn panels.**

**Why our builds failed:**

- **Milestone 2:** Harri's shape matcher read the sub-cell noise of 16×16 block textures, so every cell got a random glyph.
- **Milestone 3:**
  - It removed the noise by removing the light: one glyph per block, ink lifted to one brightness, and a 45% background.
  - Every surface became one uniform wall.
- **The 2b prototype:** kept the shape matcher, so it still chased sub-cell structure. The owner judged it worse.

### 2b. The look (as specified from 2a)

1. **Fill: a density ramp over each cell's mean brightness.**
   - *Built:* the ramp is the 10-step undirected density ramp `` .:-=+*#%@`` (`WC_ASCII_FILL`), ordered by measured ink coverage.
     - All 95 glyphs were tried first (`out/r_53.png`, `out/r_54.png`): mid-tone cells became letter soup, because letters and digits read as words.
     - The 33-symbol ramp came next and was rejected after the owner's review ("I can't tell things apart"): 95% of cells are fill cells, and neighbouring luma levels mapped to unrelated directional shapes (`" ; _ , / ( \`). Texture variation then turned every surface into the same stroke soup, which read as false edges (`out/look_forest.png`). With 10 ordered steps, a lit face stays one glyph and shading moves it a step.
   - *Built:* brightness is the cell's integer mean display luma (`luma8`), mapped linearly from `WC_FILL_LO` 12 (empty) to `WC_FILL_HI` 150 (densest).
     - Both points come from the measured fill luma distribution after the contrast change (5th percentile 11–14, 95th percentile 129–147).
   - Hysteresis: a cell keeps last frame's glyph while its ramp position stays within `WC_RAMP_HYSTERESIS` (1) step. This stands in for 2a.5.
2. **Edges: silhouette strokes from the existing integer shape matcher** (`_ - | / \ . , '` and backquote), on the 1 px outline image. A blank stroke match falls through to the fill.
3. **No silhouette gap.** An empty band behind every nearer surface was tried and removed: it is a fake outline. Objects separate by their own light and colour (6), not by drawn rings.
4. **Contrast for ascii** (owner: "we need more contrast in the renderer"):
   - sky fill light in deferred × `ASCII_AMBIENT` 0.55, so lit and shaded faces land far apart on the ramp;
   - AgX look: saturation 1.3, contrast power 1.85. It was 1.6, raised after the owner asked for more contrast once surfaces read (`out/contrast_ab.png`, before left, after right).
5. **Textures are filtered over a cell, not a pixel** (the g-buffer's texture gradients are scaled to the cell). Cell means are then stable as the camera moves, and near blocks whose texels span cells still show their pattern as density (2a.1, the stone path).
6. **Colour:**
   - Glyph: the cell's mean colour × `WC_INK_GAIN` 2.5, reduced only so no channel clips.
   - Background: navy (0.035, 0.035, 0.077) plus the glyph colour × the background tint setting: Black 0, Faint 0.25, Full 0.38 (default Full; 0.45 at first, lowered so glyphs separate more from their cells).
     - *Why the background carries the colour:* a glyph covers only 5–45% of its cell, so on a dark background a cell shows a fraction of the scene's brightness. At tint 0.15 the bright, readable shaded scene (`out/look_forest2_ldr.png`) became dim specks on black, and near and far could not be told apart (`out/look_forest2.png`). With the colour in the background and brighter ink over it, faces read as lit planes, top and side faces differ, and the glyphs still carry contrast (`out/look_forest4.png`).
   - No lift to a fixed brightness.
7. **Sky:**
   - its colour as a smooth background;
   - clouds through the cloud matcher;
   - stars and the square sun and moon, as built in milestone 4.
8. **Water:** the same fill and strokes as land. Ice is a surface like any other. Bubbles, rain, snow and lightning overlays as built.
9. **Glow:** emissives add a smooth halo after the glyphs (2a.7), as built in milestone 4.
10. **The held item is the player on screen** (owner: "it has to be *the thing* that stands out"):
    - The g-buffer marks it (material alpha | 128).
    - A cell where at least 4 of its 7 sample points hit it gets `FLAG_HAND`: ink lifted to full brightness, background 0.75× its colour, no edge strokes, and its luma pushed into the ramp's dense half (`96 + luma × 5/8`). It reads as the brightest, densest block on screen (`out/h_sheet.png`).
    - WebCraft has no third-person body; the hand and held block are all the player sees of themselves.

**Removed:**

- per-block signature glyphs;
- glyph maps;
- the land shape matcher;
- the material/liquid cell kinds;
- the footprint target.

## 3. Frame pipeline

**Today:**

> cull → sky lookup tables → shadows → g-buffer → SSAO, clouds → deferred → water → atmospherics → TAA → bloom and exposure → `final` (AgX, crosshair, vignette…) → swapchain → Clay UI

**ASCII:**

1. **3D passes at grid resolution.** The 3D passes are unchanged, but the scene renders at grid resolution: (6·cols)×(9·rows), one scene pixel per font pixel. A 160×60 grid gives 960×540. `render_scale` remains a TAA upscale for slow devices.
2. **Material target.**
   - The g-buffer gains two targets:
     - RGBA8: texture layer + 1, face u, face v (in 1/256 steps), face;
     - RG8: glyph footprint level along u + 5 × level along v, map variant.
   - The scene target gains an RGBA8 liquid attachment. The water pass writes kind (1 water, 2 ice), u, v and face (+8 when seen from below); deferred clears it.
3. **`tonemap`.** The old `final` pass, minus crosshair, vignette, dither and sharpen, writes the LDR scene at grid resolution. *Revised (2b):* the g-buffer samples albedo with an LOD bias, so a texel is never smaller than a cell, and the match image carries the silhouette rim.
   - rgb is the display colour; alpha is the silhouette outline (1 on the near side of a silhouette, else 0).
   - TAA keeps running without subpixel jitter, since cells sample surfaces at fixed points.
4. **`ascii_cells`.** One fragment per cell writes two small RGBA8 targets: glyph plus foreground colour, and background colour plus flags. Last frame's cells feed the hysteresis.
5. **`ascii_compose`.** Draws to the swapchain at device resolution. For each pixel it finds the cell (UI grid first, then scene grid) and reads the glyph's bit from the Monocraft atlas with `texelFetch`. It then mixes foreground over background and adds the glow.
   - The UI is a small RGBA8 cell texture uploaded each frame (`gpu_update_texture_region`).

## 4. Cell classifier (as specified in 2b)

For each cell (`shaders/wc_ascii_cells.glsl`):

1. **Read the cell:** from the 54 LDR pixels, the mean colour and mean luma, the outline pixel count and the cloud cover. Then the surface at the 7 sample points.
2. **Sky:** all 7 points are sky; the sky path of 2b.7.
3. **Edge:** outline pixels, and the majority surface is not cutout foliage or a plant. The integer shape matcher runs on the outline image over the stroke vocabulary. A blank match falls through.
4. **Fill:** the ramp glyph for the mean luma, with hysteresis.
5. **Colour:** 2b.6. The selected block's outline cells (its cube's silhouette, by ray-box test) swap ink and background in compose.
6. **Check:** `tools/ascii/reference.py` re-derives every edge, cloud and fill glyph from the dumped targets, and must match exactly.

## 5. UI: native text mode

`wc_ui.c` is rewritten on a new `wc_tui.c`: immediate-mode widgets on a character grid, hit-tested in cells. Clay, the MSDF Inter font, the isometric pixel icons and the scrim texture are no longer used.

- **UI grid:** the scene grid × an integer m, so UI glyphs line up with scene cells. m is picked for readable text (capital height ≥ ~12 CSS px).
- **Logo:** "WEBCRAFT" in Monocraft letters scaled to one cell per pixel. Each letter is filled with stone glyphs in greys, with a darker extruded shadow, the way the Minecraft logo looks.
- **Widgets:**
  - buttons `[ Play ]` (Minecraft's yellow text on hover);
  - sliders `[######|.......] 12 ch`;
  - toggles `[x]`;
  - cycle buttons `< Fancy >` instead of dropdown popups;
  - a scrollable settings list;
  - a "New World" confirm box.
- **Hotbar and inventory icons:** each icon is a 3×2-glyph block. The top row is the block's top glyph in its top colour; the bottom row is its side glyph in the side colour, so it reads as a little block.
  - *Built:* each icon cell is a third of the face's texture, averaged over its opaque texels and run through the scene's fill ramp. Icons are coloured like scene cells: ink at gain 2.5, background at 0.38 of the colour. With ink on black, the icons were unreadable (`out/m5_hotbar_crop.png`). A mostly see-through patch, such as a flower's edge, stays empty, and a plant's two rows are its upper and lower halves. Each icon is built once at init (`wc_icons_build`).

  ```
  .---.---.---.---.---.---.---.---.---.
  |"""|@@@|===|OOO|/ /|===| i |***|~~~|
  |%%%|@@@|===||||/ /|===| ! |***|~~~|
  '---'---'---'---'---'---'---'---'---'
  ```

- **HUD and overlays:** crosshair `+`; the targeted block's cells highlighted (text-mode inverse video); toasts; the stats panel; F3 debug text; the loading bar `[##########..........] 47%`.
- **Touch:** controls are ASCII. The stick is a ring of glyphs with an `O` knob, plus `( JUMP )` and `( SNEAK )`. Hit-testing works as today, against the cell rects.
- **Scripted runs:** `wc_ui_move_to:<name>[#<index>]` puts the pointer on a named widget, from last frame's layout. The engine's `touch_move_to` only finds Clay elements. The command moves the mouse only, plus the finger in touch mode: a touch event would switch the game into touch mode.
- *Built — data flow:*
  - The UI lays out first each frame, on the grid that frame renders. Its cells (8 bytes each: fg rgb plus glyph, then bg rgb plus opacity) are uploaded once per frame into an RGBA8 texture two texels per cell wide.
  - Compose blends the UI background by its opacity over the scene and draws UI glyphs opaque. The palette applies after, so phosphor and amber tint the UI too.
  - While the scene pipelines still compile, compose draws the UI alone, so the loading screen shows from the first frames.
  - HUD panels are 235/255 opaque, so scene glyphs never show through text.
  - The inventory marks the hovered item with yellow `[ ]` in the gap columns.
  - The background tint setting is now honoured; it was hard-coded before.
  - `wc_mode:error` reaches the error screen, which nothing in the game enters.
- **Unchanged:** the HTML "Made with Kaigen" corner keeps its clearance.

## 6. Performance

- **3D scene:** renders at grid resolution, 960×540 at 1080p instead of 1920×1080, so most GPU passes shrink about 4×. This is an expectation; it gets measured.
- **ASCII passes:**
  - cells: about 10k cells × 112 taps, plus a 95×6 glyph search;
  - compose: 2 fetches per pixel.
- **UI:** CPU cost is negligible.
- **Measurement:** the existing bench, D3D12 and wasm, once each.

## 7. How we know it's right

These checks are algorithmic, not eyeballed by the AI:

1. **Atlas test.** A debug view draws all 95 glyphs, decoded back and compared pixel-exact to `characters.json`.
   - Run: `tools/ascii/atlas_test.py <png> k= ox= oy= cols=` on a `wc_view:atlas=1` screenshot.
2. **CPU reference.** `tools/ascii/reference.py out/ascii_<n> cols= rows=` re-runs the matcher on the LDR and cell targets dumped by the `wc_dump:<n>` command (`gpu_queue_texture_read`).
   - The reference is written independently of the C tables.
   - Glyph choices must match the GPU exactly, including edge cells whose match was blank (flag bit 4).
3. **Synthetic inputs** (`tools/ascii/synthetic.py`) with known answers:
   - lines at 0°, 45°, 90° and 135° give the line glyphs, with the edge vocabulary and with all 95 glyphs;
   - a gradient gives steadily increasing ink;
   - a flat colour gives one glyph;
   - steps on black give an edge glyph.
4. **Stability.** `reference.py` counts glyph changes per frame, per cell kind:
   - camera still: only water, clouds and leaves may change;
   - fixed pan: material cells must change less than edge cells.

**Visual, judged by the owner:** a fixed shot list at both grid sizes:

- plains at noon, forest, desert, snowy taiga, ocean;
- sunset, night, a cave with torches, rain, underwater;
- menu, settings, inventory, touch HUD.

The owner picks the defaults: background tint, density and glow.

## 8. Milestones

1. **[done] Plumbing:** grid, Monocraft atlas, `tonemap`/`ascii_cells`/`ascii_compose` with a plain brightness ramp. Check: atlas test and a screenshot.
2. **[done] Shape matcher everywhere:** plus CPU reference, synthetic tests, and stability numbers. Screenshots against milestone 1.
3. **[done] Surfaces (redone per 2b).**
   - First version: per-block glyphs, which the owner rejected.
   - Built:
     - the 10-step density ramp over the cell mean;
     - silhouette strokes;
     - colour carried by the background (2b.6);
     - cell-filtered textures;
     - the ascii contrast grade.
   - Removed:
     - glyph maps and per-block signature glyphs;
     - the footprint target;
     - the land shape matcher.
   - Check: the CPU reference matches the GPU in every cell (`out/ascii_59`, `out/ascii_60`).
4. **[done] Sky, liquids, weather, selection:** stars, sun and moon, clouds; water/lava; rain `| /` and snow `* .`; lightning; underwater; selection, crosshair, glow and tint.
   - Changed while verifying:
     - **Night exposure:** the ascii exposure ceiling went from 11 to `WC_MAX_EXPOSURE` 64. At 11, a moonlit scene sat below the ramp's black point and rendered empty (2b.4).
     - **Selection:** the minecraft outline, not inverse video over the whole block.
       - A cell is outlined when its centre view ray hits the selected cube unoccluded and a 4-neighbour's ray misses it.
       - Inverse video over a near leaf block filled a third of the screen.
   - Check:
     - the CPU reference matches the GPU in every cell of `out/ascii_101`…`111`;
     - shots `out/shot_m4_{night,rain,snow,lightning,underwater,lava,select,palette1,palette2,ocean,sunset}.png`.
5. **[done] Text-mode UI**, with Clay removed. Check: a scripted click-through of every screen and widget, plus a screenshot of each.
   - Mouse path, 48 steps (`out/m5_*.png`):
     - the menu and its hover;
     - settings: every widget kind — toggles, cycles both ways, a slider, the preset, text size, palette, tint, glow;
     - the new-world box: cancel and confirm;
     - play, the HUD, F3, the inventory pick and its toast, pause.
   - Touch path, 28 steps (`out/t5_*.png`): the touch menu, then the HUD with its stick, a hotbar slot, sneak, jump, the inventory and Done, pause, settings, resume.
   - Short canvas 1280×520 (`out/s5_*.png`): the compact menu, settings scrolling by arrow and by wheel.
   - `out/e5_error.png` (the error screen) and `out/r5_rotate.png` (the portrait rotate prompt).
   - One of four full mouse runs missed a click; the next three passed. The cause is unproven (a physical mouse moving over the window overrides injected positions).
6. **[skipped by the owner] Performance:** bench on D3D12 and wasm. Drop passes that no longer show at cell size (sharpen, vignette, dither; SSR and shafts are evaluated).
   - The owner stopped all performance work on 2026-10-19 because the machine was not in a state to benchmark. No bench ran, and SSR and light shafts were not evaluated.
   - Sharpen, vignette and dither are already gone: `final` became `tonemap` in milestone 1. A grep of the tonemap and ascii shaders and `wc_render.c` finds none of them.
   - The only data is the debug HUD's GPU readout: about 1.7–2.0 ms at 1080p native debug with other GPU apps running. It is not a bench result.
7. **[awaiting owner] Polish** with the owner.

**Checks run at the end (2026-10-19):**
- Atlas test: 95/95 glyphs exact (`out/atlas_final.png`).
- Synthetic tests: 15 passed, 0 failed.
- CPU reference: exact in all 20 shot-list views at both grid sizes (160×56 and 106×37).
- Stability, camera still: 0–0.45% of cells change glyph. The changes sit in waving leaves and plants (`out/forest_changes.png`).
- Shot list:
  - 160×56 (text Medium): `out/shot_g1_*.png`.
  - 106×37 (text Large): `out/shot_g0_*.png`.
  - UI: `out/ui{1,0}_{menu,settings,inventory,touchhud}.png`.
- Shot-list helpers: `wc_find:<biome name>` moves the player into a biome; `wc_view:text=<0..2>` sets the text size.

## 9. Risks

- **First-person stability.** The reference never moves except in cell steps; a ramp changes a glyph whenever a cell's mean brightness crosses a step. Mitigation: textures filtered over the cell (2b.3) and ramp hysteresis (2b.1), measured with the stability count.
- **Coarse glyph vectors.** 1-bit 5×7 glyphs make coarse vectors, and some glyphs share one. Mitigation: a fixed preference order, with the ties measured.
- **Coarse phone grid.** About 100×30 cells. Mitigation: the density setting, judged on device.
- **Letters reading as words** in edge cells. Mitigation: the edge glyph set can mask them out, decided from screenshots.

## Sources

- ASCII CITY:
  - https://www.youtube.com/watch?v=3YtygAx_C6A
  - https://www.asciicity.com/
- Alex Harri: https://alexharri.com/blog/ascii-rendering (source: https://github.com/alexharri/website)
- Codrops (shape-aware ASCII renderer): https://tympanus.net/codrops/2026/09/04/beyond-the-luminance-ramp-a-shape-aware-ascii-renderer-in-three-js/
- Acerola: https://github.com/GarrettGunnell/AcerolaFX
- Monocraft: https://github.com/IdreesInc/Monocraft
- Existing Minecraft ASCII mods:
  - https://modrinth.com/shader/Ah4Q0Hhi
  - https://modrinth.com/resourcepack/ascii
  - https://www.planetminecraft.com/texture-pack/minecraft-but-everything-is-ascii-text/
