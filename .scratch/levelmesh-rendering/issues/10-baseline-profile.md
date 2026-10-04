# Classic-path baseline profile on acceptance maps

Type: task
Status: open
Blocked by: 09

## Question

Run the classic-path baseline profile on the acceptance maps. In-game, needs a GPU/display — a human drives
the build (`cmake --build build --config RelWithDebInfo --parallel 3`); the agent prepares the exact
checklist (maps, resolution, settings, which stats to read) once ticket 09 lands.

Record for each acceptance map: render CPU time (traversal + batch construction) and GPU frame time at the
fixed resolution/settings, including the slaughter, portal, and 3D-floor stress cases.

Deliverable: a results table (map, resolution, settings, render-CPU ms, frame ms) posted as a comment on
this ticket — these numbers anchor the acceptance bar in the spec.
