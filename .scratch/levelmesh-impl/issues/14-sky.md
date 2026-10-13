# 14: Sky

**Map:** `../map.md`: Frame model; `../research/03-portal-flow.md` (sky portals).
**Type:** task
**Status:** ready-for-agent
**Blocked by:** 10

## What to build

In the classic, sky walls and flats register sky portals (`HWSkyPortal`) that draw the dome where
the sky geometry is. In the levelmesh:

- **Mask pass.** Sky-flagged surfaces (from the bake: sky flats, sky upper walls, sky-hack walls)
  go to a **sky bucket**. They are drawn by the cull like any other surface, and the pass writes
  depth plus a stencil sky mask.
- **Dome.** The sky dome / cylinder is drawn once per sky texture configuration where the mask is
  set, reusing the classic sky renderer (`hw_skydome`, `HWSkyInfo`). Handle sky1 / sky2,
  `LEVEL_DOUBLESKY` / `LEVEL_SWAPSKIES`, the MBF sky transfer (`skytransfer`, per-sector sky
  texture: one mask value or pass per distinct sky), mist, sky fog / fadecolor rules, and
  `r_skymode`.
- **Ordering.** Match the classic's ordering relative to portals and translucent geometry.
- **Horizon and skybox portals** are ticket 15.

## Acceptance

- [ ] DOOM2 MAP01 / MAP07 / MAP27 sky views, a doublesky map, and an MBF sky-transfer map: A/B
      passes.
- [ ] Sky scroll is deterministic under the capture clock.

## Comments
