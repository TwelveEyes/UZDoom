# 12: Levelmesh dynamic lights — clustered forward+

**Spec:** amends `.scratch/levelmesh-rendering/spec.md` §6 "Locked by code investigation" →
"Dynamic lights (dlight_t)". That lock (`uLightIndex = 0`, "the classic never sets it for
walls/flats") rests on a false premise — see Background. The spec section needs rewriting as part
of this ticket's design.

**Status:** needs-triage

**Blocked by:** 03

## Background (found in ticket 03, 2026-10-10)

- The classic path DOES set a per-surface light list for walls and flats: `HWWall::SetupLights`
  culls lights per wall (radius, plane side, portal group) and uploads a list
  (`screen->mLights->UploadLights`, hw_walls.cpp:370/539); `HWWall::RenderWall` sets it per draw
  (`state.SetLightIndex(dynlightindex)`, hw_walls.cpp:94). Flats do the same.
- `uLightIndex` is NOT a "use the global buffer" switch: the fragment shaders read
  `lights[uLightIndex]` as one list header (ranges for point/spot/... lights; see
  `material_*.fp`, `ProcessMaterialLight`). `uLightIndex = 0` would pick whichever surface list
  was uploaded first in the frame — arbitrary lights.
- One levelmesh draw spans many surfaces, so a per-draw uniform cannot carry per-surface lists.
- The ticket-03 draw pass currently pushes `uLightIndex = -1` (no dynamic lights). On DOOM2
  MAP01 with `lights.pk3` this was the last real A/B diff (98% of residual pixels, a green glow
  behind a thing).

## Decision (user, 2026-10-10)

Levelmesh dynamic lighting breaks completely from the classic per-surface-list model and uses a
**clustered forward+** approach: lights binned per frame into view-space clusters (froxels); the
fragment shader finds its cluster and iterates only that cluster's lights. It is independent of
draw batching and traversal order.

## What to design / build (to be refined at triage)

- Per-frame light gather: visible `FDynamicLight`s (point / spot, additive / subtractive /
  attenuated, shadowmap index), in the units `main.fp` expects.
- Cluster grid: dimensions, depth slicing (log/exponential), per-view rebuild (incl. portal /
  mirror / skybox views, ticket 06), CPU vs GPU binning. GL33 has no SSBOs or compute: the GL path
  needs a CPU binner + texel buffers (`samplerBuffer`, as for the record pools); Vulkan (09/10)
  may use compute + SSBO.
- Fragment side: replace the `uLightIndex` list walk for the levelmesh effect with the cluster
  lookup, keeping the per-light contribution math (`lightContribution`, shadowmaps, specular/PBR
  material variants) shared with the classic path.
- Parity policy: classic culls per surface (plane side, portal group, radius) and clustered
  culls per cluster. Per-light math can match exactly, but light *selection* may differ at the
  edges. Decide whether A/B for lit scenes stays zero-tolerance (prove equal selection) or gets a
  documented allowlist class (an ADR 0004 amendment).
- Sprites/models/decals keep their existing classic light lists (unchanged path).

## Acceptance (draft)

- [ ] Spec §6 dynamic-light section rewritten (clustered forward+); false premise removed.
- [ ] `tools/abtest.py` `r_dynlights 0` pin removed; A/B on DOOM2_MAP01 + a dynlight-heavy map per the agreed parity policy.
- [ ] Light count scaling measured (perf bar to be set at triage; ticket 11 integrates).
- [ ] cvar 0 = classic, unchanged.

## Comments
