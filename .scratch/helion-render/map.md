# Helion render pipeline map

Source of truth for replacing the hardware scene pipeline with a C++ port of Helion's renderer
(`src/rendering/helion/`). Spec: `spec.md`. ADR: `docs/adr/0004-helion-style-render-pipeline.md`.

## Tickets

|#|Ticket|Status|Blocked by|
|---|---|---|---|
|01|[Backend program API (both backends; no scene code yet)](issues/01-backend-program-api.md)|resolved|—|
|02|[Helion skeleton, world hooks, bring-up switch](issues/02-helion-skeleton.md)|resolved|01|
|03|[Static/dynamic geometry, sky, textures, lighting buffers, interpolation](issues/03-geometry-sky-textures.md)|resolved|02|
|04|[Entities, models, weapon, decals, OIT](issues/04-entities-oit.md)|resolved|03|
|05|[Portals, render modes, remaining geometry features](issues/05-portals-rendermodes.md)|resolved|04|
|06|[Clustered forward+ dynamic lights, shadowmaps, coronas](issues/06-clustered-lights.md)|resolved|05|
|07|[Helion-only features, CVars, menus](issues/07-helion-features-cvars.md)|resolved|06|
|08|[Cutover: delete the old pipeline](issues/08-cutover.md)|resolved|07|
|09|[Static mode: mixed-sky bleed-through and see-through walls into closed sectors](issues/09-static-mode-sky-and-seethrough.md)|ready-for-agent|—|

## Decisions log

- 2026-10-20: D1–D29 accepted by the user; recorded in `docs/adr/0004-helion-style-render-pipeline.md`.
