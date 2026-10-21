/*
** LegacyWorldRenderer.Bsp.cpp
**
** Port of the LegacyWorldRenderer.Bsp partial.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion's BspTree.Nodes / Subsectors / Segments arrays are UZDoom's node_t / subsector_t tree: node->children[side]
** with bit 0 set marks a subsector (the (uint)index | IsSubsectorBit of BspNodeCompact), the node bounding boxes
** are node_t::bbox[child] (the box of the child, Helion keeps the box of the node itself: the box test of
** the child is made before descending into it), and the entities of a sector are its Thinglist (snext).
*/

#include "OpenGL/Renderers/Legacy/World/LegacyWorldRenderer.h"
#include "OpenGL/Renderers/Legacy/World/BspBox.h"
#include "Common/Shared/Frustum.h"
#include "HelionCVars.h"
#include "World/WorldStatic.h"
#include "g_levellocals.h"
#include "actor.h"
#include "r_utility.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalClip.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/Portal.h"
#include "po_man.h"
#include "RenderBlock.h"
#include "hw_cvars.h"
#include "doomstat.h"

EXTERN_CVAR(Int, r_distance_cull_type)

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

using namespace Helion::Geometry;

void LegacyWorldRenderer::TraverseBsp(FLevelLocals* world, Shared::RenderInfo& renderInfo)
{
	Common::Shared::Frustum::SetFrustumPlanes(renderInfo.Uniforms.MvpNoPitch, m_frustumPlanes);

	m_geometryRenderer->ClearBsp();
	m_geometryRenderer->SetViewPosition(m_renderData.ViewPos3D, m_renderData.ViewPosInterpolated3D);

	m_viewClipperPrev.Clear();
	m_viewClipper.Clear();
	m_viewClipperPrev.Center = m_renderData.ViewPosInterpolated;
	m_viewClipper.Center = Vec2D(m_renderData.ViewPos3D.X, m_renderData.ViewPos3D.Y);

	// UZDoom (plan D27, classic AddLine / DoSubsector): an out of bounds view does not occlude with the walls, the camera is not
	// where the player sees from. With the radar clipper (fog of war) it does not draw the subsectors that the player's body
	// (OffPos) cannot see and that were never seen, and it marks the automap with what the body sees.
	const FRenderViewpoint& vp = r_viewpoint;
	m_outOfBounds = vp.bDoOob;
	m_radar = vp.bDoOob && r_radarclipper && !(world->flags3 & LEVEL3_NOFOGOFWAR);
	m_radarClipper.Clear();
	m_radarClipper.Center = Vec2D(vp.OffPos.X, vp.OffPos.Y);
	m_geometryRenderer->SetOccludeWithWalls(!m_outOfBounds);

	// UZDoom (plan M5.2, HWScenePortalBase::ClearClipper): what is not seen through the portal. A sector stack whose viewpoint is
	// outside of its coverage blocks everything until covered subsectors unclip it; the walk does not unclip, the stencil and
	// the depth test bound the view instead, so that view starts unblocked. An out of bounds portal view clears the radar
	// clipper instead of the view clipper (HWScenePortalBase::DrawContents).
	if (m_portalClip != nullptr)
	{
		Geometry::Portals::PortalClip clip = *m_portalClip;
		if (clip.BlockedUnlessCovered)
			clip.BlockedAngles.clear();
		if (clip.Radar)
		{
			clip.ApplyTo(m_radarClipper);
		}
		else
		{
			clip.ApplyTo(m_viewClipper);
			clip.ApplyTo(m_viewClipperPrev);
		}
	}

	// r_distance_cull_type (classic IsDistanceCulled): measured from the player's body in a radar view.
	DVector3 cullCenter = m_radar ? vp.OffPos : vp.Pos;
	m_geometryRenderer->SetDistanceCull(Vec2D(cullCenter.X, cullCenter.Y), r_distance_cull_type > 0 ? vp.culldistsq : 0);

	// classic RenderBSP: a radar view walks the nodes front to back from the player's body, so the walls that hide what the
	// body cannot see are in the radar clipper before the subsectors behind them are tested.
	Vec2D pos2D(m_renderData.ViewPos3D.X, m_renderData.ViewPos3D.Y);
	Vec3D orderPosition = m_radar ? Vec3D(vp.OffPos.X, vp.OffPos.Y, vp.OffPos.Z) : m_renderData.ViewPos3D;
	if (world->nodes.Size() == 0)
		RenderBspSubsector(&world->subsectors[0], orderPosition, pos2D, m_renderData.ViewPosInterpolated, world);
	else
		RecursivelyRenderBsp(world->HeadNode(), orderPosition, pos2D, m_renderData.ViewPosInterpolated, world);
	m_geometryRenderer->SetDistanceCull(Vec2D(0, 0), 0);
	m_lastTicker = world->maptime;
}

void LegacyWorldRenderer::RecursivelyRenderBsp(void* nodePtr, const Vec3D& position, const Vec2D& pos2D, const Vec2D& prevPos2D, FLevelLocals* world)
{
	while (!((size_t)nodePtr & 1))
	{
		node_t* node = (node_t*)nodePtr;
		int front = R_PointOnSide(position.X, position.Y, node);

		// The child that is walked first, then the other one; each child has its own bounding box.
		if (ShouldRenderBox(BspBox::FromNodeBBox(node->bbox[front]), pos2D, prevPos2D))
			RecursivelyRenderBsp(node->children[front], position, pos2D, prevPos2D, world);

		if (!ShouldRenderBox(BspBox::FromNodeBBox(node->bbox[front ^ 1]), pos2D, prevPos2D))
			return;

		nodePtr = node->children[front ^ 1];
	}

	RenderBspSubsector((subsector_t*)((uint8_t*)nodePtr - 1), position, pos2D, prevPos2D, world);
}

void LegacyWorldRenderer::RenderBspSubsector(subsector_t* subsector, const Vec3D& position, const Vec2D& pos2D, const Vec2D& prevPos2D, FLevelLocals* world)
{
	(void)position;
	if (!ShouldRenderBox(BspBox::FromBoundingBox(subsector->bbox), pos2D, prevPos2D))
		return;

	// UZDoom (plan M5.2, classic DoSubsector): subsectors of map sections that the portal view cannot see, and the ones in front
	// of a line / mirror portal.
	if (m_portalClip != nullptr)
	{
		if (!m_portalClip->IsMapSectionVisible(subsector->mapsection))
			return;
		if (m_portalClip->ClipPortal != nullptr && m_portalClip->ClipPortal->ClipSubsector(subsector) == Geometry::Portals::PClipInFront)
			return;
	}
	// Never render polyobject origin subsectors because their vertices no longer are where one may expect.
	if (subsector->flags & SSECF_POLYORG)
		return;

	// UZDoom (classic DoSubsector): an out of bounds view does not show the secrets that were not found yet, unless the
	// radar clipper hides what was not seen.
	sector_t* sector = subsector->sector;
	if (m_outOfBounds && sector->isSecret() && sector->wasSecret() && !r_radarclipper)
		return;
	if (m_radar && !RadarSubsector(subsector))
		return;

	// Flats are rendered by sector, walls are rendered by subsector
	bool hasRenderedSector = m_sectorCheckCount[sector->Index()] == m_renderData.CheckCount;
	m_geometryRenderer->RenderSubsector(subsector, pos2D, prevPos2D, !hasRenderedSector);

	// UZDoom (plan D27): the sides of the polyobjects in the subsector (classic AddPolyobjs). They are dynamic sides at their
	// current position, in the sector the polyobject covers there (PolyobjectSideSector, like the static walk).
	for (FPolyNode* node = subsector->polys; node != nullptr; node = node->pnext)
	{
		FPolyObj* poly = node->poly;
		if (poly == nullptr)
			continue;
		for (side_t* side : poly->Sidedefs)
		{
			int id = side->Index();
			if (m_sideCheckCount[id] == m_renderData.CheckCount)
				continue;
			m_sideCheckCount[id] = m_renderData.CheckCount;
			m_geometryRenderer->RenderPolyobjectWall(PolyobjectSideSector(world, side, poly), side, m_renderData.ViewPos3D, m_renderData.ViewPosInterpolated3D);
		}
	}

	m_entityRenderer->RenderParticles(subsector, *m_currentRenderInfo, 0);

	// Entities are rendered by the sector
	if (hasRenderedSector)
		return;

	if (m_portalClip == nullptr)
		m_dither.CollectTargets(sector);

	int renderIndex = 0;
	for (AActor* entity = sector->thinglist; entity != nullptr; entity = entity->snext)
	{
		if (entity->renderCheckCount == m_renderData.CheckCount)
			continue;
		entity->renderCheckCount = m_renderData.CheckCount;
		renderIndex = renderIndex % EntityRenderIndexMax;
		RenderEntity(world, entity, renderIndex++);
	}
}

bool LegacyWorldRenderer::ShouldRenderBox(const Box2D& box, const Vec2D& pos2D, const Vec2D& prevPos2D)
{
	if (BspBox::Contains(box, pos2D) || BspBox::Contains(box, prevPos2D))
		return true;

	if (m_occlude && !m_frustumPlanes.BoxInFront(box))
		return false;

	// If not occluded in the first view clipper then don't check the second
	double x1, y1, x2, y2;
	BspBox::GetSpanningEdge(box, pos2D, x1, y1, x2, y2);
	if (!m_viewClipper.InsideAnyRange(x1, y1, x2, y2))
		return true;

	BspBox::GetSpanningEdge(box, prevPos2D, x1, y1, x2, y2);
	return !m_viewClipperPrev.InsideAnyRange(x1, y1, x2, y2);
}

// UZDoom (plan D27, classic DoSubsector / AddLine): the radar clipper of an out of bounds view. A subsector (without
// polyobjects) is drawn when it was seen before or when one of its segs faces the player's body and is not hidden from it
// by the walls already walked. A drawn subsector is revealed on the automap, and its blocking walls are added to the radar
// clipper, widened to reveal less.
bool LegacyWorldRenderer::RadarSubsector(subsector_t* subsector)
{
	const Vec2D& center = m_radarClipper.Center;
	auto facesCenter = [&](const seg_t* seg) {
		double dx = seg->v2->fX() - seg->v1->fX();
		double dy = seg->v2->fY() - seg->v1->fY();
		return (dx * (center.Y - seg->v1->fY())) - (dy * (center.X - seg->v1->fX())) < 0;
	};

	bool radarVisible = (subsector->flags & SSECMF_DRAWN) && !deathmatch;
	for (uint32_t i = 0; i < subsector->numlines && !radarVisible; i++)
	{
		const seg_t* seg = subsector->firstline + i;
		if (seg->v1 != nullptr && seg->v2 != nullptr && facesCenter(seg))
			radarVisible = !m_radarClipper.InsideAnyRange(seg->v1->fPos(), seg->v2->fPos());
	}
	if (!radarVisible && subsector->polys == nullptr)
		return false;

	if (radarVisible)
		subsector->sector->MoreFlags |= SECMF_DRAWN;

	// Make radar clipping more aggressive (reveal less)
	constexpr uint32_t Padding = 0x00200000;
	// classic AddLine, per seg of a drawn subsector: a seg facing the player's body that blocks the view adds its range to the
	// radar clipper first. Then a seg that faces the view and is inside the view's angles (the main clipper of an out of bounds
	// view holds only what is outside of the field of view) reveals its line on the automap, and the subsector when the seg is
	// not hidden from the player's body (which a blocking seg facing the body never is after adding itself).
	const Vec2D& viewPos = m_viewClipper.Center;
	for (uint32_t i = 0; i < subsector->numlines; i++)
	{
		seg_t* seg = subsector->firstline + i;
		if (seg->v1 == nullptr || seg->v2 == nullptr)
			continue;
		const bool facesPlayer = facesCenter(seg);
		if (facesPlayer && seg->sidedef != nullptr && seg->linedef != nullptr)
			AddRadarClipRange(seg, Padding);

		if (!radarVisible)
			continue;
		Vec2D start = seg->v1->fPos(), end = seg->v2->fPos();
		bool facesView = ((end.X - start.X) * (viewPos.Y - start.Y)) - ((end.Y - start.Y) * (viewPos.X - start.X)) < 0;
		Box2D box(Vec2D(std::min(start.X, end.X), std::min(start.Y, end.Y)), Vec2D(std::max(start.X, end.X), std::max(start.Y, end.Y)));
		if (!facesView || !m_frustumPlanes.BoxInFront(box))
			continue;
		if (!facesPlayer || !m_radarClipper.InsideAnyRange(start, end))
			subsector->flags |= SSECMF_DRAWN;
		if (seg->sidedef != nullptr && seg->linedef != nullptr)
			seg->linedef->flags |= ML_MAPPED;
	}
	return true;
}

void LegacyWorldRenderer::AddRadarClipRange(seg_t* seg, uint32_t Padding)
{
	bool blocks = seg->backsector == nullptr;
	if (!blocks && !(seg->sidedef->Flags & WALLF_POLYOBJ) && seg->frontsector != seg->backsector)
		blocks = RenderBlock::IsBlocked(seg->sidedef, seg->frontsector, seg->backsector);
	if (!blocks)
		return;

	using Shared::World::ViewClipping::ViewClipper;
	auto [smallerAngle, largerAngle] = m_radarClipper.GetAngles(seg->v1->fPos(), seg->v2->fPos());
	if (ViewClipper::AnglesSpanOriginVector(smallerAngle, largerAngle))
	{
		// The arc is [largerAngle, max] + [0, smallerAngle]: both parts grow towards the inside of the arc.
		uint32_t start = smallerAngle > UINT32_MAX - Padding ? UINT32_MAX : smallerAngle + Padding;
		uint32_t end = largerAngle < Padding ? 0 : largerAngle - Padding;
		if (start < end && !ViewClipper::AnglesSpanOriginVector(0, start) && !ViewClipper::AnglesSpanOriginVector(end, UINT32_MAX))
		{
			m_radarClipper.AddLine(0, start);
			m_radarClipper.AddLine(end, UINT32_MAX);
		}
		else
			m_radarClipper.AddLine(smallerAngle, largerAngle);
	}
	else
	{
		uint32_t start = smallerAngle < Padding ? 0 : smallerAngle - Padding;
		uint32_t end = largerAngle > UINT32_MAX - Padding ? UINT32_MAX : largerAngle + Padding;
		if (ViewClipper::AnglesSpanOriginVector(start, end))
			m_radarClipper.AddLine(smallerAngle, largerAngle);
		else
			m_radarClipper.AddLine(start, end);
	}
}

}
