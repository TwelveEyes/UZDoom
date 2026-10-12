# 02: Perf baseline v2: real views, stress vantages, bars

**Map:** `../map.md`: Perf bars. Audit §1, §7.
**Type:** task
**Status:** in-review (awaiting the human contact-sheet review; everything else done)
**Blocked by:** None

## What to build

The old baseline measured near-empty frames. `baseline.json` shows medians of 0–34 walls, 0–32
flats, and 0 sprites, and SOS_Boom MAP32 drew 0/0/0. Rebuild the perf kit (`tools/abtest/`)
so it measures real rendering:

1. **Fix the synthetic demo cameras** (`make_demo.py`). Keep the camera inside the playable space and
   facing geometry. Validate this per demo: across the measured window, the median `wl+fl` must be
   above a threshold, and a contact sheet of sampled frames must be dumped for human review.
2. **Stress vantage set.** Add fixed-camera scenarios (position + angles held for N seconds, using
   noclip + `-nomonsters`):
   - open vistas on myhouse MAP01, planisf2 MAP01, SOS_Boom MAP32;
   - 3D-floor-dense areas on Pirates! maps;
   - a portal-dense room on myhouse MAP01 / 20PAM;
   - the largest open area on DOOM2 MAP27 and MAP31.

   Record them in the manifest.
3. **Monsters scenario.** Run the slaughter maps (SOS_Boom MAP32 + MAP12) with monsters, as a
   separate scenario. Report it separately: actor and sprite cost is not what the levelmesh
   addresses, but it shows how much of the frame the levelmesh can touch.
4. **Check frame coverage.** Confirm that `r_perflog`'s `total` (`FrameCycles`) covers the
   wall-clock frame. Old MYHOUSE_MAP01 logged 19,819 frames over a run of about 80 s, against a
   0.82 ms median, which does not add up. Fix the window or add a wall-clock column.
5. **Statistics.** Report p50 / p95 / p99 / max for total, render-CPU, per-stage
   (`bsp`/`clip`/`ws`/`wr`/`fs`/`fr`/`ss`/`sr`), draw count, and `fin`. Vulkan is the primary
   backend; GL33 is recorded for reference only.
6. **Set the bars.** Fill in `../map.md` § Perf bars with concrete numbers, derived from the
   classic stage breakdown at the stress vantages. Write down which stages the levelmesh can
   remove.

## Acceptance

- [ ] Every demo passes the non-empty-view check (**done**: all 26 tours + 11 vantages, both
      backends). Contact sheets are reviewed by a human (**pending**: agent-reviewed only).
- [x] Stress vantage set defined (≥ 8 vantages across ≥ 5 maps) and run on Vulkan (+ GL33 for
      reference).
- [x] `results/baseline-v2.{md,json}` written. The old baseline is moved to
      `results/archive/` with a note explaining why it is invalid.
- [x] Frame-coverage question answered and fixed if needed.
- [x] Perf bars in `../map.md` set and justified from the breakdown.

## Notes

Runs must be launched unsandboxed on the real GPU. A sandboxed shell silently renders with llvmpipe,
and `run_matrix.sh` rejects that case. Build commands and test runs are separate invocations.

## Comments

- 2026-10-12 (implementation complete; human contact-sheet review open):
  - **Frame coverage.**
    - `r_perflog v2` adds `wall`, the real interval between logged frames
      (`hw_clock.cpp`, `steady_clock`, unaffected by the A/B fake clock).
    - Root cause of the gap: `r_ticstability 1` (default) busy-waits on frames without a new
      tic (`d_net.cpp` `TicStabilityWait`). On myhouse MAP01, `total` covered 43 % of wall time.
    - `baseline.cfg` now pins `r_ticstability 0`, and `total` covers 90 %. The bars use `wall`.
  - **Cameras.**
    - v1 used `noclip2`, which turns gravity off, so the camera stayed at the player-start
      height (above ceilings / under floors).
    - New `make_demo.py --path tour`: `DEM_WARPCHEAT` to 26 farthest-point-sampled stops in the
      largest two-sided sector component, 100-tic 360° pan each, god + noclip.
    - The non-empty check in `parse_perflog.py` is median wl+fl ≥ 5 and ≤ 10 % empty frames.
      The tiny DOOM2 maps legitimately sit at 6–20.
  - **Vantages.**
    - `select_vantages.py`: 11 vantages on 8 maps, each the costliest sustained tour view
      (9-tic sliding median, which ignores hitches).
    - A tic-0 turn is dropped: input is zeroed on the frozen first tic of a level
      (`player.zs` CheckFrozen). Vantage turns therefore go on tic 10.
  - **Driver.** `run_matrix.sh` gained `--demos/--tag/--monsters/--tics`. It also auto-terminates
    the engine at the timedemo end dialog, detected from the perflog (last tic reached + log idle
    3 s), and runs the engine under `stdbuf -oL`. No other script uses `-timedemo`: the A/B and
    contact-sheet tools use the capture mode, which exits on its own.
  - **Runs** (all PASS, on the real GPU):
    - tours: 26 × {Vulkan, GL33};
    - vantages: 11 × {Vulkan, GL33};
    - monsters: SOS MAP32/MAP12 tours + MAP32 vantages × both backends.
  - **Key finding.** On the heavy vantages, 93–96 % of the classic render window is scene build +
    wall/flat submission, i.e. work the levelmesh replaces. On myhouse, `SetCoverage`
    (`hw_portal.cpp:779`) is a whole-BSP walk per sector-stack portal and costs ~4.9 of
    5.9 ms. Sprites stay ≤ 7 % even with a 2,500-sprite horde.
  - **Bars** (B1–B4) are written in `../map.md`.
  - **For the human review:** contact sheets for every demo are generated under
    `tools/abtest/sheets/` (gitignored), with `review_vantages.png` as the vantage overview.
    Known weak spots:
    - myhouse MAP01 tour: 3 of 26 frames fully dark.
    - DOOM2 MAP31 V1 faces a maze wall.
    - DOOM2 MAP23's sheet missed 2 capture ticks (a capture quirk, not perf data).
