# 07: Helion-only features, CVars, menus

**Map:** `../map.md`
**Type:** task
**Status:** resolved
**Blocked by:** 06

## What to build

1. `gl_vanillarender`/`gl_vanillarender_downscale` (plane/wall clip buffers, `WriteSpriteClipBuffers`), `gl_pixelgapcorrection` (`VertexGapClampUV`), `gl_fuzzamount` + fuzz refraction pass, `gl_altinvulnoverlay`/`gl_altinvulncolor`/`gl_emulateinvulncolormap` (Helion `Renderer.cs:238-275` logic in `Renderer.World.cpp`), `vid_colormode` already from M3. `Fullbright`/`ExtraLight` map to existing `r_ab_fullbright`/`player->extralight`.
2. Add menu entries to `wadsrc/static/menudef.txt` (Display Options → Hardware Renderer; follow the existing `gl_*` option blocks and `OptionValue` lists) and strings to the Translation source language file (see `libraries/Translation/README`), `LANGUAGE` keys `OPTVAL_*`/`GLPREFMNU_*` in the existing style.
3. Acceptance: each new CVar toggles visible behaviour in a capture; menu shows them; `uzdoom.ini` is not written during captures (fresh `-config`).

## Acceptance

The acceptance item at the end of "What to build", verified per `../spec.md` § Verification.

## Comments
- 2026-10-20 (menus): added Display Options → Hardware Renderer (`OpenGLOptions`) entries for `vid_colormode` (OptionValue `ColorModes`), `gl_rendermode` (`RenderModes`), the four `gl_rendermode_*` sliders (grayed unless mode = Adaptive: timethreshold 100–10000 µs, hysteresis 0.01–0.5, segthreshold 1000–30000, timewindow 4–32 — ranges from Helion `ConfigRenderAdaptive`), `gl_vanillarender` + `gl_vanillarender_downscale` (1–8, grayed unless vanilla on), `gl_pixelgapcorrection`, `gl_fuzzamount` (0.25–4), `gl_altinvulnoverlay`, `gl_altinvulncolor` (ColorPicker), `gl_emulateinvulncolormap`. Strings `GLPREFMNU_{COLORMODE,RENDERMODE,RENDERMODE_*,VANILLARENDER,VANILLADOWNSCALE,PIXELGAPCORRECTION,FUZZAMOUNT,ALTINVULNOVERLAY,ALTINVULNCOLOR,EMULATEINVULNCOLORMAP}` and `OPTVAL_{TRUECOLOR,STATICBLOCKMAP,BSPWALK,ADAPTIVE}` added to `libraries/Translation/engine/zdoom/en_US.po` (source language) and `template.pot`; `OPTVAL_PALETTE` reused from `engine/common`. `gl_altinvulnoverlay` (Bool, false) and `gl_altinvulncolor` (Color, #00ff00 = D28 "00 ff 00") were undefined; added to `HelionCVars.{h,cpp}`. Not exercised: no build or launch.
- 2026-10-21 (HelionM7, partial): Fixed gl_vanillarender. The sprite-clip line buffers were never ported. Added `Renderer.World.cpp` SetMapDataBuffer/SetLineHeights/UpdateLineHeights (BindTextures::MapLineData/LineHeights). Removed `Unbind()` calls in WritePlaneClipData because they rebound the scene target during the wall-clip pass. Flats are now always drawn in the vanilla opaque pass, since the plane clip buffer is never MRT. The full-screen red/black garbage is gone on GL and Vulkan. ShaderVars staleness is now checked before the first frame (programs compiled before -exec pins). Vulkan `SetRenderTarget(IRenderTarget*)` now flushes pending Clear/ClearRenderTarget onto the target they were recorded for. Before this, the clip buffers on Vulkan were never cleared and every sprite was discarded. Decals: in vanilla mode all decals are drawn in `DecalRenderer::RenderOpaque` before the depth clear, translucent ones back to front. Shots behind the pillar no longer show through (kit /tmp/helion-smoke/m7, runs base/van1/van1ds2/van1vk/van1ds2vk). Invulnerability: Entrypoint/Renderer/SpriteLight/v_blend now implement gl_altinvulnoverlay (gl_altinvulncolor tint 0.15, no fullbright) and gl_emulateinvulncolormap (shader colormap, post-process skipped). Captures do NOT yet show any invulnerability effect: the kit's `m7test_invul` GiveInventory trigger did not take effect. Also, the `invemu` run (gl_emulateinvulncolormap 1) hit a Fatal Error at startup (log runs/invemu.log), not diagnosed. gl_fuzzamount 4 visibly enlarges the fuzz blocks (GL+VK). gl_pixelgapcorrection 0 changes only 5 px in the test room. Not done: vid_colormode, gl_rendermode*, menu capture. uzdoom.ini mtime unchanged (1792471034). Status stays in-progress.
- 2026-10-21 (HelionM7Finish, resolved): **gl_emulateinvulncolormap 1 startup Fatal Error, root cause:** `ModelProgram` strips `uniform samplerBuffer colormapTexture;` from the light-level variables, but with emulation on `FragFunction::InvulnerabilityFragColorInner` reads `texelFetch(colormapTexture, ...)`. So "Render program Model - Main - material Default" failed to compile, and `I_FatalError` ran at the first frame (the signal 11 in runs/invemu.log was the exit path tearing down the GL programs after the error window was killed by `timeout`). Fix: ModelProgram keeps the declaration when `ShaderVars::EmulateInvulnerabilityColorMap` is set. `EntityModelRenderer` binds `ColormapTexture(BindTextures::Colormap)`. **Invulnerability trigger:** `GiveInventory("PowerInvulnerable")` had no effect, so the kit handler now gives `InvulnerabilitySphere` (the PowerupGiver), and the effect shows from tick 40. **gl_altinvulnoverlay in palette mode:** `V_CalcBlend` suppressed the tint when `vid_colormode 1`. Helion's `DrawHudEffects` draws it in palette mode with the default `PaletteTrueColorOverlay` true, and every other UZDoom player blend is drawn in palette mode too, so the suppression was removed (v_blend.cpp). Menu: `OpenGLOptions` captured at the top (`openmenu`) and scrolled to the end (kit `menu.pk3` UiTick sends `MKEY_Up` to wrap to the last item). Every new label and option value is a resolved string. The Adaptive sliders and the vanilla downscale slider gray/ungray with their controlling CVar, and the overlay colour swatch follows `gl_altinvulnoverlay`. build-b and build/ are clean (only the pre-existing a_dynlight.h warning). uzdoom.ini mtime unchanged (1792471034). Kit /tmp/helion-smoke/m7: invmatrix.sh, map01matrix.sh, menu.pk3. Verdict (share of pixels with max channel diff > 8, tick 70 / MAP01 tick 70):

    |check|GL|Vulkan|
    |---|---|---|
    |invuln default vs classic (`r_helionrenderer 0`); only diff is the M7Fuzz actor (Helion fuzz refraction vs classic shadow fuzz, gl_fuzztype 0)|1.22%|1.30%|
    |invuln default vs no invuln (inverse gray, fullbright)|83.9%|83.9%|
    |gl_altinvulnoverlay 1 vs default (green 0.15 tint, normal lighting)|83.9%|83.9%|
    |gl_altinvulncolor "ff 00 00" vs green|83.9% (red tint)|83.9% (red tint)|
    |gl_emulateinvulncolormap 1 vs default (COLORMAP 32 ramp)|72.3%|72.3%|
    |vid_colormode 1 invuln vs truecolor invuln (colormap 32)|7.7%|7.7%|
    |vid_colormode 1 + gl_altinvulnoverlay 1 vs vid_colormode 1 invuln (tint, no colormap)|83.9%|83.9%|
    |vid_colormode 1 + gl_emulateinvulncolormap 1 vs vid_colormode 1 (palette already uses colormap 32, Helion emits nothing)|0.00%|0.00%|
    |DOOM2 MAP01 gl_rendermode 0 vs classic|0.22%|0.22%|
    |DOOM2 MAP01 gl_rendermode 1 / 2 vs 0 (visibility only, no image change expected)|0.00% / 0.00%|0.00% / 0.00%|
    |DOOM2 MAP01 vid_colormode 1 vs truecolor (palette light banding), modes 0/1/2 identical|32.6%|32.6%|
    |GL vs Vulkan, every run above|≤0.18%|—|
    |menu OpenGLOptions top + end, defaults and toggled values|labels/values resolved|labels/values resolved|
