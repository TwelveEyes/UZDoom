/*
** DitherTransparency.h
**
** UZDoom-only: r_dithertransparency for out of bounds views (classic SetDitherTransFlags).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** An out of bounds camera (FRenderViewpoint::bDoOob) with r_dithertransparency draws the level geometry between the
** camera and the player (camera->tracer) / the monsters and missiles in view with a dither pattern. Traces from the
** camera towards each of them flag the side tiers (side_t WALLF_DITHERTRANS_*) and planes (secplane_t::dithertransflag)
** they hit, exactly like the classic renderer. The walk of the main view collects the targets of the sectors it reaches
** (BeginWalk / CollectTargets, classic CollectDitherTargets); EndWalk clears the flags of the previous walk and
** traces towards the player and the targets in sight. Like the classic renderer, whose target traces run after the walk,
** the flags act on the next gather. GeometryRenderer marks the flagged walls / flats in the dynamic path
** (DynamicVertex::Dither), the interpolation programs discard the dither pattern. The view walks the BSP
** (RenderInfo::ForceBsp), so every surface goes through the dynamic path.
*/

#pragma once

#include <vector>

struct FLevelLocals;
struct FRenderViewpoint;
struct sector_t;
struct side_t;
struct secplane_t;
class AActor;

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

class DitherTransparency
{
public:
	// r_dithertransparency is on and the view is out of bounds.
	static bool Enabled(const FRenderViewpoint& vp);

	void BeginWalk(bool active);
	// classic CollectDitherTargets: the monsters and missiles touching a sector the walk reaches.
	void CollectTargets(sector_t* sector);
	// Clears the flags of the previous walk; when the walk was active, flags what hides the player and the targets in sight.
	void EndWalk(FLevelLocals* level, const FRenderViewpoint& vp);
	bool Active() const { return m_active; }

	// HWWall::RenderWall: the tier (side_t::top / mid / bottom) of the side is dithered.
	static bool SideDithered(const side_t* side, int tier);

	// The trace callback records what it flags.
	void Flagged(side_t* side) { m_flaggedSides.push_back(side); }
	void Flagged(secplane_t* plane) { m_flaggedPlanes.push_back(plane); }

private:
	static constexpr unsigned MaxTargets = 20; // Maximum number of enemies that can set dither-transparency flags

	bool m_active = false;
	std::vector<AActor*> m_targets;
	std::vector<side_t*> m_flaggedSides;
	std::vector<secplane_t*> m_flaggedPlanes;

	void SetFlags(FLevelLocals* level, const FRenderViewpoint& vp, AActor* actor, int mapSection);
};

}
