# Levelmesh path holds full parity with the classic path, including portals

We are replacing UZDoom's per-frame BSP-traversal rasterizer with a static levelmesh pipeline (Helion-style).
The reference implementation, Helion, deliberately keeps its levelmesh scope narrow — no line portals, a
limited effects set. We decided the UZDoom levelmesh path is held to **full parity** with the classic path:
`PORTAL` line specials and `sector_link`, fake flats, sky portals, dynamic lights, and all sector effects
must work on the new path.

The UZDoom WAD ecosystem uses portals and 3D floors heavily; a second-class levelmesh path would leave the
classic path permanently load-bearing and defeat the purpose of the overhaul. Parity is also what makes the
classic path a usable A/B reference, and it is a hard requirement for the path ever becoming the default.

Considered: **pragmatic parity** (portals excluded, levelmesh covers "normal" maps, classic path serves
portal maps). Rejected: it permanently forks the rendering codebase into two full pipelines and makes the
levelmesh path unshippable as a default, since portal-using maps are a visible part of UZDoom's content.

Consequence: portal/sector_link rendering is a first-class design problem of the levelmesh path (region
partitioning, per-portal view transforms), not a compatibility shim — and the acceptance bar is correspondingly
higher.
