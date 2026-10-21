# 09: Static mode: mixed-sky bleed-through and see-through walls into closed sectors

**Map:** `../map.md`
**Type:** task
**Status:** ready-for-agent
**Blocked by:** none

## What to build

Two differences between `gl_rendermode 0` (static) and classic / `gl_rendermode 1` (BSP) that the M3 acceptance runs found. Both are static-mode only, on GL and Vulkan, and come from the static cache drawing geometry that the BSP walk never reaches.

1. **Mixed-sky bleed-through.** The static cache emits every sky ceiling at `MaxSky` (Helion design, `GeometryRenderer::RenderFlat` → `WorldTriangulatorPartC::HandleSection(..., MaxSky)`), each into the sky component of its own `SkyInfo`. When a map mixes skies (sky transfer lines, UDMF `skyceiling`, per-sector sky), a ray through one sky opening can hit another sky sector's `MaxSky` plane, which then marks the stencil for that other sky. BSP mode and classic never draw that plane.
   - Repro: skytest.wad MAP01, view `8912 384 0 180 -25` (x y z yaw pitch; `/tmp/helion-smoke/sky/look.pk3`, `skylook_views`), sector 24 sky opening in sector 23: a strip of the grey transfer sky of sectors 18/19 (Static_Init lines, tags 600/601) shows where classic shows the default sky. Captures: `/tmp/helion-smoke/geo/vx-v4-st-{0,1}-m0/MAP01_195.png` vs `vx-v3-st-{0,1}-h0/MAP01_195.png`; montage `/tmp/helion-smoke/geo/acc/v4-st-{0,1}-b.png` row 4.
   - The fix must keep the `MaxSky` behaviour for a single sky (geometry of taller neighbouring sectors stays visible through a low sky ceiling, as in classic).
2. **See-through upper/lower textures into closed sectors.** Static mode draws the inside of closed sectors behind transparent texels of upper/lower textures; classic and BSP mode treat the line as solid and draw nothing behind it (the HWWall clipper occludes).
   - Repro: HEXEN MAP01, view `-3616 2176 0 0 -25`: the stained-glass windows show the geometry behind them in m0, black in classic/m1. Captures: `/tmp/helion-smoke/geo/vx-v3-hx1-{0,1}-m0/MAP01_195.png` vs `vx-v3-hx1-{0,1}-h0/MAP01_195.png`; montage `/tmp/helion-smoke/geo/acc/v3-hx1-{0,1}-b.png` row 4.

## Acceptance

- Both repro views in `gl_rendermode 0` match classic (`r_helionrenderer 0`) and `gl_rendermode 1` on GL and Vulkan.
- The rest of the V2 view matrix (`/tmp/helion-smoke/geo/vmatrix.sh`, maps hx1 hx10 d1 st d7 d30 sos32 f3d) is unchanged in static mode.

## Comments
- 2026-10-20 (HelionStaticPerf): M5.5 matrix m57 found two more repros of the same class. (a) HEXEN MAP01 view `-1104 2848 0 0 0` (also at pitch -25): stained-glass uppers at the right edge show the room behind them in m0; RMSE 0.040/0.038 against classic, against 0.005/0.018 for m1. (b) Pirates! MAP58 view `440 5968 0 270 0`: m0 draws a black silhouette above the sea horizon that classic and m1 do not draw; RMSE 0.028 against 0.007. It was already present in matrix m56. Captures: `/tmp/helion-smoke/geo/vx-m57-{hx1,p58}-{0,1}-{h0,m1,m0}/`. Montages: `/tmp/helion-smoke/m56/hx1-m57-75.png`, `/tmp/helion-smoke/m56/p58-m57-135.png` (classic, m1, m0).
