// Sequential resume of the three failed levelmesh-rendering research subagents.
// One at a time (user directive, 2026-10-03): the local 27B model's KV cache cannot
// sustain concurrent token-heavy runs. Each job resumes its latest failed run's
// context. A ticket's failure does not stop the others; the parent adapts on wake.
// Each child is instructed to create its findings file within its first two tool
// calls and update it incrementally, so a third failure still leaves durable output.

const jobs = [
	{
		key: 't01-helion',
		resume: '5b274663-f109-4f3d-9258-a55fb6c54dac',
		task: `You failed again (second time) mid-exploration of Helion. Your findings so far (still in your context): vertex format carrying current AND previous-frame pos/uv plus a LightLevelBufferIndex; lighting as a per-vertex-indexed GPU samplerBuffer (sectorLightTexture); portals as a flood-fill + stencil system (FloodFillRenderer, PortalRenderer); geometry split into a static cache vs a dynamic buffer via IsStatic/IsDynamic.

CRITICAL change this attempt: within your FIRST TWO tool calls, create /home/gene/Development/Git/doom/UZDoom/.scratch/levelmesh-rendering/research/01-helion-levelmesh.md and write everything you already know into it — all six sections from the ticket (mesh construction, dynamic-sector handling, sprites, portals, lighting, draw organization), every claim citing the Helion source file, gaps explicitly marked. Then keep exploring ONLY to fill gaps (GeometryRenderer/static-cache details, the Sector data model, sprite handling, what CPU work remains per frame), updating the file incrementally after each read. A complete findings file with marked gaps is worth more than more exploration.

When done, append a '## Answer' gist (3-8 bullets) to /home/gene/Development/Git/doom/UZDoom/.scratch/levelmesh-rendering/issues/01-helion-levelmesh-deep-dive.md and change its 'Status: open' line to 'Status: resolved'. Do not touch map.md or anything under src/.`
	},
	{
		key: 't02-consumers',
		resume: '77626b6e-574b-4007-bf16-368731f55499',
		task: `You failed again (second time) mid-inventory. Key finding so far (in your context): savegame and automap code READ the renderer's per-frame flags (ML_MAPPED, SSECMF_DRAWN) — so the traversal is NOT purely render-only. Record exactly where those flags are set and who reads them.

CRITICAL change this attempt: within your FIRST TWO tool calls, create /home/gene/Development/Git/doom/UZDoom/.scratch/levelmesh-rendering/research/02-consumer-inventory.md with the consumer table skeleton and every row you can already classify from what you have read (classification: Replaces cleanly / Needs a hook / Stays, with file:line references). Then fill the remaining rows with NARROW reads only (grep + targeted line ranges, no whole-file reads): walls, flats, sprites, portals, fake flats, glow spots, sector glow/pulse, lightmaps, dynamic lights, sky/sky portals, the render job pool, hw_precache.cpp, VkRaytrace, automap, P_CheckSight/P_LineOpening/p_visualthinker. Where unsure, mark 'needs verification' instead of another big read. Update the file incrementally.

When done, append a '## Answer' gist (3-8 bullets, including the ML_MAPPED/SSECMF_DRAWN game-logic coupling) to /home/gene/Development/Git/doom/UZDoom/.scratch/levelmesh-rendering/issues/02-consumer-inventory.md and change its 'Status: open' line to 'Status: resolved'. Do not touch map.md or anything under src/.`
	},
	{
		key: 't03-portals',
		resume: 'dc7f66e4-70f3-49d6-a1af-46a5ed9c7b47',
		task: `You failed again (second time; your last death was a connection error). You have already mapped the portal class family (HWSkyboxPortal, HWPlaneMirrorPortal, HWMirrorPortal, HWLineToLinePortal in hw_walls.cpp) and the portaltransferred wiring through the maploader.

CRITICAL change this attempt: within your FIRST TWO tool calls, create /home/gene/Development/Git/doom/UZDoom/.scratch/levelmesh-rendering/research/03-portal-flow.md with the step-by-step flow writeup skeleton and everything you can already document from your context (file:line references, gaps explicitly marked). Then fill gaps with NARROW reads only (grep + targeted line ranges, no whole-file reads): the coordinate-transform math in hw_portal.cpp, how traversal recurses into portal targets, fake flats and sky through portals, 3D floors in portal targets, recursion/re-entry limits, and per-frame portal state (view transform, clipping, depth between regions). Update the file incrementally.

When done, append a '## Answer' gist (3-8 bullets, including the hardest parity requirements) to /home/gene/Development/Git/doom/UZDoom/.scratch/levelmesh-rendering/issues/03-portal-flow.md and change its 'Status: open' line to 'Status: resolved'. Do not touch map.md or anything under src/.`
	}
];

const results = {};
for (const job of jobs) {
	try {
		const run = await runs.run(job.key, { resume: job.resume, task: job.task });
		results[job.key] = { ok: true, output: run.output };
	} catch (err) {
		results[job.key] = { ok: false, error: String(err) };
	}
}
return { sequential: true, results: results };
