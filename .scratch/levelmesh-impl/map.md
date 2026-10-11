# Levelmesh implementation — effort map

## Spec (single source of truth)

`.scratch/levelmesh-rendering/spec.md` — the finished handoff spec of the closed wayfinder effort.
Tickets here are thin pointers + acceptance; the spec carries all layouts, protocols, and decisions.
Supporting digests:
`.scratch/levelmesh-rendering/ticket-breakup/digest-{01-04,05-06,07-08,09-11,slices-proposal}.md`
(spec-territory digests + the approved 11-slice proposal).

## ADRs

- `docs/adr/0001-levelmesh-full-parity.md` — full portal parity (`PORTAL` lines + `sector_link`, first-class, no shim).
- `docs/adr/0002-dynamic-geometry-vertex-warp.md` — no runtime re-triangulation; the VS warps the static mesh from the per-frame sector-state buffer.
- `docs/adr/0003-full-frame-sector-state-streaming.md` — full per-frame sector-state stream, no dirty tracking.
- `docs/adr/0004-ab-acceptance-policy.md` — A/B zero tolerance + frozen, human-reviewed, shrink-only z-fight allowlist.

## Locked decisions (2026-10-06, user-approved)

- **GL33-first first draw**: the synchronous GL 3.3 draw path lands first, no worker; the data layer stays backend-neutral and Vulkan attaches in 09.
- **MAP01 is the portal bar**: 06's acceptance bar is myhouse MAP01 (needs 05 first — 20PAM is the bring-up warm-up, not the bar).
- **Slice 03 stays whole**: ~3–4k lines in one slice — the shader-half / frame-build-half seam admits no independent verification.
- **New effort directory**: implementation tickets live here (`.scratch/levelmesh-impl/`, numbered 01–11), separate from the resolved wayfinder design tickets in `.scratch/levelmesh-rendering/issues/`.

## Decisions during implementation (2026-10-10/11, ticket 03)

- **Dynamic lights break from the classic model**: the levelmesh uses clustered forward+ → new ticket 12. The spec §6 lock "`uLightIndex = 0`, classic never sets it for walls/flats" is factually wrong (classic builds per-wall/flat light lists); 12 rewrites it. Until then `tools/abtest.py` pins `r_dynlights 0` on both A/B paths.
- **Runtime texture state is its own ticket (13)**: texture swaps (the spec's "single surface-record sub-upload" cannot work: materials bind per sub-range group), wall/flat UV transforms (wall scroll is a spec gap; flat scroll is fetched but unapplied), material shader variants (warp). Frame animation was fixed in 03 (untranslated ids, draw-time resolve). Commercial IWADs carry no ANIMDEFS: do not test for them.
- **Classic lists keep the view-dependent / moving pieces**: on the levelmesh path the BSP walk still runs; translucent walls/flats, sky walls (sky portals), missing-texture fills (plane/flood hacks) and polyobject walls are drawn from the classic lists, not baked.
- **Back-face culling** on the mesh (CW front faces; 3D-floor flats double-sided) mirrors the classic's front-side-only drawing.
- **Smoke policy**: a clean build is the per-change smoke; A/B runs on request (demo A/B limited to the worst offenders).
- **Draw-pass diagnosis log**: `ticket03-drawpass-diagnosis.md` (root causes + fixes, sessions 1–7).

## Standing constraints (every ticket)

- The classic BSP path stays selectable (`gl_uselevelmesh 0`) in EVERY ticket.
- The Vulkan raytracer keeps working in EVERY ticket — ticket 02's adapter is the standing deliverable.
- The mesh / sector-state data layer stays backend-neutral.
- Fork work — `CONTRIBUTING.md` prohibits upstreaming AI-generated code; flag before any commit/PR.

## Tickets

| # | Ticket | Wave | Blocked by |
|---|---|---|---|
| 01 | A/B capture mode + pixel-diff driver | W1 | None |
| 02 | Data layer (FLevelMesh) + raytrace adapter | W2 | 01 |
| 03 | First GL33 draw (static + load-dynamic) — **in progress** (allowlist review + in-game walk open) | W3 | 02 |
| 04 | Dynamic sector lifecycle (movement, re-bake) | W4 | 03 |
| 05 | 3D floors: band split + 3D-light state | W5 | 04 |
| 06 | Portal / sector_link views + skybox | W6 | 04, 05 |
| 07 | Exposure pass + OOB/ortho views + cullcolor | W5 | 04 |
| 08 | Dither transparency: full port | W6 | 07 |
| 09 | Vulkan draw path (synchronous) | W7 | 06, 08 |
| 10 | Vulkan render worker (async) | W8 | 09 |
| 11 | Integrate: matrix, perf bars, soak, PROVEN | W9 | 10, 08 |
| 12 | Levelmesh dynamic lights — clustered forward+ (needs-triage) | — | 03 |
| 13 | Runtime texture state: swaps, UV transforms, shader variants (needs-triage) | — | 03 |

Waves are dependency groups, not concurrency (one agent at a time on this repo). Critical path: 01→02→03→04→06→09→10→11.
Status: 01, 02 done; 03 in progress; 12 and 13 await triage (wave/slot to be decided; both sit off the critical path today but 11's matrix will need them).

## Working the frontier

A ticket is grabbable when every `Blocked by` ticket is resolved; first by number wins. This is an
implementation effort (not wayfinding): tickets are `ready-for-agent` work items, resolved by
implementation + their acceptance passing; resolution notes append under `## Comments` in the ticket file.

## Provenance

Breakdown by the 5-chunk digest pipeline over the spec (2026-10-06); the approved 11-slice proposal sits at
`.scratch/levelmesh-rendering/ticket-breakup/digest-slices-proposal.md` (user-approved 2026-10-06).
