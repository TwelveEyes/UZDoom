# Ticket 02 — Per-frame visibility consumer inventory

Status: complete
Primary source: local UZDoom tree (read-only). All citations are `file:line` in that tree.

## What the classic path produces per frame

The per-frame visibility results of the classic path are produced by the BSP traversal in
`src/rendering/hwrenderer/scene/hw_bsp.cpp` (`HWDrawInfo::RenderBSP` :1094 → `RenderBSPNode` :1033 /
`RenderOrthoNoFog` :1074 → `DoSubsector` :766 → `AddLine` :300), executed partly in the multithreaded
render job pool (`renderPool` :51, `jobQueue` :111, `WorkerThread` :113, dispatch :1113-1120,
cvar `gl_multithread` :43). They materialize as four kinds of output:

1. **Drawlists** — `HWWall`/`HWFlat`/`HWSprite` items in `HWDrawInfo::drawlists[GLDL_*]`
   (`src/rendering/hwrenderer/scene/hw_drawlist.h:107-114`), filled by worker jobs (Wall :449,
   Particle :890, Sprite :916, Flat :967, Portal :996/1009 in `hw_bsp.cpp`), drawn by
   `HWDrawInfo::RenderScene` (`hw_drawinfo.cpp:535`) and `RenderTranslucent` (`hw_drawinfo.cpp:597`)
   through `HWDrawList::Draw*/DrawSorted` (`hw_drawlist.cpp`).
2. **Per-frame flags written into level data** (persist across the frame, readable by non-renderer code):
   - `seg->linedef->flags |= ML_MAPPED` — set *only* by the renderer, `hw_bsp.cpp:439`
     (definition `src/doomdata.h:145`, "set if already drawn in automap").
   - `subsector->flags |= SSECMF_DRAWN` — set by the renderer's clipper/radar path
     `hw_bsp.cpp:358,362,374` (definition `src/gamedata/r_defs.h:1662`); also set by savegames
     (`src/p_saveg.cpp:354,436`).
   - `sector->MoreFlags |= SECMF_DRAWN` — set only by the renderer, `hw_bsp.cpp:910`
     (definition `src/gamedata/r_defs.h:493`).
   - Dither-transparency flags: `side_t::Flags |= WALLF_DITHERTRANS_{BOTTOM,MID,TOP}`,
     `dithertranscount`, `secplane_t::dithertransflag` — written by
     `TraceCallbackForDitherTransparency` (`hw_drawinfo.cpp:716`, writes at :735-743) and
     `HWDrawInfo::SetDitherTransFlags` (`hw_drawinfo.cpp:810`).
   - `Level->cullcolor` — written from a visible sector's `Colormap.FadeColor` in the distance-cull
     branch of `AddLine` (`hw_bsp.cpp:391`); consumed as fog color (`hw_setcolor.cpp:108`) and as the
     clear color of offscreen actor views (`r_utility.cpp:1294-1295`; default `r_utility.cpp:176`).
3. **Per-frame render-state buffers** (created/cleared per scene in `HWDrawInfo::ClearBuffers`,
   `hw_drawinfo.cpp:231`): `ss_renderflags`/`section_renderflags`/`no_renderflags` (SSRF_SEEN /
   SSRF_PROCESSED — portal coverage + once-per-frame dedup, e.g. `hw_bsp.cpp:809`), `CurrentMapSections`,
   `Portals[]`, `RenderedTargets[]` (`hw_bsp.cpp:55`), and the GPU buffers `screen->mVertexData` /
   `mLights` / `mBones` (mapped in `CreateScene`, `hw_drawinfo.cpp:484`).
4. **Fake sectors** — per-frame sector copies `hw_FakeFlat()` (`hw_fakeflat.h:32`) carrying current
   heights/planes/light levels, consumed by wall/flat/sprite setup and by `CheckUpdate`
   (`hw_bsp.cpp:882`).

## Consumer table

| # | Consumer | Consumes (file:line) | Classification | Note |
|---|----------|----------------------|----------------|------|
| 1 | Walls (HWWall) | angle clipping in `AddLine` (`hw_bsp.cpp:300`); WallJob dispatch `hw_bsp.cpp:449`; `HWWall::Process`/`DrawWall` (`hw_walls.cpp`, incl. sky planes `hw_sky.cpp:134`); drawn in `RenderScene` `hw_drawinfo.cpp:535` (GLDL_PLAINWALLS/MASKEDWALLS) | **Replaces cleanly** | Front-to-back angle clipping (`mClipper`) and wall/wall/flat splitting (`hw_drawlist.cpp` `SortWallIntoPlane`/`SortWallIntoWall`) exist only for painter's order; z-buffer + per-wall frustum cull replaces them. Exception: the `RFL_NO_CLIP_PLANES` CPU-split fallback (`hw_drawlist.cpp` `SortWallIntoPlane`) has no z-buffer analog — keep behind the capability flag or drop. |
| 2 | Flats (HWFlat) | FlatJob dispatch `hw_bsp.cpp:967` (with section dedup via `section_renderflags` SSRF_PROCESSED); `HWFlat::ProcessSector` (`hw_flats.cpp`); drawn in `RenderScene` (GLDL_PLAINFLATS/MASKEDFLATS) | **Replaces cleanly** | Subsector quads → static mesh triangles; per-sector planes/light go to the sector state buffer (locked map decision). SSRF_PROCESSED dedup disappears with the per-subsector loop. |
| 3 | Sprites (HWSprite: actors, models, psprites) | `RenderThings` `hw_bsp.cpp:626` (per-thing distance cull, `CurrentMapSections` check, `R_ShouldDrawSpriteShadow`); SpriteJob `hw_bsp.cpp:916`; `HWSprite::Process` (`hw_sprites.cpp`); translucent pass `hw_drawinfo.cpp:597` → `hw_drawlist.cpp` `DrawSorted` (clip-plane split); player sprites via `PreparePlayerSprites` (end of `RenderBSP`, `hw_bsp.cpp:1094` ff.) | **Replaces cleanly** | Locked map decision: sprites stay on the existing batch path, per-sprite frustum test replaces traversal-based clipping/sorting. Sprite/wall/sprite splitting in `hw_drawlist.cpp` is painter's-order only. |
| 4 | Particles (visual-thinker sprites + P_Particle) | `RenderParticles` `hw_bsp.cpp:709`; ParticleJob `hw_bsp.cpp:890`; reads `subsector_t::sprites` (`gamedata/r_defs.h:1697`) and `Level->ParticlesInSubsec[]` | **Replaces cleanly** (draw side) / **Stays** (data side) | Drawn as `HWSprite`s through the same batch path; the thinker data is game logic (`playsim/p_effect.cpp:217` pushes thinkers into `subsector->sprites`) and is untouched by the levelmesh. |
| 5 | Portals (sector portals, stacked portals, line-to-line, plane mirrors, PClip) | `FPortalSceneState::StartFrame`/`EndFrame`/`RenderFirstSkyPortal` (`hw_portal.cpp:41/73/113`); `BeginScene` (`hw_portal.h:147`); per-portal clip `mClipPortal->ClipSeg/ClipPoint/ClipSubsector` (top of `AddLine`, `hw_bsp.cpp:300`, and in `DoSubsector` `hw_bsp.cpp:766`); portal-coverage SSRF_SEEN unclip `UnclipSubsector` (`hw_bsp.cpp:230`, consumed at `hw_bsp.cpp:766` ff.); `HWSectorStackPortal::AddSubsector` (`hw_portal.h:320`, fed by PortalJob `hw_bsp.cpp:996/1009` via `AddSubsectorToPortal` `hw_drawinfo.cpp:1091`) + `SetupCoverage`/`DrawContents` (`hw_portal.cpp:799/817`); stencil `HWPortal::SetupStencil`/`RemoveStencil` (`hw_portal.cpp:249/333`); `HWLineToLinePortal::RenderAttached` (`hw_portal.cpp:679`, called at end of `RenderBSP`); plane mirror `hw_portal.cpp:906`; `HWHorizonPortal`/`HWEEHorizonPortal` `hw_portal.cpp:1111/1180` | **Needs a hook** | The levelmesh path needs an explicit mechanism for: (a) per-portal scissor/stencil masking, (b) per-portal clip or a per-portal-region visible set (replaces PClip), and (c) the "subsector seen through the portal from the other side" coverage flag (SSRF_SEEN → `UnclipSubsector`) that lets the outer pass unclip ranges already covered. Named hook: **per-portal visibility-coverage + stencil pipeline** (feeds ticket 06). |
| 6 | Fake flats (`hw_FakeFlat`) | called throughout `hw_bsp.cpp` (worker jobs :113 ff., `AddLine` :300, `DoSubsector` :766, incl. `in_area` selection); per-frame sector sync `CheckUpdate` (`hw_bsp.cpp:882`) | **Replaces cleanly** | Becomes the per-sector state buffer + vertex-shader warp (locked map decision: heights/planes/light change at runtime, topology static). Nothing outside the renderer reads fake sectors. |
| 7 | Glow spots / plane glow (glowing floors lighting sprites) | computed in game logic: `sector_t::CheckSpriteGlow` (`playsim/p_sectors.cpp:1206`) from plane `GlowColor`/`GlowHeight` (settable at runtime via ACS `playsim/p_acs.cpp:6720`; texture glow `common/textures/gametexture.cpp:252`); consumed per-sprite by `hw_sprites.cpp:253, 1255` and `hw_weapon.cpp:373` | **Stays** (compute) / **Replaces cleanly** (apply) | The glow computation is game logic reading sector state — untouched. The renderer only applies it per sprite; the levelmesh sprite pass calls the same function. If plane glow fields must reach shaders, sync them into the sector/plane state buffer (they are runtime-mutable). |
| 8 | Sector glow/pulse (per-frame sector light level) | per-frame `sector_t` light data consumed through fake sectors / `CheckUpdate` (`hw_bsp.cpp:882`) and `hw_FakeFlat` (row 6); pulse computed in game logic (map thinkers, e.g. `playsim/mapthinkers/a_lights.cpp`) | **Replaces cleanly** | Light level/pulse is per-sector dynamic state → carried by the sector state buffer, evaluated in the shader (locked map decision). No traversal dependency: the classic path merely reads it for whatever subsectors it found visible. |
| 9 | Lightmaps | baked per level at map load: `screen->InitLightmap(Level->LMTextureSize, LMTextureCount, LMTextureData)` (`src/maploader/maploader.cpp:3235`; Vulkan `common/rendering/vulkan/system/vk_renderdevice.cpp:463`, GL `common/rendering/gl/gl_framebuffer.cpp:354`); per-surface lightmap UVs from static sidedef data (`hw_walls.cpp:1355-1361`, `hw_vertexbuilder.cpp:195`); image bound for the raytraced-lighting shader at `common/rendering/vulkan/renderer/vk_descriptorset.cpp:107` | **Stays** | Level-load operation driven by static UDMF lightmap data; no per-frame visibility input. Levelmesh keeps the same UV data on its static surface attributes. |
| 10 | Dynamic lights (FDynamicLight, shadow map) | per-tick collection into render sections/sidedefs: `a_dynlight.cpp:425,434` (`SortedAddUnique` into `section->dlist` / `sidedef->dlist`; list type `gamedata/r_defs.h:1264`), removal `a_dynlight.cpp:641,649`; per-vertex application to visible geometry: `hw_flats.cpp:153-159`, `hw_walls.cpp:431-436, 487`; shadow map light gathering is independent of the traversal: `CollectLights` (`rendering/hwrenderer/hw_entrypoint.cpp:50`, `common/rendering/v_video.h:174-178`, `common/rendering/hwrenderer/data/hw_shadowmap.cpp:99-113`) | **Stays** (collection) / **Replaces cleanly** (application) | The dlist maintenance is game-logic/tick code — untouched. Per-vertex CPU light application disappears: active per-sector light lists go into the sector state buffer and are evaluated in the shader (ticket 05 detail). Shadow-map source list is not a consumer of visibility results. |
| 11 | Sky & sky portals | sky-as-wall geometry: `HWSkyInfo::init` (`hw_sky.cpp:56`), `HWWall::SkyPlane`/`SkyTop`/`SkyBottom` (`hw_sky.cpp:134/264/362`); skybox portal contents: `HWSkyPortal::DrawContents` (`hw_skyportal.cpp:44`), `HWEEHorizonPortal::DrawContents` (`hw_portal.cpp:1180`, sky/ceil/floor :1190/1204/1218); first-sky-portal pass `RenderFirstSkyPortal` (`hw_portal.cpp:113`, gated by `gl_no_skyclear` in `DrawScene` `hw_drawinfo.cpp:1009`); sky fade uses `Level->cullcolor` (`hw_sky.cpp:113`) | **Replaces cleanly** | Z-buffered skybox geometry replaces the draw-first, no-depth sky pass; sky-plane wall geometry is ordinary mesh triangles with sky texturing. Mirror/stacked-portal sky reflection cases (`mCurrentPortal->GetMirrorSide()`, `hw_bsp.cpp:766` ff.) are the residual risk — assign to tickets 06/07. |
| 12 | Multithreaded render job pool | `renderPool` (`hw_bsp.cpp:51`), `jobQueue` pool[300000] (`hw_bsp.cpp:111`), `WorkerThread` (`hw_bsp.cpp:113`), job dispatch in `RenderBSP` (`hw_bsp.cpp:1113-1120`), `gl_multithread` cvar (`hw_bsp.cpp:43`) | **Replaces cleanly** | The pool exists to parallelize per-subsector work (Wall/Flat/Sprite/Particle/Portal jobs). With a static levelmesh the parallel work unit becomes per-sector state-buffer update + sprite batch fill; the pool mechanism is renderer-internal and reusable. |
| 13 | Texture precache (`hw_precache.cpp`) | `hw_PrecacheTexture` (`src/rendering/hwrenderer/hw_precache.cpp:89`), called **once per level load** from `src/p_setup.cpp:226` | **Stays** | Driven by a level-wide texture hit list built from *all* sector/side textures, level sky textures, MAPINFO `PrecachedTextures/Classes`, and level actors (`p_setup.cpp:148-225`) — a level-load operation with zero per-frame visibility input. |
| 14 | Vulkan raytracer (`VkRaytrace`) | `screen->SetLevelMesh(camera->Level->levelMesh)` (`src/rendering/hwrenderer/hw_entrypoint.cpp:108`) → `VulkanRenderDevice::SetLevelMesh` (`src/common/rendering/vulkan/system/vk_renderdevice.cpp:524-527`) → `VkRaytrace::SetLevelMesh` (`src/common/rendering/vulkan/renderer/vk_raytrace.cpp:49-60` — rebuilds buffers/BLAS/TLAS **only when the mesh pointer changes**); consumes only `MeshVertices`/`MeshElements`; accel struct bound at `src/common/rendering/vulkan/renderer/vk_descriptorset.cpp:109` | **Stays** (relative to the per-frame path) | Consumes the *static* `DoomLevelMesh` built at map load (`Level->levelMesh = new DoomLevelMesh(*Level)`, `src/maploader/maploader.cpp:3251`; field `src/g_levellocals.h:529`; freed `src/p_setup.cpp:378-380`). No contact with drawlists or any per-frame traversal output. Standing constraint for the levelmesh effort: keep this builder/consumer pair working when the mesh data model changes (ticket 04 scope). |
| 15 | Automap (`am_map.cpp`) | reads `sub->flags & SSECMF_DRAWN` at `src/am_map.cpp:2103, 2240, 3074, 3359`; reads `line.flags & ML_MAPPED` at `src/am_map.cpp:2692` | **Needs a hook** | Fog-of-war ("explored") state: the levelmesh path must keep populating `SSECMF_DRAWN`/`ML_MAPPED` for its per-frame geometry visibility test (per-sprite frustum tests alone will not set line/subsector flags). Named hook: **per-frame exposure pass** — mark subsectors/lines whose region passed the levelmesh frustum cull (or a coarse portal-aware visibility query), then write the same two flags. Note `ML_MAPPED` is set by the renderer only (`hw_bsp.cpp:439`), but scripts/UMAPINFO can also set it (`src/gamedata/p_xlat.cpp:98`, `src/maploader/udmf.cpp:959`, `src/playsim/p_lnspec.cpp:2774`), so the exposure pass must be *additive*, never clearing. |
| 16 | Savegame fog-of-war state | `FLevelLocals::RecalculateDrawnSubsectors` rebuilds `SSECMF_DRAWN` from `ML_MAPPED` (`src/p_saveg.cpp:347-360`, key reads at :352-354); savegame *write* encodes `SSECMF_DRAWN` per subsector (`src/p_saveg.cpp:372-390`); savegame *load* restores it (`src/p_saveg.cpp:425-440`) | **Needs a hook** | Same two flags as row 15 are serialized into savegames. If the levelmesh path stops populating them, saved/loaded fog-of-war state silently degrades. Covered by the same exposure-pass hook (row 15). |
| 17 | Dither transparency (dithered geometry around visible enemies) | per-frame rendered-actor list `RenderedTargets[MAXDITHERACTORS]` (`hw_bsp.cpp:55`), collected in `DoSubsector` under `r_dithertransparency && doOob && mCurrentPortal == nullptr` (`hw_bsp.cpp:927-934`); then `P_CheckSight(players[consoleplayer].mo, RenderedTargets[ii], 0)` filter + `SetDitherTransFlags` at the end of `RenderBSP` (`hw_bsp.cpp:1134-1139`); `SetDitherTransFlags` runs 3 viewpoint→actor `Trace()` calls (`hw_drawinfo.cpp:810, 834-837`) whose callback writes `WALLF_DITHERTRANS_*` on `side_t` and `dithertransflag` on `secplane_t`/3D-floor planes (`hw_drawinfo.cpp:716, 735-743`); flags are consumed by the *next* geometry draw: `hw_walls.cpp:85-87` (wall dither) and `hw_flats.cpp:311, 363, 400-401` (flat/3D-floor dither, per-frame reset); the clipper also treats dithered mids as non-blocking (`hw_bsp.cpp:401, 428`) | **Needs a hook** | A genuine per-frame feedback loop crossing into game logic: which actors the pass emitted → `P_CheckSight` LOS → geometry dither flags → renderer. Levelmesh path needs: (a) the "visible this frame" actor list from the per-sprite frustum pass (trivially available), (b) the same trace-based flag writing (keep `SetDitherTransFlags` as-is — it only needs the actor list and the viewpoint), and (c) dither effects in the wall/flat shaders (already a render effect `EFF_DITHERTRANS`). This is also the only place renderer code *calls* game-logic sight. |
| 18 | Game-logic sight: `P_CheckSight` / `P_LineOpening` | `P_CheckSight` declared `src/playsim/p_local.h:265`, defined `src/playsim/p_sight.cpp`; `P_LineOpening` defined `src/playsim/p_maputl.cpp:110` | **Stays** | Independent LOS over `line_t`/`sector_t` (used by bots `playsim/bots/b_func.cpp`, missiles, doors — `playsim/p_map.cpp:272, 970, 1155, 3149`, `playsim/p_switch.cpp:161`). No read of drawlists or traversal outputs. Shared state is only the `validcount` stamp: declared `src/playsim/p_maputl.h:38`, incremented by the renderer once per frame (`hw_bsp.cpp:1108`) *and* by game logic on level changes (lights `playsim/mapthinkers/a_lights.cpp:535`, plane changes `playsim/p_maputl.cpp:565, 576, 1436, 1700`, polyobjects `playsim/polyobjects.cpp:251`, portal rebuild `playsim/po_man.cpp:953`, reset `playsim/po_man.cpp:721`) — an invalidation counter, not a visibility result. |
| 19 | Visual thinkers (`p_visualthinker`) | producer: `sp->PT.subsector->sprites.Push(sp)` (`src/playsim/p_effect.cpp:217`, list type `src/gamedata/r_defs.h:1697`); sole consumer: `RenderParticles` (`hw_bsp.cpp:709`) | **Stays** | Renderer consumes its output; no reverse dependency (no renderer-structure reads in the thinker code). |
| 20 | `SECMF_DRAWN` ("sector drawn at least once") | set `hw_bsp.cpp:910`; definition `src/gamedata/r_defs.h:493` | **Stays** (effectively inert) | Grep across `src/` finds no reader anywhere. Free to keep populating (harmless) or drop in the levelmesh path. |
| 21 | `Level->cullcolor` (visible faded-sector cull color) | written `hw_bsp.cpp:391` (distance-cull branch of `AddLine`); read as fog color `hw_setcolor.cpp:108`, sky fade fallback `hw_sky.cpp:113`, offscreen-actor-view clear color `r_utility.cpp:1294-1295` (default `r_utility.cpp:176`) | **Replaces cleanly** | Per-frame scene setup, not a gameplay consumer. Levelmesh path should reproduce "cull color = fade color of a visible culled sector" (e.g. during the exposure pass) or accept the `gl_cullcolor` cvar fallback as a visual delta. |
| 22 | Per-frame render-state flags (`ss_renderflags` SSRF_SEEN/PROCESSED, `section_renderflags`, `no_renderflags`, `CurrentMapSections`) | created/cleared `hw_drawinfo.cpp:231`; read/written inside `hw_bsp.cpp` (e.g. `:809` radar-visible re-entry, `:960`-ff. section dedup) | **Replaces cleanly** (renderer-internal) | Pure traversal bookkeeping (once-per-frame dedup + portal coverage). No reader outside the renderer; SSRF_SEEN is consumed only by the portal unclip logic (row 5 hook). |

## "Is renderer traversal render-only?" — VERDICT: NO (assumption broken)

The traversal writes per-frame results into level data that game-logic and persistence code read:

1. **`ML_MAPPED`** — written *only* by the renderer's `AddLine` (`hw_bsp.cpp:439`), read by the
   automap (`am_map.cpp:2692`) and by savegame subsector recovery (`p_saveg.cpp:352`).
2. **`SSECMF_DRAWN`** — written by the renderer's clipper/radar path (`hw_bsp.cpp:358, 362, 374`),
   read by the automap (`am_map.cpp:2103, 2240, 3074, 3359`), by the renderer's own fog-of-war
   re-entry check (`hw_bsp.cpp:573, 809`), and serialized on save/load (`p_saveg.cpp:372-390, 425-440`,
   recovery `p_saveg.cpp:347-360`).
3. **Dither transparency** — the pass's per-frame rendered-actor list feeds `P_CheckSight` and then
   geometry flags (`WALLF_DITHERTRANS_*`, `dithertransflag`) consumed by the next geometry draw
   (`hw_bsp.cpp:927-934, 1134-1139`; `hw_drawinfo.cpp:716-837`; `hw_walls.cpp:85-87`;
   `hw_flats.cpp:311, 363, 400-401`).

What is *not* coupled: `P_CheckSight`/`P_LineOpening`/visual thinkers share only the `validcount`
invalidation stamp with the renderer (row 18); `hw_precache.cpp` (row 13), `VkRaytrace` (row 14),
lightmaps (row 9) and dynamic-light collection (row 10) have no per-frame traversal input at all.

## Assumptions validated / broken

- **BROKEN**: "renderer traversal is render-only." Fog-of-war state (`ML_MAPPED`, `SSECMF_DRAWN`) is
  game-logic-visible and persisted in savegames (rows 15-16); dither transparency is a per-frame
  feedback loop that crosses into game-logic sight (row 17). A levelmesh implementation that stops
  writing these flags silently breaks automap fog-of-war and savegames.
- **Validated**: precache (row 13), VkRaytrace (row 14), lightmaps (row 9), dynamic-light/shadow-map
  collection (row 10), and game-logic sight (rows 18-19) are independent of the per-frame traversal
  results.
- **Validated**: drawlist/batch content (walls/flats/sprites/particles) is consumed only by the render
  passes in `hw_drawinfo.cpp`; no file outside `rendering/` reads `HWDrawList`/`HWWall`/`HWFlat`/
  `HWSprite` (grep across `src/`).
- **New requirement (hook) candidates for the spec**: per-portal visibility-coverage + stencil
  pipeline (row 5); per-frame exposure pass writing `SSECMF_DRAWN`/`ML_MAPPED` (rows 15-16);
  visible-actor list + dither flag loop (row 17); cull-color source (row 21).
