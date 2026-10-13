# 06: Bake via classic emitters + plane-anchored surfaces

**Map:** `../map.md`: Data model (Bake, Plane-anchored vertices, Surfaces). ADR 0003.
**Type:** task
**Status:** resolved
**Blocked by:** 01, 05

## What to build

A backend-neutral bake module under `src/rendering/hwrenderer/` (it needs game types). It runs at
level setup **only** when `gl_uselevelmesh` is on and the active backend supports the levelmesh
path.

- **Walls.** For every non-polyobject seg, run `HWWall::Process` through
  `HWWallDispatcher(Level, &meshHelper, lightmode)` (mesh-helper mode, `hw_walldispatcher.h:27-52`).
  - Capture the resulting `HWWall`s: texture, flags, `tcs`, `ztop`/`zbottom`, the gl_seamless
    vertex splits from `MakeVertices`, the lightmap coords, and the light inputs.
  - View-dependent and unbakeable outputs go to **side lists**, not the mesh: `mh->translucent`
    (ticket 16), `mh->portals` (ticket 15), and `mh->upper` / `mh->lower` missing-texture fills
    (ticket 16).
  - Add the small hooks the capture needs (for example, a path that produces vertices without
    `screen->mVertexData`). Do not copy classic logic.
- **Flats.** Use the classic static flat builder (`hw_vertexbuilder.cpp`
  `CreateVerticesForSubsector` / `CreateIndexedSectorVertices`) for every subsector and plane,
  3D-floor planes included (ticket 12 consumes them). Keep the classic triangulation exactly.
- **Plane anchoring.** For every vertex, determine which plane defines its z (front/back
  floor/ceiling, or a 3D-floor model plane) and store `(x, y, zOffset, anchor)` so that
  `ZatPoint(plane) + zOffset` reproduces the baked z **exactly** at load. Anchor the `v` coordinate
  to the texture-top plane in the same way. Assert the round trip at bake time (debug builds) and
  count the mismatches (release).
- **Surface records** (std430 layout, integer fields):
  - plane refs and light ref, side-state ref (`side × tier`, for ticket 11), untranslated texture
    id;
  - draw-state bits: masked, clamp, cull mode (back-face for walls; floors from above, ceilings
    from below; 3D-floor flats double-sided), material bucket;
  - flags (sky, 3D floor, dither-eligible, ...);
  - baked static light ingredients (side light, tier light, fake-contrast rel pair, side light
    flags);
  - lightmap page and UV.
- **Normals.** Per surface (wall direction, plane normal), stored in or derived from the record.
- **Performance and memory.** Linear time and memory: no per-region scans and no O(n²) dedup. Free
  the CPU-side copies after upload, except what the CPU consumers need (AABBs, back-links).

## Acceptance

- [x] Bake round-trip: 0 z / v mismatches on all 26 acceptance maps (report the per-map count).
      0 beyond 1 ulp on all 26. 1059 z / 2330 v vertices (8 maps) are within 1 ulp but not
      bit-exact; accepted as passing (user decision 2026-10-13).
- [x] Bake time logged per map. Linear scaling shown across DOOM2 MAP01 → myhouse MAP01. No bake
      when the cvar is off or on GL.
- [x] Side lists populated and counted (translucent, portal, missing-texture).
- [x] Debug dump: the CPU-rasterized wireframe of the baked mesh (or an OBJ export) matches the
      classic geometry on DOOM2 MAP01 / MAP07 by visual check.
- [x] Clean build; classic path unchanged.

## Notes

Check ticket 04's findings on `HWMeshHelper`'s original use. Polyobject walls are excluded here
(ticket 13). `isculled` and sky-hack branches in `HWWall::Process` must be checked for view
dependence.

## Comments

- 2026-10-13 (resolved): `src/rendering/hwrenderer/hw_levelmeshbake.{h,cpp}`, created from
  `MapLoader::LoadLevel` after `DoomLevelMesh` and freed in `ClearLevelData`.
  - **Gating:** `gl_uselevelmesh` on and `screen->IsVulkan()`; ticket 07 swaps in the capability
    check. With the cvar off, or on GL, there is no bake (checked).
  - **Walls:** every non-polyobject side runs once through `HWWall::Process` in mesh-helper mode.
    Vertices come from `HWWall::CreateVertices`, which gained an optional per-vertex role output
    (bottom / top / side split). They are fan-triangulated like `DT_TriangleFan`.
  - **Flats:** `CreateVerticesForSubsector` (now exported) runs per subsector of each section, once
    per plane. 3D-floor planes reuse the target sector's triangulation.
  - **Anchoring:** each vertex picks its plane by edge semantics first, then by value.
    `zOffset`/`vOffset` are solved with a nextafter walk so that
    `LM_PlaneZ(plane) + zOffset` reproduces the float exactly. Where no plane matches, the
    vertex is anchored to the texture-pegging plane with an offset. This is almost always an
    `RENDERWALL_M2S` mid texture bounded by texture height, and it is the right motion
    semantics.
  - **Round trip** (`tools/abtest/lm_bake_survey.py` → `tools/abtest/results/lm_bake_survey.{md,json}`):
    - 0 mismatches beyond 1 ulp on all 26 maps;
    - 18 maps are fully bit-exact;
    - myhouse MAP01 has 1032 z / 2038 v vertices within 1 ulp (of 2.4M), and 7 other maps have
      ≤ 74 each.
    - These are not solvable with a float add. Example: a baked z of −1.9e-13 against
      `LM_PlaneZ` = 1.0e-4 (`d` = 649.6). The representable sums are spaced by ulp(1e-4), so
      the error is below the precision of the plane evaluation itself. Bit-exactness here would
      need a different shader formula (e.g. storing the absolute z plus the plane's bake-time z,
      or an fma order). That is left to ticket 09 if A/B shows any seam.
  - **Bake time** is linear: 0.15–0.30 µs per triangle from 10k to 1.24M triangles, plus about
    0.5 ms fixed. DOOM2 MAP01 1.3 ms, SOS MAP32 102 ms (504k tris), myhouse MAP01 349 ms
    (1.24M tris).
  - **Side lists** are counted per map (survey table): translucent / portal / fog walls,
    upper / lower missing fills, translucent and portal/sky flats, untextured flats.
  - **Visual check:** `tools/abtest/lm_obj_compare.py` CPU-rasterizes the OBJ export from the
    `r_ab_capture` camera. `d_abcapture.cpp` now logs an `ABCAPTURE view` line. The baked geometry
    matches the classic geometry on DOOM2 MAP01 / MAP07 (3 demo ticks each:
    `tools/abtest/sheets/lmbake_MAP0{1,7}.png`, gitignored, regenerate with `lm_obj_compare.py --demo demos/v2/DOOM2_MAP0x.lmp --ticks 400,900,1500`). Sky areas are empty as expected (portal list).
  - **Classic unchanged:**
    - with `gl_uselevelmesh` 0 vs 1 (bake running), 16 `r_ab_capture` frames on DOOM2 MAP07,
      HEXEN MAP10, Pirates MAP43 and SOS MAP12 are pixel-identical;
    - with the cvar off, no new code runs;
    - the `side_t::GetFakeContrastRel` extraction is a pure refactor;
    - clean build, no new warnings.
  - **Debug tools:** `lm_bakestats`, `lm_dumpobj [file]`, `lm_bake_objdir <dir>` (OBJ per bake),
    `lm_bake_debug N` (prints the first N round-trip failures and offset anchors).
  - **Deferred or caveated:**
    - Freeing the CPU copies after upload waits for ticket 07, since nothing is uploaded yet.
    - Heightsec sides and flats are baked unfaked and flagged `LMSF_HEIGHTSEC` (the fake depends
      on the view area).
    - Banded 3D-light walls are flagged `LMSF_LIGHTLIST`. The layer index is not recorded
      (ticket 12).
    - 3D-floor side `vRef` is taken from the top-left anchor (the rover is not kept in the
      HWWall); myhouse MAP01 has 39k such surfaces (ticket 12 to verify).
    - `planeRefs[4]` overflows on 0.1–0.4 % of surfaces (ticket 09 note).
  - **User decision (2026-10-13):** within-1-ulp round trips count as passing. Change the formula
    in ticket 09 only if A/B shows seams there.
