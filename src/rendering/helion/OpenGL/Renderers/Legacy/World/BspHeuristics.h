/*
** BspHeuristics.h
**
** UZDoom-only implementation of Helion's IBspHeuristics (Helion: SinglePlayer/AutomapMarker).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion times a BSP walk of the player's view on a worker thread (the automap marker). UZDoom's renderer
** is single threaded (plan D15): Process() runs the same walk synchronously, at most once per game tic, for
** the main view in every render mode. It marks what the walk sees for UZDoom's automap like
** classic AddLine / DoSubsector (subsector_t SSECMF_DRAWN, line_t ML_MAPPED, sector_t SECMF_DRAWN).
** The static mode draws the cached geometry of the whole map, so it takes what only the seen part of the map may
** cause from this walk: the portal candidates it reaches (the static mode renders the views of those portals only) and
** the camera textures on the surfaces it reaches (FCanvasTexture::NeedUpdate, re-raised every frame of the tic).
*/

#pragma once

#include <vector>
#include <cstdint>
#include "Common/Shared/Frustum.h"
#include "Common/Shared/World/ViewClipping/ViewClipper.h"
#include "OpenGL/Renderers/Legacy/World/IBspHeuristics.h"
#include "Util/Geometry.h"

struct FLevelLocals;
struct node_t;
struct seg_t;
struct subsector_t;
struct sector_t;
struct side_t;
class FCanvasTexture;

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

class BspHeuristics : public IBspHeuristics
{
public:
	void Start(FLevelLocals* world);

	// Walks the BSP from pos once per id (game tick). mvpNoPitch is ShaderUniforms::MvpNoPitch. markAutomap: mark the
	// seen subsectors / lines / sectors (false when the radar clipper of an out of bounds view marks them instead).
	// occlude: blocking walls hide what is behind them (false for an out of bounds view, like classic AddLine).
	void Process(const ::Helion::Geometry::Vec3D& pos, const ::Helion::Geometry::mat4& mvpNoPitch, int id, bool markAutomap, bool occlude);

	bool Valid() const override { return m_valid; }
	int SubsectorCount() const override { return m_subsectorCountResult; }
	int SegCount() const override { return m_segCountResult; }
	int LineCount() const override { return m_lineCountResult; }
	int LastProcessedId() const override { return m_lastProcessedId; }
	int Microseconds() const override { return m_microseconds; }
	int64_t LastProcessedTimeStamp() const override { return m_lastProcessedTimeStamp; }
	BspHeuristicInfo& Info() override { return m_info; }
	// UZDoom: whether the last walk saw a sector with a sky plane (gates the sky clear of the static mode, gl_no_skyclear).
	bool SkyVisible() const { return m_skyVisible; }

	// UZDoom: the portal candidates (LegacyWorldRenderer::BuildPortalCandidates) the walk reports when it reaches them.
	void SetPortalCandidates(const std::vector<seg_t*>& segs, const std::vector<subsector_t*>& subsectors);
	// The portal candidate segs / subsectors and the camera textures the last walk reached.
	const std::vector<seg_t*>& SeenPortalSegs() const { return m_seenPortalSegsResult; }
	const std::vector<subsector_t*>& SeenPortalSubsectors() const { return m_seenPortalSubsectorsResult; }
	const std::vector<FCanvasTexture*>& SeenCanvasTextures() const { return m_seenCanvasTexturesResult; }

private:
	FLevelLocals* m_world = nullptr;
	Shared::World::ViewClipping::ViewClipper m_viewClipper;
	Common::Shared::FrustumPlanes m_frustumPlanes;
	std::vector<bool> m_hitLines;
	std::vector<uint8_t> m_portalSeg;
	std::vector<uint8_t> m_portalSubsector;
	std::vector<seg_t*> m_seenPortalSegs;
	std::vector<subsector_t*> m_seenPortalSubsectors;
	std::vector<FCanvasTexture*> m_seenCanvasTextures;
	std::vector<seg_t*> m_seenPortalSegsResult;
	std::vector<subsector_t*> m_seenPortalSubsectorsResult;
	std::vector<FCanvasTexture*> m_seenCanvasTexturesResult;
	BspHeuristicInfo m_info;
	bool m_valid = false;
	bool m_markAutomap = false;
	bool m_occlude = true;
	bool m_skySeen = false;
	bool m_skyVisible = true;
	int m_subsectorCount = 0;
	int m_segCount = 0;
	int m_lineCount = 0;
	int m_subsectorCountResult = 0;
	int m_segCountResult = 0;
	int m_lineCountResult = 0;
	int m_lastProcessedId = -1;
	int m_microseconds = 0;
	int64_t m_lastProcessedTimeStamp = 0;

	void MarkBspLineClips(void* node, const ::Helion::Geometry::Vec2D& position);
	void MarkSubsector(subsector_t* subsector, const ::Helion::Geometry::Vec2D& position);
	bool Occluded(const ::Helion::Geometry::Box2D& box, const ::Helion::Geometry::Vec2D& position);
	bool SegInView(const ::Helion::Geometry::Vec2D& start, const ::Helion::Geometry::Vec2D& end) const;
	void AddCanvasTexture(int texture);
	static bool HasSkyPlane(const sector_t* sector);
};

}
