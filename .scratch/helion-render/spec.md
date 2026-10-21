# Helion-style hardware render pipeline: spec

## Context

The user wants UZDoom's hardware scene pipeline (classic `HWDrawInfo` BSP walk + draw lists + the Vulkan GPU-driven levelmesh of ADRs 0001–0003) ripped out and replaced by a C++ port of Helion's renderer (`/tmp/Helion`, commit `c711d4e0fcedc794ee3e59d07e903546b0eba3d5`, 2026-10-19) that runs on both OpenGL and Vulkan: level geometry, sprites, particles, VisualThinkers, textures, lighting, sky, portals, weapon sprite, models. C++ mirrors the C# 1:1 (tree, names, algorithms). UZDoom features Helion lacks are recreated in Helion style. Existing mod surfaces (GLDEFS hardware/material shaders, PP shaders, ZScript render natives, CVars, MAPINFO/UDMF knobs) keep working unchanged. End state: `src/rendering/hwrenderer/` scene code, the levelmesh and `hw_skydome` are deleted; `src/rendering/helion/` renders every frame through the untouched backend layer (`DFrameBuffer`, `FRenderState`, `IBuffer`, `IHardwareTexture`, `hw_postprocess`, `F2DDrawer`).

UZDoom has no software renderer (there is no `src/rendering/swrenderer`; AGENTS.md is stale on this). Nothing in this plan touches 2D drawing, menus, automap, post-processing or the ZWidget launcher.

## Decisions

| # | Decision |
|---|---|
| D1 | Keep the backend layer (`DFrameBuffer`, `FRenderState`, `IBuffer`, `IHardwareTexture`, `FMaterial`, GL/Vulkan backends, `hw_postprocess`). Replace everything above it. |
| D2 | Delete the levelmesh entirely (GPU-driven pipeline, bake, clusters, feedback, `lm_*`, `gl_uselevelmesh`). ADRs 0001–0003 superseded. |
| D3 | 1:1 mirror of Helion: `Core/Render/<X>` → `src/rendering/helion/<X>`, Helion's `OpenGL` directory/namespace segment kept, `Legacy` prefixes kept, nested C++ namespaces `Helion::Render::...`, PascalCase types and files, one `.h`/`.cpp` per `.cs` (partial classes → one `.h`, one `.cpp` per partial file). |
| D4 | Full mod-visible parity before the old pipeline is deleted; internal-only optimisations of the old code may go. |
| D5 | Hard shader contract: existing GLDEFS material/hardware shaders and ZScript `Shader`/`PPShader` APIs compile and behave unmodified; same injection points (`Material`, `ProcessMaterial`, `SetupMaterial`, `ProcessLight`, `ProcessMaterialLight`, same uniform/varying names). |
| D6 | Side-by-side bring-up behind `r_helionrenderer` (`CVAR_NOSAVE\|CVAR_NOINITCALL`), old pipeline deleted in the final step. Tree builds after every step. |
| D7 | Untouched: `hw_postprocess*`, `F2DDrawer`/HUD/menus/automap, ZWidget. |
| D8 | Verification: clean Debug build + scripted in-game captures on GL and Vulkan via the existing `r_ab_capture` mode with a fresh `-config`, never touching `~/.config/uzdoom/uzdoom.ini`. |
| D9 | Two colour modes like Helion: truecolor (default) and palette (`vid_colormode 1`). Existing `gl_lightmode` values stay and apply in truecolor mode. |
| D10 | Weighted-blended OIT (Helion `OitFrameBuffer`) for STYLE_Translucent, STYLE_Add, STYLE_Fuzzy and alpha-tested masked surfaces; a Helion-shaped sorted back-to-front bucket for every other render style, custom-material-shader surfaces, decals, translucent models and translucent portal planes. |
| D11 | Conventional depth only. `ShaderVars.ReversedZ` and the `${Depth}` substitution are not ported. |
| D12 | GPU interpolation Helion-style: `DynamicVertex`/`EntityVertex` carry prev state; the hardware path no longer calls `FInterpolator::DoInterpolations/RestoreInterpolations` for sector planes (camera/actor view interpolation unchanged). |
| D13 | 2D-array textures for base + brightmap layers (new backend capability). Surfaces whose material has normal/specular/PBR/detail/glow layers or a custom material shader use a per-material binding path. |
| D14 | Static-cache invalidation = Helion-style `SectorDynamic` flags + events raised from thinkers/setters, PLUS a per-frame render-signature sweep that catches direct writes. |
| D15 | Single-threaded renderer. `gl_multithread` and the `HWDrawInfo` worker thread are deleted. |
| D16 | Dead CVars deleted (list in M8); live CVars keep name/type/default/flags and are honoured. |
| D17 | New generic program API in the backend layer (`IRenderProgram`, buffer textures, 2D-array textures, compute dispatch); Helion `RenderProgram` classes generate GLSL from string helpers exactly like Helion, with the UZDoom contract prelude emitted by the same helpers. |
| D18 | Visibility: Helion Static (render blockmap), BSP (ViewClipper) and Adaptive modes all ported; new CVar `gl_rendermode`. |
| D19 | Instanced GPU sprites (`EntityVertex`, divisor 1) for actors, particles and VisualThinkers; UZDoom orientation modes as `RenderOptions` bits; models/voxels via a separate `EntityModelRenderer`. |
| D20 | Port every Helion renderer feature except health bars and blood recolouring (VanillaRender, PixelGapCorrection, fuzz refraction, invulnerability options get new CVars; MaxDistance→`r_distance_cull_type`/culldist, ContrastMode→`r_fakecontrast`, SkyMode→`r_skymode`). |
| D21 | Port Helion's `SkySphereRenderer`; recreate UZDoom sky features on it; delete `hw_skydome`. |
| D22 | Every program × material-shader × pass combination compiles at startup through the existing `GetShaderCount`/`CompileNextShader` progress hook. |
| D23 | Work tracked in `.scratch/helion-render/` (spec.md, map.md, one ticket per milestone M1–M8) and `docs/adr/0004-helion-style-render-pipeline.md`; continuous execution; single user sign-off at the end. |
| D24 | `r_ab_capture` (`src/d_abcapture.cpp`) and `tools/abtest.py` stay; `r_ab_fullbright` is re-implemented in the new pipeline; the `gl_uselevelmesh` pin line is removed from `PIN_TEMPLATE`. |
| D25 | Dynamic lights = clustered forward+: compute-shader light→cluster assignment on Vulkan and GL ≥ 4.3, CPU assignment on GL 3.3, identical buffer layouts. Sprites keep the per-sprite CPU light probe (`SetDynLight`); world geometry, models and the weapon use clusters. Existing `lights[]` record layout and `uLightIndex` name preserved. |
| D26 | Portals: Helion-shaped `PortalRenderer` hosts UZDoom's portal kinds; each portal view re-runs the world pass from the portal viewpoint inside a stencil mask with clip planes; recursion bounded by `r_portal_recursions`/`r_mirror_recursions`. |
| D27 | Polyobject sides are always dynamic and relink in the render blockmap when moved; `r_radarclipper` or fog-of-war (`LEVEL3_NOFOGOFWAR` absent + radar) forces BSP mode for that view. |
| D28 | New CVars (all `CVAR_ARCHIVE\|CVAR_GLOBALCONFIG`): `vid_colormode` 0, `gl_rendermode` 0 (0 static, 1 bsp, 2 adaptive), `gl_rendermode_timethreshold` 2200, `gl_rendermode_hysteresis` 0.05, `gl_rendermode_segthreshold` 8000, `gl_rendermode_timewindow` 10, `gl_vanillarender` false, `gl_vanillarender_downscale` 1.0, `gl_pixelgapcorrection` true, `gl_fuzzamount` 1.0, `gl_altinvulnoverlay` false, `gl_altinvulncolor` "00 ff 00", `gl_emulateinvulncolormap` false. Menu entries under Display Options → Hardware Renderer; LANGUAGE strings via `libraries/Translation`'s source language file. |
| D29 | Flats emitted per subsector fan grouped by `FSection` (reuse `hw_vertexbuilder.cpp` triangulation); walls per side split by 3D floors/transfer heights like Helion; Helion `WallTextureMapper` ported for UV math. |

## Verification

Prerequisites: build dir `build/` (`cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug -DHAVE_VULKAN=ON`), IWADs already in `build/wads/` (`DOOM2.WAD`, `HEXEN.WAD`, `HERETIC.WAD`, `Pirates!.wad`, `SOS_Boom.wad`, `3dfloortest.wad`, `lighttest.wad`, `citadel.pk3`). Never set `CVAR_ARCHIVE` CVars on the real config: every launch uses a fresh `-config /tmp/helion-smoke/run.cfg` and `-exec` pins, as `tools/abtest.py:499-560` does.

- **V0 (every milestone)**: `cmake --build build` completes with zero new warnings (compare `ninja -C build 2>&1 | grep -c warning` before/after).
- **V1 (M1)**: `build/uzdoom -iwad wads/DOOM2.WAD -config /tmp/helion-smoke/run.cfg -exec /tmp/helion-smoke/pins -width 1280 -height 720 +map MAP01` with pins `set vid_preferbackend 0|1`, `set r_helionrenderer_test 1`, `set screenshot_quiet 1`, `set r_ab_capture 30:/tmp/helion-smoke/m1-<backend>` → the capture PNG shows the test triangle over the scene; stdout contains `Selecting OpenGL backend` / `Selecting Vulkan backend`.
- **V2 (M3–M8)**: same launch per map in `{DOOM2 MAP01, DOOM2 MAP30, HEXEN MAP01, HEXEN MAP10, SOS_Boom MAP32, Pirates! MAP58, 3dfloortest MAP01, lighttest MAP01}` × backend `{gl33, vulkan}` × `gl_rendermode {0,1,2}` with pins `set r_dynlights 1`, `set r_ab_capture 30,120,300:/tmp/helion-smoke/<ms>-<map>-<backend>-<mode>`; expected: three PNGs per run, no `VM execution aborted`/`Shader`/`Vulkan` error lines in stdout, exit code 0, and (until M8) `tools/abtest.py diff` between `r_helionrenderer 0` and `1` captures reports only diffs in reviewed classes DC1/DC2/DC4/DC5 (`docs/adr/0002`) — the user reviews unclassified diffs at the final sign-off, not per milestone.
- **V3 (M4)**: additionally capture with `+summon ShadowArmor`-style spawns of a `STYLE_Translucent` and a `STYLE_Subtract` actor from `build/wads/lighttest.wad`'s actors (or `summon` of `Spectre` for fuzz and `SoulSphere` for additive) and confirm both appear.
- **V4 (M6)**: run lighttest twice on GL: normal and with `UZDOOM_FORCE_CPU_CLUSTERS=1`; `tools/abtest.py diff` between them reports no diff outside DC2.
- **V5 (M8)**: `grep -rn "HWDrawInfo\|gl_uselevelmesh\|LevelMesh_\|hw_skydome\|gl_multithread" src wadsrc tools` → no hits outside `src/d_abcapture.cpp` comments and `docs/`; `~/.config/uzdoom/uzdoom.ini` mtime unchanged across the whole V2 matrix.

## Assumptions & contingencies

- Helion commit `c711d4e` is the reference; if `/tmp/Helion` is missing, `git clone https://github.com/Helion-Engine/Helion /tmp/Helion && git -C /tmp/Helion checkout c711d4e0fcedc794ee3e59d07e903546b0eba3d5`.
- If a Helion defect is found during porting (two are known: `VertexPipeline.DrawArrays` ignores its primitive type; `WritePlaneClipData` picks the same program on both sides of a `Sector3D` ternary), port the evident intent and note it in the ticket's `## Comments`.
- If Vulkan `VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER` views cannot be made to work for a format, back the same `samplerBuffer` declaration with an SSBO (`layout(std430)`) and have `GenerateProgramPrelude` emit a `texelFetch`-compatible wrapper function with the same name; shader text in the Helion classes stays unchanged.
- If GL 3.3 drivers reject `layout(location=N)` on vertex inputs (they must not, it is core in 3.3), fall back to `glBindAttribLocation` from the `RenderProgramDesc` attribute table before link.
- If per-startup compile time of every program × material shader × pass exceeds what the launcher progress bar tolerates on a heavy mod, do NOT switch to lazy compilation (D22); instead reduce variants by folding pass variants into one program with `uniform int uPassType` branching and report the numbers to the user.
- The `-config` fresh-config launch leaves `~/.config/uzdoom/uzdoom.ini` untouched; if any capture is observed to write it, stop and pin the offending CVar through the pins file instead of `+set`.
- `.scratch/levelmesh-impl` tickets 23/24 (black-region/myhouse triage) are closed `wontfix`; if the same black regions appear in V2 on the new pipeline they are a new ticket under `.scratch/helion-render/`, not a blocker for M8.
