# A/B acceptance is zero-tolerance pixel diffing with a frozen known-diff allowlist

The levelmesh path is accepted by a deterministic A/B tool: the same map, camera, and inputs rendered on
the classic path and the levelmesh path, frame captures diffed pixel by pixel. The bar is **zero
tolerance**, except for one structural case: a **known-diff allowlist** per map (+ mode, resolution)
that tolerates specific diff clusters. The allowlist is recorded at first bring-up, human-reviewed,
committed to the repo (`tools/abtest/allowlists/`), and frozen — any new diff outside it fails
acceptance, and entries can only be removed, never added silently.

There is exactly one legitimate source of frame-to-frame divergence between the paths: **z-fighting**.
The classic path draws into a normalized per-view depth range; the levelmesh draws into true z (the
geometry data model decision, ticket 04). Where two surfaces are near-coincident, either path's winner
is correct, and that pixel may differ without any bug — and at slaughter scale (the `SOS_Boom`
acceptance map) such coincidences are routine, not corner cases. Everything else must be bit-identical
by construction: the light chain is a formula-for-formula port (ticket 07), the dynamic sector state is
CPU-interpolated before both paths render (ticket 05), and the A/B capture mode drives both paths off
the same synthetic clock (ticket 09). A diff outside the allowlist is therefore a bug by definition.

Considered: **a numeric budget** (e.g. ≤0.05% of pixels differing per frame, cluster size capped).
Rejected: it lets slow regressions through indefinitely (a bug that diffs 0.01% of a frame passes
forever), leaves no persistent record of which pixels are known-benign, and a budget tuned on one WAD
does not transfer to the next. Considered: **strict zero everywhere**, with the map list avoiding
z-fighting. Rejected: it is unachievable in principle at true z at slaughter scale, and forcing it
either weakens map coverage or pushes the levelmesh to replicate the classic's depth-range
normalization — a deviation from true z that the geometry data model deliberately chose against.

Consequence: the allowlist is a permanent, reviewed repo artifact — every tolerated pixel has a named
cause (a z-fight between two near-coincident surfaces) and a human review; bringing up a new map is a
human step (record, review each cluster, commit); the gate itself makes no "close enough" judgment; and
the allowlist's size is a standing measure of the remaining distance from full parity — it can only
shrink.
