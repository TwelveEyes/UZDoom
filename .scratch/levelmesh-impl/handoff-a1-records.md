# Handoff A1 — record fields (planeRef, vparam, lightRef, flags)

## Files changed
- `src/common/rendering/levelmesh.h` — flag enum reconciled with spec §3 table; encoding
  constants (`LM_PLANEREF_NONE`, `LM_LIGHT_SLOT_*`) added; `LM_LIGHTREF_OWN` removed
  (lightRef is now always a real encoding); record field comments updated.
- `src/common/rendering/levelmesh.cpp` — builder extended in place: new file-scope helpers
  `LM_PlaneRef(sec, plane)` and `LM_LightRef(sec, slot)`; `AddSurface()` gained
  `(planeRefs[4], lightRef)` parameters; all 9 surface call sites (4 wall tiers +
  4 flats + 1 band table untouched) pass real values. No existing function restructured,
  renamed, or rewritten (the lower/upper wall `peg` bool was renamed to `unpeg` with the
  same value and identical ternary semantics — only the flag/vparam additions are new).

## Record fields now populated (field by field)
`LevelMeshSurface` is 64 bytes; every field has a source expression:

| offset | field | source expression | status |
|---|---|---|---|
| 0 | textureIndex | `tex->GetID().GetIndex()`, `0xFFFFFFFFu` if no texture | pre-existing |
| 4 | regionIndex | `r` (region from `SectorRegion(frontsector)` / subsector sector) | pre-existing |
| 8 | planeRef[0..3] | see below | **A1** |
| 24 | lightRef | see below | **A1** |
| 28 | flags | see flag table | **A1 (reconciled)** |
| 32 | vparam[0..3] | see below; flats all zero via memset | **A1 (walls)** |
| 48 | light | `RescaleLightLevel(side->GetLightLevel(...))` / `RescaleLightLevel(sec->lightlevel)` | pre-existing |
| 50 | tierLight | `side->TierLights[tier]` / `sec->planes[plane].Light` / model equivalents | pre-existing |
| 52/54 | relSmooth/relNonSmooth | `LM_CalcRelLight(ll, orglightlevel, rel)` (both formulas identical today) | pre-existing |
| 56 | lightlistRef | `-1` for statics | pre-existing |
| 58/60 | bandOffset/bandCount | 0/0 here; bands attached by later wall-in-3D-volume pass | pre-existing gap |
| 62/63 | skyScrollKind(2) | 0/0 on `LMF_SKY` surfaces (sky1 default; full sky2/doublesky is a render concern) | pre-existing |

### planeRef[4] = sectorIndex<<1|planeBit (0=floor, 1=ceiling)
- Subsector floor/ceiling fan: `[0]=sec<<1|0`, `[1]=sec<<1|1`, `[2]=[3]=LM_PLANEREF_NONE (0xFFFFFFFF)`
- 3D-floor fake flats (model sector fans): same shape with `model` index, sentinels in [2]/[3]
- Wall upper/lower/mid: `[0]=front.floor, [1]=front.ceil, [2]=back.floor, [3]=back.ceil`
- 3D-floor boundary quad: all four = `model` floor/ceiling (per work item: model sector's planes)

### vparam[4] — wall tiers only (flats stay zero)
- lower (`side_t::bottom`): `[0] = unpeg ? backFloorTexZ - frontFloorTexZ : 0`,
  where `unpeg = ML_DONTPEGBOTTOM`. Classic source: `HWWall::DoTexture` peg branch
  (`hw_walls.cpp:1374-1375`) — without DONTPEGBOTTOM the texture anchors on the front floor
  (tracks it); with it, static at back floor TexZ.
- middle (`side_t::mid`): `[1] = 0`. The mid texture anchors on and tracks the front ceiling
  (builder texturetop `= fch1 + yOffset + renderHeight`, classic `DoMidTexture` unpegged
  branch `hw_walls.cpp:1435-1437`).
- upper (`side_t::top`): `[2] = unpeg ? 0 : backCeilTexZ - frontFloorTexZ`,
  where `unpeg = !ML_DONTPEGTOP`. Same DoTexture peg branch — unpegged tracks the front floor;
  pegged anchors on back ceiling TexZ.
- `[3]` always 0 (reserved).
- All three offsets are relative to the front floor plane, consistent with the builder's
  `texturetop` convention (v = -z + texturetop), so the VS `V = z - TexZ + vparam` chain
  reproduces the baked static V.

### lightRef = sectorIndex<<2|slot (0=floor, 1=ceiling, 2=wall)
- `LM_LightRef(sec, slot)` resolves the effective light sector at build time: if `sec` has a
  `heightsec` (Transfer_Heights special / transdoor hack — the only writers of `heightsec`,
  `maploader/specials.cpp:691`, `renderinfo.cpp:371`) and it lacks `SECMF_NOFAKELIGHT`, the
  model sector's index is used; otherwise `sec`'s. This mirrors the classic renderer, which
  passes `hw_FakeFlat(render_sector)` as the wall/flat light source (`hw_bsp.cpp:946-957`).
- Walls (upper/lower/mid): `LM_LightRef(frontsector, 2)`.
- 3D-floor boundary quad: `LM_LightRef(model, 2)` (the FFBLOCK wall uses the model sector's
  light in the classic path).
- Flats: `LM_LightRef(sec|model, 0/1)` for floor/ceiling.
- `LM_LIGHTREF_OWN` sentinel removed from the header; every record now carries a real ref.

## Flag bits as implemented (`ESurfaceFlags`)
| bit(s) | flag | spec status |
|---|---|---|
| 0 | LMF_DYNAMIC (alias LMF_LOADDYN kept) | spec: dynamic — was bit 3 |
| 1 | LMF_SKY | spec: sky — unchanged |
| 2 | LMF_LIGHTMAP | spec: hasLightmap — was bit 5 |
| 3 | LMF_ISFLAT | free bit (not in table) — was bit 0 |
| 4–6 | LMF_OPENING_UPPER / MIDDLE / LOWER | spec openingMask; set per wall tier |
| 7 | LMF_UNPEGGEDLOWER | spec: unpeggedLower = `ML_DONTPEGBOTTOM` (lower walls only) |
| 8–9 | tier = `side_t::{top,mid,bottom}` << 8 (0/1/2) | spec tier; set on all wall surfaces |
| 10 | LMF_POLYOBJ | free bit — was bit 6 |
| 12 | LMF_3DFLOOR | free bit — was bit 2 |
| 13 | LMF_FAKECONTRAST | internal bookkeeping, outside the table (rel values are baked in the record regardless; draw path selects by tier) — was bit 4 |

No bits ≥ 14 used; spec bits 10–31 of the 07-block remain free for chunk C. No bit-31 constant
was needed (`static_cast<uint32_t>(1u<<31)` not required in this chunk).

## Spec deviations / notes
- `LMF_ISFLAT`/`LMF_3DFLOOR`/`LMF_POLYOBJ` moved to free bits 3/12/10 (required by the task;
  they were colliding with spec openingMask). The raytrace adapter (`doom_levelmesh.cpp`) only
  consumes vertex/position/index data, so the move is safe.
- lightRef is the static approximation of the classic fakesector resolution: `hw_FakeFlat`'s
  per-frame area dependence (area_below / area_above) is not resolvable at build time; the
  "normal" heightsec relationship is used, which is what the spec's "static, resolved … at
  build time" calls for.
- vparam[1] (middle window) is 0: the mid texture tracks the front ceiling in both classic and
  builder bakes, so no static offset relative to that plane exists.
- Band table entries (`BuildBands`) are still stubs (modelSector=0, plane=0) — pre-existing,
  owned by a later pass; untouched here.

## Build result
`cmake --build build --config RelWithDebInfo --parallel 3` from repo root:
```
[ 67%] Building CXX object src/CMakeFiles/zdoom.dir/common/rendering/levelmesh.cpp.o
[ 67%] Linking CXX executable ../uzdoom
[100%] Built target zdoom
```
Zero warnings from the modified files. A full-tree recompile (forced by touching levelmesh.h)
shows a pre-existing `a_dynlight.h:266` -Wdeprecated-anon-enum-enum-conversion warning in ~34
TUs; verified present on the unmodified baseline via `git stash`, so it is not introduced here.
