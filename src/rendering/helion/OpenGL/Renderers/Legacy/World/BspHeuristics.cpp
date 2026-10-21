/*
** BspHeuristics.cpp
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
*/

#include <algorithm>
#include <chrono>
#include "OpenGL/Renderers/Legacy/World/BspHeuristics.h"
#include "OpenGL/Renderers/Legacy/World/BspBox.h"
#include "RenderBlock.h"
#include "g_levellocals.h"
#include "r_utility.h"
#include "c_cvars.h"
#include "texturemanager.h"
#include "textures.h"

EXTERN_CVAR(Bool, r_radarclipper)

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

using namespace Helion::Geometry;

void BspHeuristics::Start(FLevelLocals* world)
{
	m_world = world;
	m_viewClipper.Clear();
	m_hitLines.assign(world->lines.Size(), false);
	m_valid = false;
	m_lastProcessedId = -1;
	m_microseconds = 0;
	m_lastProcessedTimeStamp = 0;
	m_info = BspHeuristicInfo();
	m_portalSeg.assign(world->segs.Size(), 0);
	m_portalSubsector.assign(world->subsectors.Size(), 0);
	m_seenPortalSegsResult.clear();
	m_seenPortalSubsectorsResult.clear();
	m_seenCanvasTexturesResult.clear();
}

void BspHeuristics::SetPortalCandidates(const std::vector<seg_t*>& segs, const std::vector<subsector_t*>& subsectors)
{
	for (seg_t* seg : segs)
		m_portalSeg[seg->Index()] = 1;
	for (subsector_t* sub : subsectors)
		m_portalSubsector[sub->Index()] = 1;
}

void BspHeuristics::Process(const Vec3D& pos, const mat4& mvpNoPitch, int id, bool markAutomap, bool occlude)
{
	if (m_world == nullptr || m_lastProcessedId == id)
		return;

	auto start = std::chrono::steady_clock::now();

	m_markAutomap = markAutomap;
	m_occlude = occlude;
	m_subsectorCount = 0;
	m_segCount = 0;
	m_lineCount = 0;
	m_skySeen = false;
	m_viewClipper.Clear();
	m_viewClipper.Center = pos.XY();
	std::fill(m_hitLines.begin(), m_hitLines.end(), false);
	m_seenPortalSegs.clear();
	m_seenPortalSubsectors.clear();
	m_seenCanvasTextures.clear();

	Common::Shared::Frustum::SetFrustumPlanes(mvpNoPitch, m_frustumPlanes);

	if (m_world->nodes.Size() == 0)
		MarkSubsector(&m_world->subsectors[0], pos.XY());
	else
		MarkBspLineClips(m_world->HeadNode(), pos.XY());

	m_lastProcessedId = id;

	auto end = std::chrono::steady_clock::now();
	m_valid = true;
	m_lastProcessedTimeStamp = std::chrono::steady_clock::now().time_since_epoch().count();
	m_microseconds = (int)std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
	m_subsectorCountResult = m_subsectorCount;
	m_segCountResult = m_segCount;
	m_lineCountResult = m_lineCount;
	m_skyVisible = m_skySeen;
	m_seenPortalSegsResult.swap(m_seenPortalSegs);
	m_seenPortalSubsectorsResult.swap(m_seenPortalSubsectors);
	m_seenCanvasTexturesResult.swap(m_seenCanvasTextures);
}

void BspHeuristics::MarkBspLineClips(void* node, const Vec2D& position)
{
	while (!((size_t)node & 1))
	{
		node_t* bsp = (node_t*)node;
		int front = R_PointOnSide(position.X, position.Y, bsp);

		// Helion tests the node's bounding box before descending: UZDoom stores it per child.
		if (!Occluded(BspBox::FromNodeBBox(bsp->bbox[front]), position))
			MarkBspLineClips(bsp->children[front], position);

		if (Occluded(BspBox::FromNodeBBox(bsp->bbox[front ^ 1]), position))
			return;

		node = bsp->children[front ^ 1];
	}

	MarkSubsector((subsector_t*)((uint8_t*)node - 1), position);
}

void BspHeuristics::MarkSubsector(subsector_t* subsector, const Vec2D& position)
{
	m_subsectorCount++;

	// UZDoom (classic DoSubsector): an out of bounds view skips the secrets that were not found yet.
	if (!m_occlude && subsector->sector->isSecret() && subsector->sector->wasSecret() && !r_radarclipper)
		return;

	// UZDoom: classic DoSubsector marks the sector of every subsector the walk reaches.
	if (m_markAutomap && !(subsector->flags & SSECF_POLYORG))
		subsector->sector->MoreFlags |= SECMF_DRAWN;

	if (m_portalSubsector[subsector->Index()])
		m_seenPortalSubsectors.push_back(subsector);
	AddCanvasTexture(subsector->sector->GetTexture(sector_t::floor).GetIndex());
	AddCanvasTexture(subsector->sector->GetTexture(sector_t::ceiling).GetIndex());
	for (uint32_t i = 0; i < subsector->numlines; i++)
	{
		seg_t* edge = subsector->firstline + i;
		Vec2D start = edge->v1->fPos();
		Vec2D end = edge->v2->fPos();
		double dx = end.X - start.X;
		double dy = end.Y - start.Y;
		bool front = (dx * (position.Y - start.Y)) - (dy * (position.X - start.X)) < 0;

		if (edge->linedef == nullptr)
		{
			// UZDoom (classic AddLine): a miniseg that is seen marks its subsector for the automap.
			if (m_markAutomap && front && !(subsector->flags & SSECMF_DRAWN) && SegInView(start, end) &&
				!m_viewClipper.InsideAnyRange(start, end))
				subsector->flags |= SSECMF_DRAWN;
			continue;
		}

		// The static mode: a portal candidate or a camera texture of a reached subsector is in view when the seg is in
		// front of the view (CollectPortals tests the facing; the plane of a sector portal is seen over walls that
		// the clipper hides).
		if (SegInView(start, end))
		{
			if (m_portalSeg[edge->Index()])
				m_seenPortalSegs.push_back(edge);
			if (front || edge->backsector != nullptr)
			{
				for (int tier = 0; tier < 3; tier++)
					AddCanvasTexture(edge->sidedef->textures[tier].texture.GetIndex());
			}
		}

		if (edge->backsector == nullptr && !front)
			continue;

		auto [smallerAngle, largerAngle] = m_viewClipper.GetAngles(start, end);
		if (m_viewClipper.InsideAnyRange(smallerAngle, largerAngle))
			continue;

		if (m_occlude && (edge->backsector == nullptr || RenderBlock::IsBlocked(edge->sidedef, edge->frontsector, edge->backsector)))
			m_viewClipper.AddLine(smallerAngle, largerAngle);

		m_segCount++;

		// UZDoom (FPortalSceneState::RenderFirstSkyPortal): the view sees a sky when a seen seg bounds a sector with a sky plane
		// (HWFlat / HWWall::SkyPlane, SkyTop / SkyBottom of the sector behind it).
		if (!m_skySeen && SegInView(start, end) &&
			(HasSkyPlane(edge->frontsector) || (edge->backsector != nullptr && HasSkyPlane(edge->backsector))))
			m_skySeen = true;

		// UZDoom (classic AddLine): a seg that passes the clipper reveals its subsector and its line on the automap.
		if (m_markAutomap && SegInView(start, end))
		{
			subsector->flags |= SSECMF_DRAWN;
			edge->linedef->flags |= ML_MAPPED;
		}

		unsigned lineId = edge->linedef->Index();
		if (m_hitLines[lineId])
			continue;

		m_hitLines[lineId] = true;

		line_t* line = edge->linedef;
		if (!m_frustumPlanes.PointInFrustum(line->v1->fX(), line->v1->fY()) &&
			!m_frustumPlanes.PointInFrustum(line->v2->fX(), line->v2->fY()))
			continue;

		m_lineCount++;
	}
}

bool BspHeuristics::HasSkyPlane(const sector_t* sector)
{
	return sector->GetTexture(sector_t::ceiling) == skyflatnum || sector->GetTexture(sector_t::floor) == skyflatnum;
}

void BspHeuristics::AddCanvasTexture(int texture)
{
	FGameTexture* gameTexture = TexMan.GameByIndex(texture, true);
	if (gameTexture == nullptr || !gameTexture->isHardwareCanvas())
		return;

	FCanvasTexture* canvas = static_cast<FCanvasTexture*>(gameTexture->GetTexture());
	if (std::find(m_seenCanvasTextures.begin(), m_seenCanvasTextures.end(), canvas) == m_seenCanvasTextures.end())
		m_seenCanvasTextures.push_back(canvas);
}

bool BspHeuristics::SegInView(const Vec2D& start, const Vec2D& end) const
{
	Box2D box(Vec2D(std::min(start.X, end.X), std::min(start.Y, end.Y)), Vec2D(std::max(start.X, end.X), std::max(start.Y, end.Y)));
	return m_frustumPlanes.BoxInFront(box);
}

bool BspHeuristics::Occluded(const Box2D& box, const Vec2D& position)
{
	if (BspBox::Contains(box, position))
		return false;

	if (!m_frustumPlanes.BoxInFront(box))
		return true;

	double x1, y1, x2, y2;
	BspBox::GetSpanningEdge(box, position, x1, y1, x2, y2);
	return m_viewClipper.InsideAnyRange(x1, y1, x2, y2);
}

}
