# Ticket 03 — GL33 levelmesh draw pass: diagnosis plan

Date: 2026-10-10. Status: in progress (blocker phase).
Map used throughout: `DOOM2_MAP01`, frame 8, backend gl33, mode static.

## The three problems

| # | Symptom | Current evidence |
|---|---------|------------------|
| P1 | Majority of walls invisible (not rendered or rendered off-screen) | A shows full pillar/back-wall scene; B draws only the left near wall face + stair risers. Wall-only test (flats skipped) → same. Draw list contains entries for the missing groups and draw calls are issued with plausible VAO/IBO offset+count. |
| P2 | Textures appear as flat colors | With `vColor = 1` forced in VS, floor/ceiling render as uniform tan, left wall as uniform gray-white, a few quads as solid blue. No texture pattern anywhere. Blue garbage suggests a wrong/unbound unit being sampled on some draws. |
| P3 | Bottoms of walls missing triangles | Latest frame: large black holes at the base of the scene (floor/wall junction); earlier frames: wall quads with notched/cut lower edges. |

## Already ruled out (do not re-derive)

1. Bake inputs are correct: per-seg z ranges, x/y scales, texture IDs all validated against live level data (LMDIAG bakes).
2. Surface records are clean: raw byte dumps of visible vs invisible surfaces show valid `light=128`, `tierLight`, `lightlistRef=-1`, sane `planeRef`/`lightRef`/`flags`. Earlier "garbage float" readings were a printf format bug (int16 passed to `%f`), not data.
3. Sector-state ring unpacks correctly; lightlevels match live sectors.
4. IBO structure for region 9 (invisible group) is clean and contiguous: 27 indices, 3 quads, world positions sane (e.g. v342..v345 = wall (-448,576,56)-(−576,704,264)).
5. It is not flat occlusion: skipping flats in the draw loop changed nothing for walls.
6. The white-override shader edit **is** reaching the GPU (verified present inside `build/uzdoom.pk3`), so "shader edit did nothing" means the constant we see is the *texture sample*, i.e. P2 is real and upstream of lighting.

## Open suspects by problem

P1:
- S1.1 Per-region vertex-pool offset mapping in the VAO/IBO upload (visible quads may be a lucky subset; the tan slivers flying on the right of the latest frame look like walls drawn at wrong positions).
- S1.2 IBO span non-contiguity: many `texRange` spans are far larger than the sum of their members' index counts (e.g. region 0 tex 3030: span=264, sum=72). If surfaces are interleaved by build order rather than grouped by texture, each group's span overdraws other groups' triangles — and if a group's `iboOffset` points past real data, its own triangles never get drawn.
- S1.3 Frame-build culling (AABB / portal / frustum) dropping entries per frame even though they exist at build time.
- S1.4 Winding / `frontFace` mismatch: back-face culling eating most quads while a few survive by orientation luck.
- S1.5 Depth-order interaction: floor drawn later with depth mask clobbering walls (or vice versa). The black holes may be floor triangles *over* wall bases.

P2:
- S2.1 Sampler-unit collision: C++ binds the three record texture buffers to fixed units 12–14; if `FShader` assigns a material layer (`SetMaterial(tex, UF_Texture)`) to one of those same units for this effect, the wall sample reads a buffer-backed/placeholder texture → constant color. Blue garbage = a different unit's contents.
- S2.2 `SetMaterial` state not actually applied before `DrawLevelMesh` (buffered state / `ApplyShader` ordering), so `tex` keeps stale contents from another pass.
- S2.3 UVs collapsed to one texel: baked UVs are sane for the checked quads, but negative UVs (-7..-14) seen on region 9 tex 3030 must be verified against what classic bakes; if the VS re-maps them through a scale that zeroes the range, everything samples one pixel.
- S2.4 The flat tan is literally one texel of the real floor texture (techfloor has tan pixels) — i.e. sampling works but UVs are degenerate for most quads. Distinguish with S2.3's test.

P3:
- S3.1 Unpegged-lower vparam math in the VS (`V = z - TexZ + vparam`) displacing bottom vertices; `vparam[0]` is 48 on the visible lower wall, 0 on mids — the formula needs a side-by-side against classic `R_Sector` corner z's.
- S3.2 Opening-mask (mid-window) two-band emission: second band of a split wall missing or mis-emitted (`LMF_OPENING_MIDDLE` surfaces exist in the records).
- S3.3 Floor quads actually missing (the holes are in the *floor*, at the wall base line): floor quad emission or its IBO range dropping triangles.

## Plan

### FINDINGS (2026-10-10 session 2, phases 1-4 interleaved)

Fixed / verified:
- **S1.3 DEAD**: draw list is 139 entries, identical frames 1-10, 0 skipped.
- **S1.2 REAL + FIXED**: texRange spans were bounding intervals of interleaved
  BSP-order sub-ranges (region 0 spans [36,300)/[72,210)/[45,306) overlapped).
  Fix: permute each region IBO into texture-sorted order in BuildSubRanges so
  spans are exact [first, first+sum). Post-fix: 139/139 contiguous, monotonic.
- **CRASH found+fixed**: first permutation attempt used TArray::Reserve(n) as
  capacity prealloc — GZDoom's Reserve APPENDS n elements, so each region IBO
  doubled with a zero-index head → DoomLevelMesh raytrace ctor segfault
  (signal 11 at map load) + garbage state. Lesson: never Reserve-then-Push.
- **S1.4 DEAD**: Cull_None for the pass = byte-identical output (culling was
  already effectively off / no back-facing geometry among drawn tris).
- **S1.5 DEAD**: DF_Always for the pass changes almost nothing (113→111
  missing). Depth ordering is not hiding the walls.

Root causes identified (not yet fixed):
- **P1 ROOT — one-sided walls never emitted**: BuildWall returns when
  backsector==nullptr; classic draws these as solid mid-texture walls
  (front floor→ceiling). Hue AABB map: EVERYTHING east of the camera
  (courtyard, all outer walls) missing; visible = flats + few two-sided bands.
  Also: tex 3141 draws in region 10 but not 16 → not texture/material; same
  VAO shows floor span but not wall spans → not VAO/region level.
- **P3 ROOT — corner-order mismatch**: BakeWallUVs wants z in classic
  [bl,tl,tr,br] order; emitters pack [v1lo,v1hi,v2lo,v2hi]; cornerMap
  {0,3,1,2}/{2,3,0,1} then cross-assigns v1/v2 top/bottom heights →
  bowtie/sheared quads (notched bottoms) + swapped v-coords on v2 corners.
- **P2 ROOT — VAO aTexCoord misbinding**: LevelMeshVertex has slotB@12,
  u@16 non-adjacent; D1 bound aTexCoord=(u@16,lmu@20) so VS slotB reads lmu
  (≈0 without lightmaps) → every surface samples texel row v=0 → flat colors.
  (Flats bake v=world/64 into slotB too → also flat.) Fix: reorder struct to
  x,y,slotA,u,slotB,... and bind aTexCoord@12.

Remaining pipeline notes: mGpuPool repack of surfaceIndex is fine;
LMF constants match C++/GLSL; LOADDYN alias correct.

### FINDINGS (2026-10-10 session 3) — P1 and P2 fixed, lighting is next

- **P1 REAL ROOT — `IsLoadDynamic` tested `planes[].alpha > 0`.** Plane alpha
  defaults to 1.0 at load (maploader.cpp:1105, udmf.cpp:1667), so EVERY
  surface got LMF_LOADDYN. Walls: EmitWallQuad(dyn, vparam=nullptr) wrote
  slotA=slotB=0 on all 4 corners, and the VS dynamic branch computes
  z = tracked.texZ + surf.vparam[tier] (per-SURFACE) -> all corners same z ->
  zero-height quads. Flats survived because EmitFlatFan always bakes z.
  (recon-02 sec. 8c misread hw_flats' `alpha != 0` "draw plane at all" gate
  as a translucency test.) Fix: `GetAlpha(plane) < 1.`. Frame-8 B dark
  fraction 62% -> 1.7%.
- **P2 REAL ROOT — `VATTR_TEXCOORD` never enabled** in FGLLevelMesh::SetMesh
  (pointer set, no glEnableVertexAttribArray). A disabled attribute reads the
  constant (0,0,0,1) -> u = slotB = 0 for every vertex -> one texel per
  surface. The struct reorder + offset 12 fix was right but inert until this.
  Verified with a UV probe (uLMDiagGroup == -2 -> fract(vTexCoord) as RG: all
  black before). Post-fix gradient energy B 0.14 -> 5.62 (A 2.04).
- **P3 partial — EmitWallQuad indexed uv by emission slot k, z by corner c.**
  With cornerMap {0,1,3,2} the two v2 vertices swapped v. Fixed: uv[c].
  (lmuv: FillLightmapUV fills pool order [LOLFT,LORGT,UPLFT,UPRGT] but is
  indexed by classic c — wrong too, inert while lightmaps are off; TODO.)
- Removed the S1.4/S1.5 Cull_None / DF_Always test overrides.

Run 20261010T204805Z-ab (frame 8): edge-map corr vs A 0.61, luminance corr
0.41. Remaining dominant diff: **B ~3x brighter, no distance falloff**
(B/A cell ratio 0.3..6). Next: lighting chain (vLightLevel / vLightFactor /
vLightDist / fog in the fp vs classic SetShaderLight/SetColor), then re-check
P3 wall bases once brightness matches.

### FINDINGS (2026-10-10 session 4) — lighting + wall tiers fixed; frame ~matches A

- **Lighting chain (levelmesh.vp) vs classic SetColor/SetFog**, all fixed:
  - `vLightLevel` was raw hwlightlevel 0-255; the fp's R_DoomLightingEquation
    takes the NORMALIZED `hw_ClampLight(light+rel)/255`, -1 off (non-software
    mode or blendfactor). Default gl_lightmode 1 = ZDoomSoftware -> colormap
    was pinned full-bright -> the 3x brightness + no falloff.
  - fog density not pre-scaled: classic uFogDensity = d * (-LOG2E/64000).
  - SetShaderLight ran unconditionally with swapped args; classic runs it only
    under fog-on && (Doom || soft+blend) && black fog, with
    (CalcLightLevel(weapon=false), sector light); Build = (0.2d, 1/31);
    else (1, 0).
  - LEVEL_HASFADETABLE fog case added; LEVEL3_NOLIGHTFADE (0x8) and
    LEVEL2_SMOOTHLIGHTING (0x01000000) macros were bit 0.
  - vLightmap.z = -1 unless LMF_LIGHTMAP (was 0 -> LightMap always sampled).
- **Upper/lower walls (BuildWall) ported verbatim from HWWall::Process**:
  upper used bch*a = fch* + bogus clamps and an inverted condition -> closed
  doors / upper tiers never emitted (the centre-screen hole). Lower used the
  unclipped back floor as its top. Texture anchors (texturetop) were wrong for
  both tiers in both peg states; now DoTexture's formula exactly (RowOffset,
  flh, v_offset incl. the both-ceilings-sky case). Upper skipped when both
  ceilings are sky (classic).
- **Per-seg u span**: BakeWallUVs used frac 0..1 per seg but texlength spans
  the whole line -> every split seg had the full line's texture squeezed in
  (stair risers). Now frac from the classic major-axis formula.

Run 20261010T210128Z-ab, frame 8: 14,352 px differ, none > 24/255.
- 14,109 px (98.3%): **dynamic light** (green glow on the right wall behind a
  thing). The draw pass pushes uLightIndex -1, so no dynamic lights. Spec
  (digest-05-06:89/127) says the level pass "locks uLightIndex 0"; needs a
  design decision (see below).
- 243 px: scattered single-pixel edge/rasterization diffs, mostly <= 5/255.
- The tool still counts 228 clusters per tick (zero-tolerance per ADR 0004).

Open decision: dynamic-light model for the levelmesh pass (per-surface light
list index vs a global list / the spec's uLightIndex = 0 lock). Then decide
whether the residual edge pixels are allowlist-eligible or a T-junction /
per-seg vs per-line emission artefact (classic emits whole lines).

### FINDINGS (2026-10-10 session 5) — dynlights deferred; seams down to junctions

- **Dynamic lights: DEFERRED to `issues/12-dynlights-clustered-forward.md`**
  (user decision: levelmesh dynlights break from classic, clustered forward+).
  The spec §6 lock "uLightIndex = 0, classic never sets it for walls/flats" is
  factually wrong (hw_walls.cpp:94/370/539) and is to be rewritten by 12.
  `tools/abtest.py` PIN_TEMPLATE now pins `r_dynlights 0` on BOTH paths until
  12 lands (removal is in 12's acceptance).
- **Per-side wall emission**: classic draws non-polyobject walls once per LINE
  from the linedef vertices (hw_bsp.cpp validcount); the builder emitted per
  seg -> interior vertical seams in split walls. BuildWall now dedupes per
  side (`sideEmitted`) and emits through a proxy seg spanning the line;
  polyobject sides stay per seg (classic).
- **Wall diagonal**: classic fan LOLFT,UPLFT,UPRGT,LORGT -> diagonal
  LOLFT-UPRGT; EmitWallQuad used UPLFT-LORGT. Now (0,1,3),(0,3,2).

Run 20261010T211734Z-ab (r_dynlights 0): every tick 171 px differ, max 17/255,
170 clusters. All on surface junctions (stair step-floor/riser-top edges,
 far-room window corners, pillar face corners); none across open floors, so
the flat triangulation difference (classic: earcut over FSection, levelmesh:
subsector fans) does not show on MAP01. Candidate ADR 0004 near-coincident
class -> needs HUMAN review: `abtest.py record DOOM2_MAP01 static gl33 <a> <b>`
writes `allowlists/pending/DOOM2_MAP01.json` for review/promotion.

Known parity gaps left (not visible on MAP01, revisit on other maps):
- flats: earcut sections vs subsector fans (interior edge interpolation).
- classic gl_seamless edge splits (SplitLeft/Right/Upper/LowerEdge vertices).
- two-sided masked mid textures: builder spans floor..ceiling, classic
  DoMidTexture clamps to the texture height within the opening.
- upper walls under a sky front ceiling still get LMF_SKY (sky u scroll).
- lightmap UV corner order (FillLightmapUV) inert while lightmaps are off.

Latent (not hit on MAP01, must fix before real dynamic sectors work):
- Dynamic walls: EmitWallQuad is always called with vparam=nullptr, and the
  VS uses one per-surface vparam for all 4 corners -> any truly load-dynamic
  wall is still a zero-height quad. Needs per-corner vparam (top/bottom
  plane + offset) in slotA.
- Dynamic flats: VS dynamic branch overwrites the flat's baked v with the
  wall window formula and tracks pFF for ceilings too.
  (Both RESOLVED in session 6, see below.)

### FINDINGS (2026-10-11 session 6) — diagnostics removed, known gaps closed

All LMDIAG code removed (diag-only files restored to HEAD). From here on the
smoke test is a clean build (user decision); A/B is not re-run per change.

Fixed:
- **Frame cull**: LevelMesh_CalcCullVP rotated yaw about (0,-1,0); classic
  SetViewMatrix uses (0,mult,0). The cull mirrored the view (MAP07 all black,
  MAP31 door gone). LMAABBInFrustum is now conservative (all corners outside one
  clip plane), GPU-space swizzle (x,z,y), z in [-32768,32768]. MAP07 now
  byte-identical.
- **Live light**: walls/flats evaluated side_t::GetLightLevel /
  GetFloor/CeilingLight from BAKED sector light; light thinkers never reached
  them. Now raw ingredients (side Light/TierLights, side flags LMF_SIDE_*, both
  fake-contrast rels) + live sector record (spec §6 port list). MAP27 2412 ->
  56 px. (GLSL gotcha: `smooth` is a reserved word.)
- **Frame animation (A)**: surfaces store the untranslated texture id; the draw
  resolves GetGameTexture(id, true) per group.
- **HWWall::Process port**: one-sided/two-sided split, DoTexture,
  SetWallCoordinates (incl. plane-intersection clipping), CheckTexturePosition,
  DoMidTexture (texture-height clamp, alpha gate, translucent mids left to the
  classic translucent pass), skews, LEVEL3_FORCEWORLDPANNING,
  COMPATF_MASKEDMIDTEX, sloped untextured upper/lower drawn with the flat
  texture. Per-group draw state (LMDS_*): sampler clamp (flags & 3),
  TM_CLAMPY for M2S, masked-pass threshold by classic list routing.
- **Load-dynamic geometry** (ADR 0002 warp): sector record grows to 40 floats
  (live floor/ceil TexZ at [36..37]); VS ZatPoint sign fixed; per-corner
  tracked plane slot packed in vparam[0] (vertex corner index packed with
  surfaceIndex in the GL repack); v = (anchorTexZ + vparam[2] - z) * vparam[3]
  + slotB. Dynamic flats track their own plane, keep baked v; transdoor -1.
- **Sky walls**: classic sky walls are sky portals from the still-running BSP
  walk; LMF_SKY removed from textured upper walls. Translucent flats
  (alpha < 1) excluded (classic translucent-border pass draws them).
- **Missing upper/lower fills**: SSRF_PLANEHACK/FLOODHACK flats drawn from the
  classic lists on the levelmesh path (DrawHackFlats).
- **Flats**: classic has no section earcut (TriangulateSection is a TODO);
  ported CreateVerticesForSubsector: <3-line skip, earcut for SSECF_HOLE,
  fan (0,k-1,k), transdoor z-1, per-vertex flat lightmap uvs.
- **gl_seamless edge splits** ported for static walls (read at map load).
- **Polyobjects**: excluded from the mesh; classic polyobject walls drawn from
  the classic lists (DrawPolyobjWalls). Fixes HEXEN_MAP01 door.

Deferred:
- `issues/13-runtime-texture-state.md`: texture swaps (spec contradiction),
  wall/flat UV transforms (MAP30 scroller), material shader variants (warp).
- Fog boundary sheets, DoMidTexture GetAreas vertical slicing, 3D-floor
  BuildFFBlock port (05), line portals/horizon (06).
- Lightmap page index not in the vertex (VS assumes layer 0).
- Dynamic walls skip seamless splits (4-corner warp only).

### Phase 0 — Reset to a known baseline (30 min)
1. Revert all temporary shader hacks in `wadsrc/static/shaders/glsl/levelmesh.vp` (white/flat-color overrides, the inert `#define getTexel` stub). Keep lighting path as-is.
2. Keep the C++ `LMDIAG-*` logging for now; it is cheap and on the build side only.
3. Build + capture once. Expected: the known-broken baseline (few walls, flat colors, holes). This is the reference frame for all later diffs.

### Phase 1 — Per-group identity coloring: one experiment that localizes P1, constrains P2/P3 (1 h)
Goal: see *which* texRange groups draw where, and in what shape.
1. In `levelmesh.vp`, color by group ID: pass a per-draw uniform `uLMDiagGroup` (set from the frame build/draw loop = running index into `drawList`) and output `FragColor = hue(uLMDiagGroup/N)` in `levelmesh.fp` (bypass texture + lighting).
2. Build, capture, view frame 8. Read off:
   - which groups are visible → P1's missing set is now explicit per group;
   - group shapes → notched/missing bottoms (P3) and displaced quads (S1.1) become directly visible;
   - whether *all* groups render as distinct hues → if yes, P2 is 100% texture-side.
3. Cross-reference the visible hue set with the `LMDIAG-draw` list: for each missing group record region/tex/offset/count and its world AABB.
4. Record findings in this file (fill the table below).

| group | tex | region | off/cnt | world AABB | on screen? | shape defect |
|-------|-----|--------|---------|-----------|------------|--------------|
|       |     |        |         |           |            |              |

### Phase 2 — Texture/units audit: fix P2 (1 h)
Order matters: after Phase 1 we know P2 is texture-side.
1. Static audit first (no build): in `gl_framebuffer.cpp`/`FShader`, list every unit the levelmesh effect binds: material layer units from `SetMaterial`, the record buffers at 12–14, lightmap/bright/detail/glow if attached. Verify no overlap; check where `FShader::Load` pins units for `EFF_LEVELMESH`.
2. If a collision exists → move the record buffers to free units (update C++ binds + the sampler declarations in the shaders). This is likely the one-line root cause of P2.
3. If no static collision: runtime probe — after `SetMaterial` + `DrawLevelMesh`, log `glGetUniformLocation` value for `tex` and the texture bound to that unit; compare against the expected FGameTexture per group.
4. Re-capture with real lighting+textures restored. Exit criterion: wall/floor textures show patterns (not uniform color), matching A's palette at roughly correct brightness.

### Phase 3 — Wall-bottom geometry audit: fix P3 (1 h)
1. Pick one visible wall in A whose base is cut in B (stair-adjacent lower wall is the current example; surf 0, `vparam[0]=48`).
2. Dump its four corner z's from `EmitWallQuad` and compare against classic `R_Sector`'s computed corner heights for the same seg (front floor/ceiling, back floor/ceiling, unpegged handling). One C++ dump, no GPU involvement.
3. Check the VS vparam formula for static walls (`V = z - TexZ + vparam`) against what the bake already put in slotA — if the bake bakes final z's and the VS re-applies vparam, that is a double-application bug.
4. If corners are right: inspect opening-mask band emission (S3.2) for `LMF_OPENING_*` surfaces; verify both bands' triangles exist in the IBO with correct z's.
5. Verify the floor holes (S3.3): dump the floor group's IBO span + member sum; if the span logic of S1.2 already explains it, this is the same fix as P1.
6. Exit criterion: wall bases and floor junction match A within allowlist tolerance.

### Phase 4 — Invisible walls: fix P1 (1 h)
Work the suspects in order of likelihood given Phase 1 data:
1. S1.2 first if Phase 1 showed whole groups missing while their spans overlap neighbors: verify `BuildSubRanges` span math and the IBO layout it assumes; if surfaces are interleaved, either group emission by texture or change the draw to per-surface sub-ranges (the `subRanges` records already exist for exactly this).
2. S1.1: verify per-region vertexOffset → VAO buffer offset/stride in `GLLevelMesh` upload; render one known-missing group alone (draw only that entry) and see where it lands.
3. S1.4: toggle `glDisable(GL_CULL_FACE)` for the pass — if walls pop in, frontFace/winding is the bug; fix winding or the per-region `frontFace` state.
4. S1.3: log the frame build's per-entry cull decisions vs the build-time list.
5. S1.5 last: swap floor/wall draw order and/or depth mask; confirm with a two-color test (floor green, walls red — the machinery already exists from Phase 1).
6. Exit criterion: all groups from the Phase 1 table render on-screen at their expected positions.

### Phase 5 — Convergence + cleanup (30 min)
1. Remove every `LMDIAG-*` diagnostic and temporary hook (levelmesh.cpp, gl_framebuffer.cpp, shaders).
2. Full clean build: `cmake --build build --config RelWithDebInfo --parallel 3` (always a separate invocation from any test run).
3. A/B: `python3 tools/abtest.py run --map DOOM2_MAP01 --mode static --backend gl33` — iterate until pass or the remaining diffs are allowlist-eligible; document any new allowlist entries per ADR 0004.
4. Second map sanity check (e.g. a non-DOOM2 map with midtex/openings) before closing ticket 03's chunk F.

## Standing rules for this work
- Build and test runs are **separate bash invocations**, never combined.
- Build: `cmake --build build --config RelWithDebInfo --parallel 3`.
- Test: `python3 tools/abtest.py run --map DOOM2_MAP01 --mode static --backend gl33`, read `b/stdout.log` + `b/png/MAP01_8.png` from the newest run dir.
- Shader edits only take effect after a rebuild (PK3 repack).
- Do not trust `%f` prints of int16 fields; use byte dumps or matching formats.
