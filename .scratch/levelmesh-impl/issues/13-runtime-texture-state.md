# 13: Levelmesh runtime texture state — swaps, UV transforms, material shader variants

**Spec:** amends `.scratch/levelmesh-rendering/spec.md` §3 (surface record "MUTABLE textureIndex",
"Texture swap (ACS): single surface-record sub-upload"), §4 (floor/ceil scroll), §6 ("Animated flats",
"Scrolling"). Contains one spec contradiction and one spec gap (see Background).

**Status:** needs-triage

**Blocked by:** 03

## Background (found in ticket 03, 2026-10-10)

Runtime texture changes fall into four mechanisms. Ticket 03 fixed (A) frame animation only: the
builder now stores the UNTRANSLATED texture id per surface, and the draw pass resolves
`TexMan.GetGameTexture(id, true)` per (region, texture) group. That is the classic per-draw
semantics: the animator remaps every frame id of an animation together, so all surfaces of a group
animate in lockstep. The rest is open:

- **B. Texture swaps**: switches (`side->SetTexture`, p_switch.cpp), floor/ceiling texture changes
  by specials (a_floor.cpp, a_plats.cpp, a_ceiling.cpp), ACS/FS `SetLineTexture` / `ChangeFloor` /
  `ChangeCeiling`. **Spec contradiction:** §3 says a swap is a single surface-record sub-upload of
  `textureIndex`, but the material is bound per (region, texture) sub-range group and the shader
  never reads the record's `textureIndex`. A swapped surface must move to another group (IBO regroup:
  rewrite the group spans of the affected region) or the draw needs per-surface material selection
  (texture arrays / bindless), which conflicts with the GL33-first constraint.
- **C. UV transforms**:
  - Walls: sidedef offset/scale changes, i.e. scrollers (`DScroller` `sc_side`, with
    `DWallScrollInterpolation`) and `Line_SetTextureOffset` / `Line_SetTextureScale`.
    **Spec gap:** not covered anywhere. The builder bakes u/v from load-time offsets.
    Repro: DOOM2 MAP30 start view (scrolling wall), the only remaining A/B diff there (18.5k-53k px
    per tick, varies by tick).
  - Flats: §4 already carries `floorScroll`/`ceilScroll` in the sector record, but levelmesh.vp
    fetches and never applies them; plane rotation/scale (`xform`) is not carried at all.
- **D. Material shader variants**: `EFF_LEVELMESH` replaces the material's shader variant
  (`ApplyShader` binds the effect shader when `mSpecialEffect > EFF_NONE`), so WARP/WARP2 textures
  and custom material shaders (incl. their brightmap/glow layer variants) never run on the
  levelmesh path.

## Design direction (to be refined at triage)

- One per-frame "texture state", packed and diffed by the snapshot pass like the sector ring: per
  side x tier `{texture id, x/y offset, x/y scale}`, per plane `{xform}`. Offsets/scales reach the
  VS as UV deltas against the baked values (surface record gets a side-state ref). Texture-id
  changes trigger a group regroup of the affected region (or the alternative chosen at triage).
- D: compose the material's shader variant with the levelmesh VS (a levelmesh variant per material
  shader, as the classic `FShader` set does per effect), or route those groups through a
  per-variant program.

## Acceptance (draft)

- [ ] Spec §3/§4/§6 amended (swap mechanism fixed, wall UV state added, flat xform added).
- [ ] DOOM2 MAP30 static A/B: scroll diff gone.
- [ ] Switch + floor-texture-change scripted check (A/B on a demo or tick list that triggers them).
- [ ] A WARP texture map renders through its warp shader on the levelmesh path.
- [ ] cvar 0 = classic, unchanged.

## Comments
