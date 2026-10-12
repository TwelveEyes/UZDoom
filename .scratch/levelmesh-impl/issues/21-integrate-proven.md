# 21: Integrate: matrix, perf bars, soak, PROVEN, default flip

**Map:** `../map.md`: Acceptance policy, Perf bars.
**Type:** task
**Status:** needs-triage (pending the 05 gate)
**Blocked by:** 03, 20

## What to build

- **Acceptance matrix.** Every acceptance map in `tools/abtest/manifest.md` (DOOM2, HEXEN, HERETIC,
  myhouse, Pirates!, SOS_Boom, planisf2) × {static tick list, demo replay}, on Vulkan, classic vs
  levelmesh. Plus the cvar sweep from ticket 10, and the game-state set-diff from ticket 18.
- **Perf.** The full ticket-02 protocol (demos + stress vantages + monsters scenario), with
  results vs bars.
- **Soak** (human-driven, levelmesh on, about half a day):
  - full DOOM II campaign;
  - Hexen and Heretic E1 samples;
  - myhouse MAP01 + 20PAM tours;
  - a SOS_Boom MAP32 fast fly;
  - a save / load + automap walk;
  - a deathmatch on a portal map;
  - a 1 h+ stability watch (no crash, hang, or GPU memory growth);
  - `vid_restart` and map-change cycling.
- **PROVEN** = A/B pass ∧ perf pass ∧ soak pass ∧ no open levelmesh-only bug of severity ≥ S2.
  Severities: S1 crash / hang / memory growth; S2 a visible artifact or broken behavior on an
  acceptance map; S3 cosmetic. The implementer posts the results; the user declares PROVEN.
- **On PROVEN:**
  - `gl_uselevelmesh` defaults to on for Vulkan. It is still ignored on GL, and capability fallback
    is unchanged.
  - Add a SettingsPage (launcher / options) entry.
  - Add a console / menu option to force classic.

## Acceptance

- [ ] Matrix, perf, and soak results posted in Comments.
- [ ] User declares PROVEN; default flip and settings entry shipped together.

## Comments
