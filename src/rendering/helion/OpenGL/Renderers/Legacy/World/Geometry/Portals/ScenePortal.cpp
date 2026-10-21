/*
** ScenePortal.cpp
**
** UZDoom-only (plan M5.2), see ScenePortal.h. Port of HWScenePortalBase::ClearClipper, classic FrustumAngle,
** classic UpdateCurrentMapSection and classic SetViewArea.
**
**---------------------------------------------------------------------------
**
** Copyright 2004-2018 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include "ScenePortal.h"
#include <algorithm>
#include <cmath>
#include "PortalDriver.h"
#include "g_levellocals.h"
#include "hw_renderstate.h"
#include "printf.h"
#include "r_utility.h"
#include "v_draw.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

namespace
{

constexpr uint64_t FullCircle = 0x100000000ull;
using Range = std::pair<uint64_t, uint64_t>;

// A counter clockwise range from start of the given length (< a full circle), split at the BAM wrap.
void AddWrapped(std::vector<Range>& out, uint32_t start, uint64_t length)
{
	if (length == 0)
		return;
	uint64_t end = (uint64_t)start + length;
	if (end <= FullCircle)
		out.emplace_back(start, end);
	else
	{
		out.emplace_back(start, FullCircle);
		out.emplace_back(0, end - FullCircle);
	}
}

void Normalize(std::vector<Range>& ranges)
{
	std::sort(ranges.begin(), ranges.end());
	std::vector<Range> merged;
	for (const Range& range : ranges)
	{
		if (!merged.empty() && range.first <= merged.back().second)
			merged.back().second = std::max(merged.back().second, range.second);
		else
			merged.push_back(range);
	}
	ranges = std::move(merged);
}

std::vector<Range> Intersect(const std::vector<Range>& a, const std::vector<Range>& b)
{
	std::vector<Range> result;
	for (const Range& x : a)
	{
		for (const Range& y : b)
		{
			uint64_t lo = std::max(x.first, y.first);
			uint64_t hi = std::min(x.second, y.second);
			if (lo < hi)
				result.emplace_back(lo, hi);
		}
	}
	Normalize(result);
	return result;
}

std::vector<Range> Complement(const std::vector<Range>& normalized)
{
	std::vector<Range> result;
	uint64_t position = 0;
	for (const Range& range : normalized)
	{
		if (range.first > position)
			result.emplace_back(position, range.first);
		position = std::max(position, range.second);
	}
	if (position < FullCircle)
		result.emplace_back(position, FullCircle);
	return result;
}

}

void ScenePortal::DrawContents(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip)
{
	if (!driver.Hooks.RenderView)
		I_FatalError("PortalDriver: the RenderView hook is not set");

	if (Setup(driver, outer, vp, clip))
	{
		const PortalSceneState& state = driver.State();
		clip.Type = Type();
		clip.CurrentPortal = this;
		clip.MirrorSide = GetMirrorSide();
		clip.Mirrored = !!(state.MirrorFlag & 1);
		clip.PlaneMirrored = !!(state.PlaneMirrorFlag & 1);
		clip.PlaneMirrorMode = state.PlaneMirrorMode;
		clip.InMirror = state.MirrorFlag > 0 || state.PlaneMirrorFlag > 0;
		clip.PortalDepth = state.RenderDepth;

		driver.Hooks.RenderView(vp, clip);
		Shutdown(driver, outer, vp);
	}
	else
	{
		driver.UnbindProgram();
		screen->RenderState()->ClearScreen();
	}
}

uint32_t ScenePortal::FrustumAngle(const FRenderViewpoint& vp)
{
	if (vp.bDoOob)
	{
		// OoBFrustumAngle
		// If pitch is larger than this you can look all around at an FOV of 90 degrees
		if (fabs(vp.HWAngles.Pitch.Degrees()) > 89.0)
			return 0xffffffff;
		int aspMult = AspectMultiplier(r_viewwindow.WidescreenRatio); // 48 == square window
		double absPitch = fabs(vp.HWAngles.Pitch.Degrees());
		// Smaller aspect ratios still clip too much. Need a better solution
		if (aspMult > 36 && absPitch > 30.0) return 0xffffffff;
		else if (aspMult > 40 && absPitch > 25.0) return 0xffffffff;
		else if (aspMult > 45 && absPitch > 20.0) return 0xffffffff;
		else if (aspMult > 47 && absPitch > 10.0) return 0xffffffff;

		double xratio = r_viewwindow.FocalTangent / vp.PitchCos;
		double floatangle = 0.05 + atan(xratio) * 48.0 / aspMult; // this is radians
		angle_t a1 = DAngle::fromRad(floatangle).BAMs();

		if (a1 >= ANGLE_90) return 0xffffffff;
		return a1;
	}

	float tilt = fabs(vp.HWAngles.Pitch.Degrees());

	// If the pitch is larger than this you can look all around at a FOV of 90 degrees
	if (tilt > 46.0f) return 0xffffffff;

	// ok, this is a gross hack that barely works...
	// but at least it doesn't overestimate too much...
	double floatangle = 2.0 + (45.0 + ((tilt / 1.9))) * vp.FieldOfView.Degrees() * 48.0 / AspectMultiplier(r_viewwindow.WidescreenRatio) / 90.0;
	angle_t a1 = DAngle::fromDeg(floatangle).BAMs();
	if (a1 >= ANGLE_180) return 0xffffffff;
	return a1;
}

void ScenePortal::ClipOutsideFrustum(const FRenderViewpoint& vp, PortalClip& clip)
{
	uint32_t a1 = FrustumAngle(vp);
	if (a1 >= ANGLE_180)
		return;

	// AddClipRangeRealAngles(yaw + a1, yaw - a1): the part of the circle outside of [yaw - a1, yaw + a1].
	std::vector<Range> visible;
	AddWrapped(visible, vp.Angles.Yaw.BAMs() - a1, 2ull * a1);
	Normalize(visible);
	for (const Range& range : Complement(visible))
		clip.AddBlockedBam(range.first, range.second);
}

void ScenePortal::ClearClipper(const PortalFrameView& outer, const FRenderViewpoint& vp, PortalClip& clip) const
{
	const FRenderViewpoint& outvp = *outer.Viewpoint;
	DAngle angleOffset = deltaangle(outvp.Angles.Yaw, vp.Angles.Yaw);

	// The view clips with the radar clipper when it is out of bounds (HWScenePortalBase::DrawContents picks it).
	bool radar = vp.bDoOob;
	clip.Radar = radar;
	clip.BlockedAngles.clear();

	// Set the clipper to the minimal visible area
	DVector3 outPos = radar ? outvp.OffPos : outvp.Pos;
	angle_t padding = radar ? 0x00200000 : 0x00000000; // Make radar clipping more aggressive (reveal less)
	std::vector<Range> visible;
	for (const PortalSurface& line : Lines)
	{
		DAngle startAngle = (DVector2(line.X2, line.Y2) - outPos.XY()).Angle() + angleOffset;
		DAngle endAngle = (DVector2(line.X1, line.Y1) - outPos.XY()).Angle() + angleOffset;

		if (deltaangle(endAngle, startAngle) < nullAngle)
		{
			// SafeRemoveClipRangeRealAngles(start + padding, end - padding): the counter clockwise range from the angle of the
			// second vertex to the angle of the first, narrowed by the padding on both sides.
			uint32_t span = endAngle.BAMs() - startAngle.BAMs();
			if (span <= 2 * (uint64_t)padding)
				continue;
			AddWrapped(visible, startAngle.BAMs() + padding, (uint64_t)span - 2 * (uint64_t)padding);
		}
	}
	Normalize(visible);

	// and finally clip it to the visible area
	uint32_t a1 = FrustumAngle(vp);
	if (!radar && a1 < ANGLE_180)
	{
		std::vector<Range> frustum;
		AddWrapped(frustum, vp.Angles.Yaw.BAMs() - a1, 2ull * a1);
		Normalize(frustum);
		visible = Intersect(visible, frustum);
	}

	for (const Range& range : Complement(visible))
		clip.AddBlockedBam(range.first, range.second);
}

void ScenePortal::InitCoverage(FLevelLocals* level, PortalClip& clip)
{
	clip.CoveredSubsectors.Resize(level->subsectors.Size());
	clip.CoveredSubsectors.Zero();
	clip.CoveredNodes.Resize(level->nodes.Size());
	clip.CoveredNodes.Zero();
	clip.HasCoverage = true;
}

uint8_t ScenePortal::SetCoverage(FLevelLocals* level, void* node, PortalClip& clip)
{
	if (level->nodes.Size() == 0)
	{
		return 0;
	}
	if (!((size_t)node & 1))  // Keep going until found a subsector
	{
		node_t* bsp = (node_t*)node;
		uint8_t coverage = SetCoverage(level, bsp->children[0], clip) | SetCoverage(level, bsp->children[1], clip);
		clip.CoveredNodes.Set(bsp->Index(), coverage != 0);
		return coverage;
	}
	else
	{
		subsector_t* sub = (subsector_t*)((uint8_t*)node - 1);
		return clip.CoveredSubsectors[sub->Index()] ? 1 : 0;
	}
}

void ScenePortal::UpdateCurrentMapSection(FLevelLocals* level, const FRenderViewpoint& vp, PortalClip& clip)
{
	int mapsection = level->PointInRenderSubsector(vp.Pos)->mapsection;
	if (vp.bDoOob || vp.bDoOrtho)
		mapsection = level->PointInRenderSubsector(vp.OffPos)->mapsection;
	clip.MapSections.Set(mapsection);
}

void ScenePortal::SetViewArea(FLevelLocals* level, FRenderViewpoint& vp)
{
	// The render_sector is better suited to represent the current position in GL
	vp.sector = level->PointInRenderSubsector(vp.Pos)->render_sector;
	if (vp.bDoOob)
		vp.sector = level->PointInRenderSubsector(vp.camera->Pos())->render_sector;
}

}
