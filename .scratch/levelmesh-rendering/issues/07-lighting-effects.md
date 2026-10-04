# Lighting & visual effects on static geometry

Type: grilling
Status: resolved
Blocked by: 02, 04

## Question

Lock the lighting and per-sector visual-effect model for the levelmesh path at full parity with the classic
path's look:

- Baked per-vertex light vs per-sector dynamic light (doors, light switches, runtime lightlevel changes).
- The classic per-segment light interpolation semantics (LINE_PASSLITE and friends) and how they map onto
  the mesh (vertex-baked vs shader-computed).
- Sector effects: glow, pulse, RIP, animated flats, scrolling, colormap (fire) — and how each maps to
  shader state / per-sector attributes.
- Point/dynamic lights and lightmap support (`GLightmapping`).
- Sky and sky-portal rendering in the mesh world (`hw_sky.cpp`, `hw_skyportal.cpp`).

End state: a spec-ready shading-model section (what is baked, what is per-sector state, what is a shader
variant) plus a parity checklist against the classic path.

## Answer

Resolved 2026-10-04 (grilling, 3 rounds; every branch below was explicitly chosen/confirmed by the user). The
shading model is locked: the classic per-surface light chain runs **in the levelmesh vertex shader** as a
formula-for-formula, bit-exact GLSL port; the fragment shader is the existing `main.fp` with a define-level
adaptation promoting the per-draw light/fog/glow/desat values to per-vertex varyings. Parity is by
construction — same equations, same inputs, same fragment code — and is verified with the A/B tool at zero
tolerance on the checklist below.

### Light chain placement (round 1, locked: port into the VS)

**Why the CPU cannot pre-compute:** a levelmesh draw is a (region, texture) IBO sub-range (08) spanning many
sectors and sides — the classic's per-draw `SetColor`/`SetFog` state has nowhere to live. Every per-surface
light input is either per-sector-per-frame (light thinkers, fire/pulse/damage colormaps) or per-surface-static
(side tier lights, fake contrast) — exactly the two sources the VS already fetches.

**Contract.**

- The levelmesh VS computes per vertex the full chain the classic computes per draw (port list below) and
  emits: all `main.vp` varyings (vColor, vTexCoord, vLightmap, vWorldNormal, vEyeNormal, pixelpos, glowdist,
  gradientdist) **plus promoted per-surface values**: vLightLevel, vFogDensity, vLightFactor, vLightDist,
  vDesaturation, vFogEnabled (int), vGlowTopPlane, vGlowBottomPlane, vGlowTopColor, vGlowBottomColor — ≈45
  float units ≈ 12 vec4, inside GL33 varying limits.
- The levelmesh fragment = `main.fp` + an adaptation block `#define`-ing each promoted uniform to its varying
  (the GL backend already does exactly this for `uLightLevel` → `uLightAttr.a`; the pattern is established).
  All per-pixel lighting math in `main.fp` (Doom lighting equation, fog, glow falloff, lightmaps, dynamic
  lights) is untouched.
- **Every surface fetches the sector state record for shading — not only dynamic ones.** 04 already states
  "light/texture by all surfaces"; this makes it explicit. Light thinkers (strobe/pulse/fire/glow) mutate
  arbitrary sectors without moving geometry, so static surfaces must read the record too. For a dynamic
  surface the same fetch serves the warp. Bandwidth: 96 B/vertex, L1/L2-resident (50k sectors ≈ 4.8 MB),
  good locality from the per-region/per-piece pool order (04); 09/10 validate at scale.
- C++ remains the single source of truth for the equations; the GLSL is a translation. The A/B tool (02) is
  the acceptance gate, zero tolerance.

**Port list (C++ reference → levelmesh VS), in classic order:**

1. Flat effective light: `sector_t::GetFloorLight/GetCeilingLight` (p_sectors.cpp) —
   `ABSLIGHTING ? planeLight : hw_ClampLight(lightlevel + planeLight)`; the lighting sector is the one the
   surface's `lightRef` (04) points at (transfer-light / heightsec semantics).
2. Wall base light: `side_t::GetLightLevel` (p_sectors.cpp:1579) — ABSLIGHTING global/tier branches,
   fake-contrast `rel` computed and *returned separately*, trailing `Light + TierLights[which]` add gated on
   `!is3dlight && !ABSLIGHTING && !TIERABS && (!foggy || LIGHT_FOG)`. `foggy = (frontsector->Colormap.FadeColor
   != 0) || (Level->flags & LEVEL_HASFADETABLE)`.
3. `CalcRelLight` (hw_walls.cpp:2100) — the 3-branch rel → rellight.
4. `RescaleLightLevel` (hw_lighting.h:35) — r_extralight cvar.
5. Weapon/extra light: `getExtraLight` (hw_lighting.h:51) = `r_viewpoint.extralight * gl_weaponlight`.
   Flats add it as the whole rel; walls add it to the fake-contrast rel (hw_walls.cpp:208
   `int rel = rellight + getExtraLight()`).
6. `CalcLightLevel` (hw_lighting.cpp) — the ELightMode branches (dark-mode integer path incl.
   RoundHalfEven(1.87f*L)/5 and the 20-clamp; the software-lighting path) → the uLightLevel value.
7. `CalcLightColor` (hw_lighting.cpp) — integer colormap math (LightColor/BlendFactor/Desaturation) → vColor.
8. `GetFogDensity` + `SetFog`/`SetShaderLight` (hw_setcolor.cpp:59,93) — distfogtable (256×2 f32 uniform
   buffer, rebuilt on `gl_distfog` change), MAXDIST = 32*r_visibility, the cullcolor branch
   (r_distance_cull_type > 1), outsidefog, fullbright, gl_fogmode.
9. uFogEnabled encoding (vk_renderstate.cpp:356-372): 0 = no fog; black fogcolor → +gl_fogmode (1 = view-depth
   fade, 2 = true distance); non-black → −gl_fogmode. Levelmesh: computed per vertex into vFogEnabled; the
   main.fp branches already key off it.
10. The 3D-light variants (band split / FF_FOG — below).

`ELightMode` is per level (`Level->info->lightmode`, `gl_maplightmode`, `gl_lightmode` →
`getRealLightmode`, g_level.cpp:151) → per-level uniform; `uPalLightLevels`/numShades → global uniforms.

### Sector record re-slot (round 1, approved; amends 05)

05 locked the slots and the per-frame write, and deferred the light *semantics* to 07. 07 replaces the
derived "effective light" slots with **raw ingredients** (the VS derives): 96 bytes, unchanged size.

```c
struct SectorState {                 // 96 bytes
	float	floorPlane[4];    //  0 (05)
	float	ceilPlane[4];     // 16 (05)
	float	floorScroll[2];   // 32 (05)
	float	ceilScroll[2];    // 40 (05)
	uint16	lightlevel;       // 48: raw sector->lightlevel (0..255)
	int16	planeLight[2];    // 50: [0] floor, [1] ceiling — secplane Light offset
	uint16	flags;            // 54: bit0 transdoor (05) | bit1 floor ABSLIGHTING | bit2 ceil ABSLIGHTING
	uint32	glowFloorColor;   // 56: packed PalEntry; 0 = texture-glow fallback, ~0u = glow disabled
	float	glowFloorHeight;  // 60
	uint32	glowCeilColor;    // 64
	float	glowCeilHeight;   // 68
	uint8	colormap[9];      // 72: FColormap — LightColor(3) BlendFactor(1) Desat(1) FadeColor(3)
	                           //     FogDensity(1); fire/pulse/damage thinkers write these per tick
	uint8	reserved[3];      // 81
	float	reserved[2];      // 84: 05's skyOffset slot — reserved (round-3 sky split)
	uint32	reserved;         // 92
};
```

Note: 05's `colormap uint32` (annotated "selfmap") is replaced by the real HW `FColormap` (9 B) — the HW
path never reads `selfmap` (a software-renderer field). Glow slots keep 05's fields and sentinels, shifted
for alignment.

### Surface record 48 → 64 B (round 1, approved; amends 04)

04's documented offsets contain an 8-byte hole (planeRef[4] spans 8..24 but lightRef is listed at 32); the
"48 bytes" total only reconciles with lightRef@24 / flags@28 / vparam@32. 07 normalizes the base layout and
adds a 16-byte wall-shading/sky block:

```text
offset  type     field
0       uint32   textureIndex   // (04, MUTABLE)
4       uint32   regionIndex    // (04)
8       uint32   planeRef[4]    // (04; spans 8..24)
24      uint32   lightRef       // (04; offset normalized)
28      uint32   flags          // (04; offset normalized) — bits below
32      float    vparam[4]      // (04; [3] reserved)
48      int16    light          // side->Light
50      int16    tierLight      // side->TierLights[tier]
52      int16    relSmooth      // baked fake contrast, smooth formula (build-time CPU)
54      int16    relNonSmooth   // baked fake contrast, classic formula (build-time CPU)
56      int16    lightlistRef   // 3D-light index into the level light buffer; -1 = none
58      uint16   bandOffset     // into the level band table (walls in 3D volumes)
60      uint16   bandCount      // 1 = ordinary wall (band 0 = this record, no split planes)
62      uint8    reserved[2]
```

64 bytes. Flats use only `lightlistRef` of the light block (rest reserved).

**flags uint32 bits:** 0 dynamic (04, MUTABLE) | 1 sky (04) | 2 hasLightmap (04) | 4-6 openingMask (04) |
7 unpeggedLower (04) | **8-9 tier** (0 upper / 1 middle / 2 lower) | **10 WALLF_ABSLIGHTING** |
**11-13 WALLF_ABSLIGHTING_TIER[3]** | **14 WALLF_NOFAKECONTRAST** | **15 WALLF_SMOOTHLIGHTING (side)** |
**16 WALLF_LIGHT_FOG** | **17 3D FF_FOG (flats)** | **18-25 lightmapNum** | **26-27 skyScrollKind**
(0 sky1 / 1 sky2 / 2 mist) | **28-29 skyScrollKind2 (doublesky second layer)** | **30 m2snf** (fake
infinite wall: fog runs at light 255 / rel 0 — hw_walls.cpp:291) | 31 reserved.

**Fake-contrast bake:** `rel = f(line delta, Level->WallHorizLight, Level->WallVertLight)` is static
(smooth: `RoundHalfUp(WallHorizLight + fabs(atan(dy/dx)) / 1.57079 * (Vert - Horiz))`, delta.X != 0;
classic: delta.X == 0 → Vert, delta.Y == 0 → Horiz, else 0 — p_sectors.cpp:1579). Both values are baked
CPU-side at build; there is no GPU atan. VS selection: `smooth = LEVEL2_SMOOTHLIGHTING || side smooth ||
r_fakecontrast == 2`; rel applies only when `(!foggy || LEVEL3_FORCEFAKECONTRAST) && !NOFAKECONTRAST &&
r_fakecontrast != 0`; rellight = `CalcRelLight(L, org, rel)` with org = raw (unrescaled) frontsector
lightlevel.

### 3D floor lights (round 2, locked: per-level light state buffer + band split)

**Light state buffer** (per level, full-copied per frame like 05): one 12 B record per
`Level->lightlist` entry — `uint16 lightlevel` (raw `*p_lightlevel`), `FColormap` 9 B (the light's
`extra_colormap`, re-copied at runtime by `P_RecalculateLights`), 1 B pad. These are live values (pointers
into source sectors; A_ChangeSector colormap propagation), so nothing is baked.

**3D-floor flats:** the surface's `lightlistRef` (baked at build — `P_GetPlaneLight` resolution is static
topology) → VS: `L = RescaleLightLevel(light.lightlevel)`; colormap = the light's (FF_FOG flag:
LightColor := light.FadeColor, other components 0, white FlatColor — hw_flats.cpp SetFrom3DFloor). Flats
do not split.

**Walls in a 3D volume = per-band sub-draws** (the classic splits them, hw_walls.cpp:305-325): the wall
quad is drawn once per intersecting 3D-floor layer, each clipped to that layer's z-band via
`uSplitTopPlane`/`uSplitBottomPlane` (main.vp:79-82 clip distances; the planes are the 3D-floor plane +
next-layer plane / front floor — dynamic, from the sector state records). Levelmesh expression:

- Build time: enumerate the intersecting 3D-floor layers per wall (static structure — 04 decision 4) →
  **M band records** in a per-level static **band table**: `{ int16 lightRef; int16 reserved; uint32
  splitTopPlaneRef; uint32 splitBottomPlaneRef }` (16 B). Split refs are 04 plane refs into the model
  sectors; `lightRef = -1` means the band's light has no caster → use the wall's own light (the classic
  `caster == nullptr` case). The base surface record keeps the tier statics and points at its band range
  (`bandOffset`/`bandCount`).
- IBO: sub-range granularity becomes (region, texture, **bandIndex**) — 08 consumes. Sub-range k contains
  the quads of walls having ≥ k+1 bands, in pool order. The vertex pool is unchanged (bands re-reference
  the same quads; only IBO indices repeat).
- VS (band k): light = `band.lightRef >= 0 ? RescaleLightLevel(lightBuf[i].lightlevel) : the wall's normal
  chain`; colormap = the light's FColormap **with the wall's own FadeColor/FogDensity** (the classic merges
  `thiscm.FadeColor/FogDensity = Colormap.*`, hw_walls.cpp:315-317); split planes = the sector-record
  planes of splitTop/BottomPlaneRef → ClipDistance3/4 exactly as main.vp:81-82.

### Parity strategy (round 2, locked: bit-exact port + A/B, zero tolerance)

- Integer paths (CalcLightColor, RescaleLightLevel, CalcRelLight, CalcLightLevel's Doom-dark integer
  branch, all clamps) use int32 arithmetic — bit-exact.
- Float paths (the 1.87f dark-mode product + RoundHalfEven, GetFogDensity, the distfogtable f32 lookups,
  sky scroll adds) replicate the C++ op order in float32 — bit-exact under IEEE-754 (both sides float32).
- distfogtable (256×2 f32) is a uniform buffer rebuilt on `gl_distfog` change; fake contrast is baked (no
  GPU atan); skytransfer resolution happens at build.
- Verification: A/B tool, zero tolerance, on the checklist below plus the cvar sweep.

### Sky (round 3, locked: sky = ordinary geometry)

Verified classic facts: sky is shaded like ordinary geometry (frontsector light + colormap; no special light
level). The dynamic per-frame sky state is just three per-level floats (`Level->hw_sky1pos / hw_sky2pos /
hw_skymistpos`, r_sky.cpp:146, advanced by R_UpdateSky). `sector->skytransfer` is a **static int** (MBF: 0
or `(lineIndex+1) | PL_SKYFLAT` — the sky texture comes from that line's sidedef with angle = texture X
offset × 360/65536, y offset, mirror flag; resolved by `HWSkyInfo::init`, hw_sky.cpp:56).

**Contract.**

- Sky surfaces (04 bit 1) resolve at build time exactly as `HWSkyInfo::init` does (which sky texture —
  incl. the sky2/doublesky selection from LEVEL_SWAPSKIES/LEVEL_DOUBLESKY/PL_SKYFLAT and the MBF
  line-texture case) → the static parts (texture, angle offset, y offset, mirror) fold into the pool's
  baked u/v and textureIndex (the existing static bake handles them); the surface record carries
  `skyScrollKind` / `skyScrollKind2` (which of the three per-level scroll positions drives the surface, and
  the doublesky second layer).
- VS: `u += levelSkyPos[skyScrollKind]` (+ second layer for doublesky), with `levelSkyPos = (hw_sky1pos,
  hw_sky2pos, hw_skymistpos)` a per-level per-frame uniform triple. This is the locked round-3 split:
  **no per-frame sector-record write for sky scroll** — 05's `skyOffset` slot is reserved, and scrolling-sky
  levels churn zero record bytes.
- Sky fadecolor rule (hw_sky.cpp:116-126) as a VS branch on sky surfaces: `r_distance_cull_type > 1` →
  `FadeColor > 0 ? FadeColor : gl_cullcolor`; else `Level->skyfog > 0` → the sector FadeColor; else none.
- **Skybox** (PORTS_SKYVIEWPOINT/HORIZON/PLANE, GLSector_Skybox) = a 06 portal view: ordinary flat/wall
  shading inside, plus the inkybox fog-density ×1.5 boost and cullcolor as per-view uniforms.

### Locked by code investigation (no decision needed)

- **Glow:** `sector_t::GetWallGlow` (p_sectors.cpp:1251) — sector `GlowColor` (sentinels: 0 = fall back to
  the *texture's* glow, ~0u = disabled) + `GlowHeight`; the shader receives color+height (height as the
  vec4 alpha) and the glow planes are the frontsector's **actual (tilted) floor/ceiling planes**
  (hw_walls.cpp:45-51, 210-214). Levelmesh: the sector record carries color+height (sentinels intact); the
  planes come from the same record fetch; the texture-glow fallback uses a per-texture glow table (glowColor
  u32 + glowHeight f32 per texture, built at load, refreshed by the snapshot pass for animated glow frames).
  Glow is **walls only** — the HW classic has no flat glow; the levelmesh does not add one.
- **Dynamic lights (dlight_t):** per-pixel fragment work against the global per-frame light buffer
  (`ProcessMaterialLight`, main.fp) — traversal-independent, unchanged. The level pass uses `uLightIndex = 0`
  (the classic never sets it for walls/flats; decals/sprites keep their own indices on their existing path).
- **Selfmap:** a software-renderer field — never read on the HW path; no levelmesh analogue.
- **Animated flats:** 04's mutable textureIndex (snapshot pass advances the frame); no 07 involvement.
- **Scrolling:** 05's per-plane scroll + VS `uv += scroll` (scroll sectors are load-dynamic per 05).
- **Transdoor:** 05's flag + the classic z-1 floor offset in the VS.
- **Weapon light:** per-view (`r_viewpoint` is per player in net) — per-view uniform.
- **culldist / fullbright / inkybox:** per-view/per-level uniforms as listed below.
- **WALLF_EXTCOLOR gradients, TextureFx, NPOT emulation:** the texture-manipulation domain (08 / texture
  handling); the gradient planes they need are already in the sector record.

### Uniform set (spec-ready)

- **Global (rebuilt on cvar change):** distfogtable[2][256] f32; uPalLightLevels (lightmode pack);
  numShades.
- **Global per-frame:** the dynamic-light buffer (the classic lightbuffer — unchanged).
- **Per level (static):** lightmode (ELightMode int); outsidefog, outsidefogdensity, sector fogdensity
  (Level->fogdensity), skyfog. (The level *flags* — HASFADETABLE, SMOOTHLIGHTING, FORCEFAKECONTRAST,
  DOUBLESKY, SWAPSKIES — are baked at build, not uniforms.)
- **Per view (dynamic):** r_visibility (→ MAXDIST = 32*r_visibility), r_distance_cull_type, gl_fogmode,
  r_fakecontrast (mode selector), r_extralight, gl_weaponlight, r_viewpoint.extralight, culldist,
  fullbright-scene flag, inkybox flag (skybox views).
- **Per level (dynamic):** levelSkyPos (3 f32); the 3D-light state buffer (12 B × light count);
  **cullcolor** (1 value) — the fog/clear-color latch written per frame per view by 11's exposure pass
  (classic `hw_bsp.cpp:378-392`: `r_distance_cull_type > 0` && `IsDistanceCulled` && nonzero fade color
  → `Level->cullcolor = FadeColor`; `gl_cullcolor`-initialized, latching, never reset per frame). Fog-
  color selection rule to reproduce (`hw_setcolor.cpp:108`): `gl_distance_cull_type > 1` → fog color =
  `Level->cullcolor`.
- **Per surface:** everything else, from the records above.

### Parity checklist (A/B, zero tolerance)

Maps: lighting stress (D2), skybox (D3), lightmap, fog/fade (outsidefog + FadeColor), glow (sector +
texture + animated), 3D floors (multi-layer → band split), animated/scrolling flats, transfer light
(sector_link light), fake contrast (all line angles incl. axis-aligned).
Cvar sweep: r_lightmode 0/1/2/8, gl_fogmode 0/1/2, r_fakecontrast 0/1/2, r_extralight ±, gl_distance_cull
0/1/2, gl_weaponlight 0/1, lightmaps on.
Runtime: strobe/pulse/fire/glow/damage thinkers active during the diff runs (frame-locked).

### Handoffs to dependent tickets

- **08 (draw organization):** sub-range granularity (region, texture, bandIndex); the band table +
  uBandIndex draw uniform; the per-view uniform list; levelSkyPos; fullbright/inkybox per view; the fragment
  adaptation block (#define promoted uniforms → varyings) is the levelmesh shader variant 08 wires up.
- **09/10 (scale/perf):** validate the per-vertex sector-record fetch (all surfaces) and the band IBO
  repetition at slaughter scale.
- **02 (A/B tool):** the checklist above is the acceptance input.

### Amendments (post-resolution, ticket 11 — 2026-10-04)

- **cullcolor is per-level per-frame dynamic**, not static (corrected in the uniform set above).
  Written by 11's exposure pass from the culled draw list (frustum-only gate — documented delta vs the
  classic's occlusion gate, D6). Consumers unchanged: view clear color (`r_utility.cpp:1294-1295`) and
  fog color (`hw_setcolor.cpp:108`).

### Decision provenance

Round 1: light-chain placement = **port into the levelmesh VS** (recommended option chosen over CPU
per-sector pre-compute and per-surface pre-compute); sector-record re-slotting **approved** (96 B, raw
ingredients, FColormap 9 B, ABSLIGHTING → flags); wall statics = **surface record grown 48 → 64 B** (chosen
over a separate shading SSBO and vparam packing).
Round 2: 3D lights = **per-level light state buffer** (chosen over baking at load and over sector-record
extension); parity = **bit-exact port + A/B zero tolerance** (chosen over ULP-tolerant fog and ±1-step
tolerance); sky = **ordinary geometry** (chosen over fixed-light sky and a separate sky pass).
Round 3: sky scroll = **split — static skytransfer/angle resolved at build + per-level per-frame
levelSkyPos uniform, no per-frame record write** (chosen over per-frame record writes); full spec
**approved** including the glossary update.
