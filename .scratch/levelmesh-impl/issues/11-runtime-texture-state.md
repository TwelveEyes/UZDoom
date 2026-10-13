# 11: Runtime texture state + material buckets

**Map:** `../map.md`: Data model (State buffers: side state, plane xform, translation table), Shaders.
**Type:** task
**Status:** ready-for-agent
**Blocked by:** 10

## What to build

- **Frame animation.**
  - Keep a per-frame texture translation table (untranslated id → bindless slot) that follows the
    animator (`ANIMATED` / `ANIMDEFS`, warping textures' frame ids), and upload it dirty-only.
  - Surface records keep untranslated ids.
  - Note: commercial IWADs have no `ANIMDEFS`; their animation uses the `ANIMATED` defaults.
- **Texture swaps.** Switches (`p_switch.cpp`), `side->SetTexture`, floor / ceiling texture changes
  (`a_floor.cpp`, `a_plats.cpp`, `a_ceiling.cpp`), and ACS / FS `SetLineTexture` / `ChangeFloor` /
  `ChangeCeiling` all write the side-state or sector record. Bindless means no regrouping.
  - A swap that changes the **draw bucket** (opaque ↔ masked, or a different material shader)
    rewrites that surface's bucket bits. The cull writes commands per bucket from per-surface bits.
  - If a swap introduces a case the bake did not anticipate (for example a mid texture appearing
    where none was baked), document it and route the surface to a fallback (the classic draw of that
    side) until resolved.
- **Wall UV state.** Side x/y offsets and scales per tier, including scrollers (`DScroller` side
  scroll with `DWallScrollInterpolation`), `Line_SetTextureOffset`, and `Line_SetTextureScale`.
  The vertex stage applies them as deltas against the baked u/v.
- **Flat xform.** Plane offsets, scale, and rotation (`planes[].xform`), including sector scrollers
  with interpolation.
- **Material shader buckets.** Default, warp / warp2, user hardware shaders, and brightmap / glow /
  normal / specular / PBR layer variants. Each is a levelmesh vertex-stage variant of the existing
  `FShaderManager` key, composed the way the classic composes effects with material shaders. Layers
  are bindless (per-layer slots in the material's record).
- **Lightmaps.** Lightmap page + UV from the bake, with the lightmap texture array bound in the
  levelmesh set.

## Acceptance

- [ ] DOOM2 MAP30 start view (scrolling wall): no residual diff beyond DC1 / DC2.
- [ ] Scripted switch + floor-texture-change test (a demo or tick list that triggers them): A/B
      passes.
- [ ] A WARP texture map and a brightmapped map render through their material variants and pass
      A/B.
- [ ] A lightmapped map passes A/B.

## Comments
