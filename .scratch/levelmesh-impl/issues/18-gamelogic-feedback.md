# 18: Game-logic feedback: exposure, dither, OOB/ortho/radar

**Map:** `../map.md`: Acceptance policy (DC3, DC5); `../research/02-consumer-inventory.md`.
**Type:** task
**Status:** ready-for-agent
**Blocked by:** 17

## What to build

The classic walk writes game state that the automap and savegames consume. Reproduce it from GPU
visibility:

- **Exposure.** From the readback (ticket 17), mark `SSECMF_DRAWN` on subsectors of visible
  surfaces and `ML_MAPPED` on lines of visible wall surfaces. Apply the classic's map-exempt rule:
  a same-sector two-sided line with an invalid mid texture is not mapped. Marks are monotonic.
  Savegame code (`p_saveg.cpp`) is unchanged.
- **Cullcolor latch.** Port the classic `Level->cullcolor` latch (`hw_bsp.cpp`; `r_distance_cull_type`
  plus fade color) from visible distance-culled surfaces.
- **Dither transparency** (`r_dithertransparency`):
  - targets come from the ticket-17 actor gather;
  - the classic `P_CheckSight` + `SetDitherTransFlags` are reused verbatim;
  - flags set on sides / planes are uploaded as per-surface dither bits (dirty-only);
  - the shader applies the dither pattern from the bit (an in-shader branch, not a separate
    pipeline);
  - clear-on-consume semantics and the `dithertranscount` decay must match the classic.
- **OOB / ortho / radar view modes.** Port the classic view-mode rules:
  - suplex pitch window for OOB non-radar views;
  - radar gating (`r_radarclipper`);
  - ortho viewbox;
  - secret-sector gating for draw and exposure in OOB views.

  Culling and exposure follow these modes, either through extra view parameters in the cull shader
  or through CPU-side filtering of the readback.

## Acceptance

- [ ] Game-state set-diff (ticket 03 tool) after the same scripted walk on DOOM2 MAP01 / MAP27 and
      myhouse 20PAM: identical, or every difference classified DC5 with a reason.
- [ ] Automap screenshot A/B after the walk passes.
- [ ] Dither: OOB camera + `r_dithertransparency 1`, a monster behind partial walls; frames and
      decay pass (DC3 allowed if latency-bound).
- [ ] Save / load round trip preserves exposure and dither state.

## Comments
