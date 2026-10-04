# Per-frame game-logic feedback: exposure flags & dither transparency

Type: grilling
Status: open
Blocked by: 08

## Question

Ticket 02's research broke the "renderer traversal is render-only" assumption. Two per-frame game-logic
contracts must be replicated by the levelmesh path (details with file:line refs in
`../research/02-consumer-inventory.md`):

1. **Exposure flags**: `ML_MAPPED` (set only at `hw_bsp.cpp:439`) and `SSECMF_DRAWN`
   (`hw_bsp.cpp:358,362,374`) are read by the automap (`am_map.cpp:2103,2240,2692,3074,3359`) and
   serialized into savegames (`p_saveg.cpp:347-440`, including a recovery path that rebuilds
   `SSECMF_DRAWN` from `ML_MAPPED`). Decide how the levelmesh path computes and writes these per frame —
   from the frustum-culled draw set, or a cheap separate pass? What exact timing relative to the game
   tick and render thread?
2. **Dither-transparency loop**: visible actors (`RenderedTargets`, `hw_bsp.cpp:55,927-934`) →
   `P_CheckSight` filter (`hw_bsp.cpp:1134-1139`) → trace-based `WALLF_DITHERTRANS_*` writes
   (`hw_drawinfo.cpp:716-837`) consumed by the next frame's wall/flat drawing. Decide the
   visible-actor-list hook (available from the per-sprite frustum pass) and where the trace loop runs.

End state: a spec-ready "per-frame game-logic contracts" section with the update sequence (exposure pass +
dither loop placement in the frame timeline) and an acceptance checklist (automap behavior, save/restore
round-trip, dither appearance parity).
