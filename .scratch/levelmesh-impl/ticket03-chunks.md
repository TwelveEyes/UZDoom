# Ticket 03 — chunk plan (subagent orchestration)

Sequential blocking `delegate` subagents, one per chunk. Each chunk ends with:
clean build + a handoff file under `.scratch/levelmesh-impl/`. Parent commits between chunks.

Common rules for every chunk:
- Repo: UZDoom at /home/gene/Development/Git/doom/UZDoom. Read ticket
  `.scratch/levelmesh-impl/issues/03-first-gl33-draw.md` first, then the spec sections cited below.
- Tabs not spaces; LF; final newline; no trailing whitespace. Do NOT run tools/format-spaces.sh.
- Build: `cmake --build build --config RelWithDebInfo --parallel 3` (build dir already configured).
- No test runner exists; a warning-free build is the bar. No commits from subagents.
- Use `knowledge_search` / `knowledge_symbol_search` before grep for codebase facts.
- Handoff file must contain: what changed (files), key design decisions, exact API surface added
  (signatures/layouts), known gaps deferred to later chunks, and build result.

## Chunk A1 — record fields (planeRef, vparam, lightRef, flags) → handoff-a1-records.md
Files: `src/common/rendering/levelmesh.h`, `levelmesh.cpp` (ticket 02's builder).
EXTEND the existing builder; do not restructure or rewrite it. Keep existing function names.
1. planeRef[4]: encoding sectorIndex<<1|planeBit. Flat: [0]=own floor, [1]=own ceiling;
   wall: front.floor/front.ceil/back.floor/back.ceil; 3D-floor fake flats + boundary quads →
   model sector's planes. Flats store a sentinel (0xFFFFFFFF) in [2]/[3].
2. vparam[4]: [0] lower-unpegged offset rel. front floor, [1] middle window offset,
   [2] upper window offset, [3]=0. Classic semantics: unpegged/window offsets that do not
   track a plane (see how the wall UV bake computes texturetop for pegged vs unpegged walls).
3. Reconcile LMF_* flags with spec section 3 table: openingMask 4-6, unpeggedLower 7, tier 8-9.
   Existing LMF_ISFLAT/LMF_POLYOBJ/LMF_3DFLOOR get free bits (note in handoff). Set the new
   wall bits from classic seg/side data.
4. lightRef: sectorIndex<<2|slot resolved through heightsec transfer-light at build (see how
   the classic renderer picks a wall's effective light sector).
NOTE: `1 << 31` does not narrow into a uint32_t enum — use static_cast<uint32_t>(1u << 31).
Acceptance: clean build; handoff lists every record field now populated + flag table as implemented.

## Chunk A2 — structural fixes (flat z, sub-range table, actor list) → handoff-a2-structure.md
Files: same two. EXTEND only; no rewrites.
1. Flat fan bakes ONE z from a reference vertex — wrong for slanted planes (3D-floor model
   sectors). Bake per-vertex z = plane.ZatPoint(x,y).
2. Sub-range table: region records need the per-(region,texture) {textureIndex, iboOffset,
   iboCount} sub-range table (spec section 3 region record); group by textureIndex within a
   region. Keep per-sub-range world AABBs.
3. Region actor list stores mo->tid — replace with subsector indices (region-sorted);
   draw pass walks each subsector's live sprite list.

## Chunk B — sector state ring + snapshot pack → handoff-b-state.md
Files: new `src/common/rendering/levelmesh_state.h/.cpp` (or extend levelmesh) + CMakeLists if needed.
- 96-byte sector state record: floor+ceiling plane (normal, D, TexZ), light raw ingredients
  (lightlevel + per-plane Light offsets + FColormap). Spec §4 owns layout — read it.
- Ring of HW_MAX_PIPELINE_BUFFERS slots; full-copied every frame (ADR 0003: byte-identical
  across runs); single current CPU-interpolated values.
- Initial fill from level at build time (spec §3 build order step 5).
- 3D-light state buffer, 12 B/light (live lightlevel + extra colormap), packed into the frame
  record for ticket 05 to consume.
- Expose a `PackSnapshot(level)` entry point that Chunk E's frame build calls inside the
  interpolation window.
Acceptance: clean build; handoff has the exact record layout + API.

## Chunk C1 — levelmesh VS (light chain port) → handoff-c1-vs.md
Files: new shader source under `wadsrc/static/shaders/glsl/` (+ registration if the build needs it).
Spec section 6 is the contract: the bit-exact port list.
- Levelmesh vertex shader: fetch the surface record (samplerBuffer) + the sector state record
  every surface; bit-exact classic light chain (effective light → RescaleLightLevel →
  CalcRelLight → CalcLightLevel → CalcLightColor → fog/cullcolor/glow); int32 on the integer
  paths, C++ op order in float32 on the float paths; C++ (src/rendering/hwrenderer/scene/
  hw_lighting.cpp + the wall/flat DoTexture light sections) is the source of truth.
- Load-dynamic warp from sector state: z = dot(normal, xy) + D, V = z - plane.TexZ + vparam;
  window clamps from the four evaluated planes + per-surface vparam/openingMask/unpegged.
- Sky scroll u += levelSkyPos[skyScrollKind] + the sky fadecolor rule.
- Emit every varying main.fp needs (positions through the existing view/clip transform) plus
  the promoted per-surface light/fog/glow/desat values.
- Read the port list in spec section 6 function by function; keep the GLSL names mirroring the
  C++ names. Do not "improve" any formula.
Acceptance: clean build; handoff has the varying list + formula-by-formula mapping table.

## Chunk C1b-fix — VS fetch to corrected layout + glow → handoff-c1b-fix.md  [DONE c22c198]
Files: `wadsrc/static/shaders/glsl/levelmesh.vp`.
- LMFetchSector repacked to the 35-float slot order of the corrected 96-byte record (9 texels/sector).
- plane texZ = D; glow colors/heights wired from front sector; V formula verified vs SetWallCoordinates.

## Chunk C2 — fp adaptation + shader wiring → handoff-c2.md  [DONE c22c198]
Files: the levelmesh .fp under `wadsrc/static/shaders/glsl/` + FShaderManager wiring.
- fp adaptation of main.fp: consume the promoted per-vertex varyings for light/fog/glow/desat;
  all per-pixel fragment math stays UNTOUCHED (same order, same formulas).
- Wire the VS+FP pair through the existing shader compile machinery the same way main.vp/
  main.fp are registered; verify attribute layout matches the 32-byte vertex format.
Acceptance: clean build (shaders pack into the PK3); handoff has uniform/varying list + wiring.

## Chunk D — GL33 backend objects → handoff-d1-vertex.md + handoff-d2-records.md  [DONE cfa218412c + d9f4dbf1]
(Split: D1 vertex VBO/VAOs committed cfa218412c; D2 record samplerBuffers committed after this session's verification. Band-table TBO deferred to ticket 05 — bands are empty until then.)
Files: `src/common/rendering/gl/` (+ hwrenderer data if needed).
- One VBO for the 32-byte vertex pool; one VAO per region bound to its IBO range.
- Surface records / sector state ring / 3D-light buffer / band table as GL_ARB_texture_buffer_obj
  samplerBuffers (GL 3.3 core has no SSBO).
- Program hookup through existing FRenderState machinery; verify attribute layout matches the
  vertex format (x,y,slotA,slotB,u,lmu,lmv,surfaceIndex).
Acceptance: clean build; handoff documents object lifetimes + upload strategy.

## Chunk E1 — frame build at D_Render + cvar → handoff-e1-framebuild.md  [DONE 39cd799]
(gl_uselevelmesh already existed from ticket 01 A/B tooling; E1 latched it per level. Frame build data + r_perflog lmfb/lmdc landed; no draw yet.)

## Chunk E2 — draw pass → handoff-e2-drawpass.md  [DONE a46ab5a]
(Parent fix: levelmesh pass must restore SetEffect(EFF_NONE) at end — mSpecialEffect persists across draws and sprites/models rely on the ambient EFF_NONE.)

## Chunk F — A/B bring-up → handoff-f-abbringup.md
Files: `src/d_main.cpp` (D_Render), g_* cvar file, new frame-build module, r_perflog extension.
- `gl_uselevelmesh 0/1` cvar; path selection at level setup; mid-game toggle applies at next map load.
- Frame build inside the classic render-view site in the interpolation window: PackSnapshot →
  normal-view cull walk over static per-(region,texture,band) sub-range table with per-sub-range
  world-AABB frustum cull → compact per-slot culled draw list.
- Per-view sprite gather walks region actor list → existing sprite batch path unchanged.
- Pass order 1:1 with classic RenderScene (sub-ranges → models → decals → SSAO → translucent).
- Extend EXISTING r_perflog with the frame-build window; do not create a new perf log.
- cvar 0 = classic, byte-for-byte unchanged.
Acceptance: clean build; handoff has call-site map + cvar wiring.

## Chunk F — A/B bring-up + in-game walk (parent-coordinated)
- Zero-tolerance GL33 A/B on DOOM2 MAP01/MAP27 (ticket 01 AbCapture infra); static tick lists
  restricted to non-movement frames; record diff clusters at first bring-up, human review →
  committed allowlist (ADR 0004). Run game unsandboxed.
- In-game walk cvar 1: sky scrolls, fog, light, sprites/models/decals present.
