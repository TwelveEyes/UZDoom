/*
** PortalClip.h
**
** UZDoom-only (plan M5.2). What the world pass of a portal view has to know about the portal it is drawn through: the clip
** plane (classic SetClipLine / SetClipHeight), the mirror parity, the map sections and subsectors that are visible, the
** angular ranges the BSP walk must not enter (HWScenePortalBase::ClearClipper) and the portal that clips segs / subsectors
** (classic mClipPortal).
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
** The portal driver (PortalDriver) fills a PortalClip while it sets a portal up and hands it to the recursion callback
** together with the FRenderViewpoint of the portal view. The callback renders the world pass from that viewpoint and
** applies the clip as follows (every item is mandatory, see PortalDriver.h for the integration list):
**  - ApplyToUniforms: clip plane + thick fog scale into the HWViewpointUniforms block of the view,
**  - ApplyTo(ViewClipper): blocked angular ranges into both view clippers after their Center is set,
**  - MapSections / CoveredSubsectors / CoveredNodes / BlockedUnlessCovered / ClipPortal: BSP walk filters,
**  - Mirrored / PlaneMirrored: the projection flip and the culling mode of the view (see PortalClip::MvpFlip()).
*/

#pragma once

#include <cstdint>
#include <utility>
#include <vector>
#include "tarray.h"
#include "vectors.h"
#include "hwrenderer/data/hw_viewpointuniforms.h"
#include "Common/Shared/World/ViewClipping/ViewClipper.h"
#include "Util/Geometry.h"

struct subsector_t;
struct FLinePortalSpan;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

class Portal;

// HWPortalTypes. HWP_SKY is not a portal here: the sky is drawn by LegacySkyRenderer.
enum class PortalType
{
	Horizon = 1,
	Skybox,
	SectorStack,
	PlaneMirror,
	Mirror,
	LineToLine
};

// PClip_* of hw_drawinfo.h (same values).
enum PortalClipResult
{
	PClipInFront,
	PClipInside,
	PClipBehind
};

enum class PortalClipKind
{
	None,
	// classic SetClipLine: clip everything behind ClipLine (x, y, dx, dy).
	Line,
	// classic SetClipHeight: clip everything below (direction 1) / above (direction -1) ClipHeight.
	Height
};

struct PortalClip
{
	PortalType Type = PortalType::Skybox;
	// classic mClipPortal: the portal whose ClipSeg / ClipSubsector / ClipPoint the BSP walk of the view calls
	// (line, mirror and line to line portals only; null otherwise). Valid for the duration of the recursion callback.
	const Portal* ClipPortal = nullptr;

	PortalClipKind Kind = PortalClipKind::None;
	// mClipLine: x, y of the first vertex, dx, dy of the line (map units, x / y of the map).
	float ClipLine[4] = { 0, 0, 0, 0 };
	// mClipHeight / mClipHeightDirection (map z).
	float ClipHeight = 0;
	float ClipHeightDirection = 0;

	// (MirrorFlag & 1), (PlaneMirrorFlag & 1) of FPortalSceneState after this portal was entered.
	bool Mirrored = false;
	bool PlaneMirrored = false;
	// FPortalSceneState::PlaneMirrorMode of the view (-1 ceiling mirror, 1 floor mirror, 0 none).
	int PlaneMirrorMode = 0;
	// HWPortal::GetMirrorSide() of the portal (-1 ceiling, 1 floor, 0 none): the BSP walk uses it to skip the flats of the mirror plane.
	int MirrorSide = 0;
	// Skyviewpoint sectors are scaled up by 16x: HWSkyboxPortal divides mThickFogDistance by 16 and multiplies mThickFogMultiplier
	// by 16. The product of all skybox portals on the way to this view (1 = none).
	float ThickFogScale = 1;
	// FPortalSceneState::renderdepth of the view (0 = main view, the portal view of a main view portal is 1).
	int PortalDepth = 0;

	// classic CurrentMapSections of the view (Level->NumMapSections bits): subsectors of other map sections cannot be seen.
	BitArray MapSections;
	// Set by the sector stack portal: classic ss_renderflags / no_renderflags SSRF_SEEN, a bit per subsector / node.
	// A subsector (node) in the set has to be walked and its angular range unclipped even if the view clipper blocks it.
	BitArray CoveredSubsectors;
	BitArray CoveredNodes;
	bool HasCoverage = false;
	// Clipper::SetBlocked(true) of the sector stack portal: the viewpoint is not inside a covered subsector, so the whole
	// circle is blocked (BlockedAngles holds it) and only covered subsectors / nodes (IsSubsectorCovered / IsNodeCovered)
	// unblock their angular range when the walk reaches them ("inside a stacked sector portal which hasn't unclipped
	// anything yet").
	bool BlockedUnlessCovered = false;

	// HWScenePortalBase::ClearClipper: diamond angle ranges (ViewClipper::ToDiamondAngle space, direction of the portal view,
	// first <= second, each well below half a circle so that ViewClipper::AddLine never reads them as wrapped) that are not
	// seen through the portal. Radar: the view is an out of bounds view (Viewpoint.bDoOob) which clips with the radar clipper
	// of the classic renderer, relative to OffPos; the integrator applies the ranges to the clipper of the OoB walk then.
	bool Radar = false;
	std::vector<std::pair<uint32_t, uint32_t>> BlockedAngles;

	// The portal this view is drawn through (classic mCurrentPortal). Its RenderAttached(driver, view) has to be called at the
	// end of the BSP walk of the view (line to line portals add the actors of the destination there), its GetMirrorSide()
	// is MirrorSide.
	const Portal* CurrentPortal = nullptr;
	// (state.MirrorFlag > 0 || state.PlaneMirrorFlag > 0): RF2_INVISIBLEINMIRRORS / RF2_ONLYVISIBLEINMIRRORS actors filter on it.
	bool InMirror = false;
	// The view was set up with the stencil of its portal (classic mPortalStencil).
	bool UsedStencil = true;
	// Orthographic views draw skybox portals with the perspective projection (classic ProjectionMatrix2).
	bool PerspectiveProjection = false;

	// Blocks the angle range [fromBam, toBam] (BAM, counter clockwise, 0 <= from <= to <= 2^32, the map direction of the angle
	// is (cos, sin) with BAM 0 = +x) of the portal view: converted to diamond angles and split into ranges of at most a
	// quarter circle.
	void AddBlockedBam(uint64_t fromBam, uint64_t toBam);

	// Adds the blocked angles to a view clipper. Call after clipper.Clear() and after Center is set.
	void ApplyTo(Shared::World::ViewClipping::ViewClipper& clipper) const
	{
		for (const auto& range : BlockedAngles)
			clipper.AddLine(range.first, range.second);
	}

	// Sets the clip plane and the scaled thick fog of the portal view in a viewpoint block that
	// Renderer::SetupViewpointUniforms has just filled from the level (it resets the clip plane and the fog).
	void ApplyToUniforms(HWViewpointUniforms& uniforms) const
	{
		switch (Kind)
		{
		case PortalClipKind::Line:
			uniforms.mClipLine = FVector4(ClipLine[0], ClipLine[1], ClipLine[2], ClipLine[3]);
			uniforms.mClipHeight = 0;
			uniforms.mClipHeightDirection = 0.f;
			break;
		case PortalClipKind::Height:
			uniforms.mClipHeight = ClipHeight;
			uniforms.mClipHeightDirection = ClipHeightDirection;
			uniforms.mClipLine.X = -1000001.f;
			break;
		case PortalClipKind::None:
			break;
		}

		if (ThickFogScale != 1)
		{
			uniforms.mThickFogDistance /= ThickFogScale;
			uniforms.mThickFogMultiplier *= ThickFogScale;
		}
	}

	// True when a clip plane is active (HWViewpointBuffer::SetViewpoint mClipPlaneInfo): the view has to call
	// FRenderState::EnableClipDistance(0, true).
	bool ClipPlaneActive() const { return Kind != PortalClipKind::None; }

	// The clip plane as RenderProgram::SetClipPlane wants it: distance = dot(plane, (x, y, z, 1)) in map coordinates, z up.
	// Line: main.vp -((y - ly) * dx + (lx - x) * dy) + 1/32768 (a tiny bit of imprecision for colinear linedefs).
	// Height: main.vp (z - height) * direction.
	FVector4 ClipPlane() const
	{
		switch (Kind)
		{
		case PortalClipKind::Line:
			return FVector4(ClipLine[3], -ClipLine[2], 0.f, ClipLine[1] * ClipLine[2] - ClipLine[0] * ClipLine[3] + 1.f / 32768.f);
		case PortalClipKind::Height:
			return FVector4(0.f, 0.f, ClipHeightDirection, -ClipHeight * ClipHeightDirection);
		default:
			return FVector4(0.f, 0.f, 0.f, 1.f);
		}
	}

	// HW mirrors the image of mirror portals (X) and plane mirror portals (Y) and flips the winding: the portal view's matrix is
	// scale(Mirrored ? -1 : 1, PlaneMirrored ? -1 : 1, 1) * projection * view in clip space.
	float MvpFlipX() const { return Mirrored ? -1.f : 1.f; }
	float MvpFlipY() const { return PlaneMirrored ? -1.f : 1.f; }
	// FPortalSceneState::isMirrored(): the winding order of the view is reversed (cull the other face).
	bool WindingFlipped() const { return Mirrored != PlaneMirrored; }

	bool IsSubsectorCovered(int subsectorIndex) const { return HasCoverage && CoveredSubsectors[subsectorIndex]; }
	bool IsNodeCovered(int nodeIndex) const { return HasCoverage && CoveredNodes[nodeIndex]; }
	// classic DoSubsector: the subsector belongs to a map section that is not visible.
	bool IsMapSectionVisible(int mapSection) const { return MapSections.Size() == 0 || MapSections[mapSection]; }
};

}
