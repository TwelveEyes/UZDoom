# 11: Integrate: matrix, perf bars, soak, PROVEN

**Spec:** `.scratch/levelmesh-rendering/spec.md` §9 (verification & acceptance — the
acceptance matrix, the frozen allowlist, the perf protocol + bars, the soak, the PROVEN
definition) + §10 (classic-path baseline + bars). Detail:
`.scratch/levelmesh-rendering/ticket-breakup/digest-09-11.md` (locked decisions D1–D12, the
26-map matrix, the tolerance rules, the reference machine + baseline results, the 9-item
soak) + `digest-slices-proposal.md` (slice 11).

**What to build:** Verification close-out. (a) The FULL acceptance matrix: every acceptance
map (the 26 in the `tools/abtest/` manifest) × {static tick lists, demo replay} × {GL33,
Vulkan} = 104 combinations / 208 fresh-level-load launches, plus the cvar sweep (`r_lightmode`,
`gl_fogmode`, `r_fakecontrast`, `r_extralight`, `r_distance_cull_type`, `gl_weaponlight`,
lightmaps on) on the fog/fake-contrast/animated-light/scroll/skybox maps in the matrix and
the gamma × glow axis (gamma {1.0, 0.9} × glow on/off) on the stress maps — with the
known-diff allowlist FROZEN: any NEW diff fails; entries are z-fight-only, human-reviewed,
and shrink-only (removable by re-review, never added silently — ADR 0004). (b) The PERF
PROTOCOL against the classic baseline already on disk (the `tools/abtest/` results):
host-native resolution pinned (2560×1440), vsync off, gamma 1.0, 10 s warmup discarded /
60 s window, median + p95 of {total frame, CPU render window, GPU 3D window} via the extended
per-frame perf log (the levelmesh-side windows landed in 03 — frame build — and 10 — worker
fence wait) + the existing parser; reference machine = the recorded dev box (the bar is
relative — same machine, both paths) — with the bars: levelmesh median ≤ classic × 1.05 per
map on BOTH backends, and the slaughter-scale map (SOS_Boom MAP32) ≤ 50% of its classic
median. (c) The 9-ITEM HUMAN soak (levelmesh on, ~half a day): 1. full DOOM II campaign;
2. full Hexen campaign; 3. Heretic E1; 4. myhouse MAP01 + 20PAM tours, 10+ min each (portals
+ 3D floors + sector_link + skybox); 5. SOS_Boom MAP32 15-min fast-camera fly (scale +
sector-state upload budget); 6. the scripted save/load + automap fog + secret-gating walk
(the 07/08 acceptance items); 7. 15-min deathmatch on a portal map (dither + OOB views +
exposure under stress); 8. light-thinker/fog/fake-contrast tour, 10 min; 9. stability watch
throughout — no crash/hang/GPU memory growth — incl. one 1 h+ session. (d) The PROVEN
declaration: the implementer runs the matrix + cvar sweep + perf protocol and POSTS the
results; the MAP OWNER declares PROVEN = A/B pass ∧ perf pass ∧ soak pass ∧ no open
levelmesh-only bug of severity ≥ S2 (S1 = crash/hang/memory growth; S2 = visible artifact or
broken behavior on an acceptance map under normal play; S3 = cosmetic/rare). The
`gl_uselevelmesh` default flip (off → on) + the settings-page entry (next to the backend
radio group) ship WITH the declaration; the cvar stays off until it.

**Blocked by:** 10 (the worker is the default-path state the perf bars measure) + 08 (the
exposure/dither pixel items are part of the A/B pass).

**Status:** ready-for-agent

- [ ] 104/104 matrix combinations + cvar sweep pass (zero tolerance outside the allowlist; fresh level load per path; same-backend pairs only).
- [ ] The known-diff allowlist frozen: every entry named z-fight + human-reviewed, shrink-only — any NEW diff fails the run.
- [ ] Perf bars met on the pinned reference machine (levelmesh median ≤ classic × 1.05 per map, BOTH backends; the slaughter-scale map ≤ 50% of its classic median); results posted with per-map tables.
- [ ] 9-item soak checklist complete (campaigns; portal + 3D-floor tours; slaughter-scale fly; the scripted save/load + automap + secret walk; deathmatch; light tour; stability watch incl. one 1 h+ session).
- [ ] No open levelmesh-only bug of severity ≥ S2 (S1 crash/hang/growth; S2 visible artifact on an acceptance map; S3 cosmetic/rare).
- [ ] PROVEN declared by the map owner; the default flip (off → on) + the settings-page entry ship WITH the declaration.

**Note:**
1. The A/B + perf runs are GPU dev-machine work, NOT CI (no headless render mode exists), and must run unsandboxed with the real-GPU stdout verification — see the `tools/abtest/` kit README for the run-environment lessons (sandboxed shells silently fall back to software GL / block the perf-log writes).
2. Resolution of this ticket = the PROVEN declaration itself; the cvar stays off until it.
