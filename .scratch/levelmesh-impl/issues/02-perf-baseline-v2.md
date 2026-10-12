# 02: Perf baseline v2: real views, stress vantages, bars

**Map:** `../map.md`: Perf bars. Audit §1, §7.
**Type:** task
**Status:** ready-for-agent
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

- [ ] Every demo passes the non-empty-view check. Contact sheets are reviewed by a human.
- [ ] Stress vantage set defined (≥ 8 vantages across ≥ 5 maps) and run on Vulkan (+ GL33 for
      reference).
- [ ] `results/baseline-v2.{md,json}` written. The old baseline is moved to
      `results/archive/` with a note explaining why it is invalid.
- [ ] Frame-coverage question answered and fixed if needed.
- [ ] Perf bars in `../map.md` set and justified from the breakdown.

## Notes

Runs must be launched unsandboxed on the real GPU. A sandboxed shell silently renders with llvmpipe,
and `run_matrix.sh` rejects that case. Build commands and test runs are separate invocations.

## Comments
