# Archived: perf baseline v1 (invalid)

`baseline-v1.{md,json}` were the classic-path baseline of the retired wayfinder effort
(ticket 10, 2026-10-06). **Do not use them.**

- **Cameras saw nothing.** The spiral demos used `noclip2` (gravity off), so the camera kept
  the player-start height and spent most frames above ceilings / under floors: median 0–34
  walls+flats per frame, 0 on SOS_Boom MAP32.
- **`total` missed most of the frame.** `r_ticstability 1` (engine default) busy-waits on every
  frame without a new sim tic; on myhouse MAP01 `total` covered 43% of wall-clock time.
- **Median-only statistics hid the tails.**

Replaced by `../baseline-v2.{md,json}` (ticket 02 of `.scratch/levelmesh-impl/`).
