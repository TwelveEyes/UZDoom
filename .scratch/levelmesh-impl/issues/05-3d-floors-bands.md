# 05: 3D floors: band split + 3D-light state

**Spec:** `.scratch/levelmesh-rendering/spec.md` §6 (3D floors — band split, 3D-light state
buffer, the FF_FOG variant). Detail:
`.scratch/levelmesh-rendering/ticket-breakup/digest-05-06.md` (3D-floor lighting) and
`digest-slices-proposal.md` (slice 05).

**What to build:** The 3D-floor half of the levelmesh path. Walls in 3D volumes are drawn as
per-band sub-draws: each band sub-range carries the band index + the split top/bottom clip planes
(the split-plane clip-distance mechanism in the vertex shader), the split planes resolved per
frame from the sector state records via the band table's model-sector plane refs (dynamic — the
3D-floor layers move). Band light = the band's 3D-light state buffer entry (its live lightlevel,
rescaled) or the wall's own chain when the band has no 3D light (light ref −1 → own); the
colormap is merged in classic order — the 3D light's extra colormap first, then the wall's own
fade/fog overwrite it — and the classic split's early-break semantics hold. 3D-floor flats are
lit from the per-level 3D-light state buffer (12 B/light: live lightlevel + 9-byte extra
colormap, full-copied per frame — packed into the frame record by 03): flats take the light's
live lightlevel and extra colormap directly (flats do not split), incl. the FF_FOG variant (the
light's fade color as the flat light color, white flat color). The band table + sub-range IBO
structure already exist from ticket 02 — this ticket draws them. A/B GL33 on the 3D-floor maps
(DOOM2 MAP07 3D floors + lift, MAP30 3D floors + scale) — zero tolerance, incl. moving 3D-floor
layers; in-game multi-layer 3D-floor tour with active 3D lights.

**Blocked by:** 04 (the verification maps exercise lifts — moving sectors — and the snapshot pass feeding the dynamic split planes).

**Status:** ready-for-agent

- [ ] MAP07 + MAP30 A/B GL33 zero-tolerance, incl. moving 3D-floor layers.
- [ ] Per-band draws match the classic split: plane refs, light/colormap merge order (the 3D light's extra colormap first, then the wall's own fade/fog), early-break semantics.
- [ ] 3D-light state buffer carries live values — light thinkers on 3D lights mutate flats per frame, frame-locked in the A/B runs.
