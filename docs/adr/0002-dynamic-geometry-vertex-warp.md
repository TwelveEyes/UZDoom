# Moving sector geometry warps in the vertex shader from a per-frame sector state buffer

The levelmesh path (ADR 0001) holds full parity with the classic rasterizer, and parity must be provable
with an A/B pixel-diff tool. We decided that moving and animated sector planes are **not re-uploaded as
vertices**: the baked geometry stays static, and the vertex shader warps it from a per-frame sector state
buffer whose records carry the single *current*, CPU-interpolated plane values. No dynamic VBO, no
prev/current pair, no GPU-side tick interpolation.

UZDoom's classic pipeline already interpolates all sector movement on the CPU before rendering
(`DoInterpolations(I_GetTimeFrac())` writes the tick-blended planes into `sector_t`), and it bakes exactly
those values into vertices every frame. Warping the *same numbers* in the vertex shader makes A/B parity on
moving geometry exact by construction — a property no GPU-side interpolation model can offer, because it
would be a second, subtly different blend of the same tick pair. The warp costs the vertex shader a few
ops, and only for the small set of surfaces flagged dynamic by the snapshot pass.

Considered: **Helion's approach** — a second per-plane dynamic VBO carrying previous- and current-frame
Z/UV that the vertex shader mixes by tick fraction (`research/01-helion-levelmesh.md`). Rejected: it
introduces an interpolation model the classic path does not have, so moving geometry cannot be proven
pixel-identical to the classic bake, and it doubles per-plane vertex storage and upload paths for a
computation the vertex shader performs cheaply.

Consequence: the vertex format carries a dual-purpose slot (static: baked `z,v`; dynamic: `vparam`), and
the 96-byte `SectorState` record, snapshot pass, and re-bake protocol of ticket 05
(`.scratch/levelmesh-rendering/issues/05-dynamic-sector-state.md`) are built around single current values —
the record never carries a previous frame.
