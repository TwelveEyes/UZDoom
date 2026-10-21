/*
** GeometryRenderer.Sector3D.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.GeometryRenderer.Sector3D.cs
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** UZDoom differences (see also the header comment of GeometryRenderer.h):
**  - Helion's fake sector lines/sides do not exist. RenderSectorLine3D walks Sector3D::ParentSector->Lines and the
**    "use side" is the sentinel m_fakeSide, filled per call with the real linedef of the visible parent side.
**  - m_fakeSide is the render proxy (Helion m_fakeSide + m_fakeWall + m_fakeSideScrollData). Helion's shared m_fakeWall is
**    represented by writing the same texture/scale/offset into all three textures[] tiers of the sentinel. The scroll Y
**    offset (Helion SideScrollData) is the Y offset of the tiers, the previous-tick scroll is applied to PrevV afterwards.
**  - Helion's temporary Line.Flags.Unpegged mutation is done on the real linedef flags (ML_DONTPEGTOP/BOTTOM) and restored.
**  - Sector snapshots (RenderSector) have no LastRenderChangeGametick: Helion uses it to invalidate cached vertices, the
**    sentinel side never uses the vertex caches.
*/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"
#include "World/WorldStatic.h"
#include "doomdata.h"
#include "gametexture.h"
#include "r_defs.h"
#include "texturemanager.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

using RS = ::Helion::World::Geometry::Sectors::RenderSector;
using ::Helion::World::Geometry::Sectors::PlaneFace3D;
using ::Helion::World::Geometry::Sectors::WallHeights;

namespace
{

constexpr uint32_t UnpeggedMask = ML_DONTPEGTOP | ML_DONTPEGBOTTOM;

int TierOf(::Helion::World::Geometry::Walls::WallLocation location)
{
	using Loc = ::Helion::World::Geometry::Walls::WallLocation;
	switch (location)
	{
	case Loc::Upper: return side_t::top;
	case Loc::Lower: return side_t::bottom;
	default: return side_t::mid;
	}
}

bool UnpeggedLower(const side_t* side)
{
	return (side->linedef->flags & ML_DONTPEGBOTTOM) != 0;
}

// Helion SectorPlane3D.LightSector is never null; a null sector falls back to the slice sector.
RS LightSectorOf(sector_t* sector, const RS& fallback)
{
	return sector == nullptr ? fallback : RS(sector);
}


}

void GeometryRenderer::SetSectorForLineRendering3D(Sector3D* sector3D)
{
	const WallHeights& heights = sector3D->GetWallHeights();
	m_wallSector.Ceiling.Z = heights.TopZ;
	m_wallSector.Ceiling.PrevZ = heights.PrevTopZ;
	m_wallSector.Floor.Z = heights.BottomZ;
	m_wallSector.Floor.PrevZ = heights.PrevBottomZ;
}

void GeometryRenderer::RenderSectorLine3D(Sector3D* sector3D, int lineIndex, bool renderFront, bool renderBack, const RenderVertices3DAction& renderVertices)
{
	sector_t* parent = sector3D->ParentSector;
	line_t* line = parent->Lines[lineIndex];

	// Helion NoRenderSector3D: fake lines only exist for two sided lines between different sectors.
	if (line->backsector == nullptr || line->frontsector == line->backsector)
		return;

	// Helion: the fake front side faces the neighbour (parentBack), the fake back side the inside of the parent sector.
	bool parentIsFront = line->frontsector == parent;
	side_t* parentFront = parentIsFront ? line->sidedef[0] : line->sidedef[1];
	side_t* parentBack = parentIsFront ? line->sidedef[1] : line->sidedef[0];

	if (renderFront && parentBack != nullptr)
		RenderSide3D(sector3D, parentBack, parentFront, m_wallSector, true, renderVertices);

	if (renderBack && sector3D->ShouldRenderInsideWalls() && parentFront != nullptr)
		RenderSide3D(sector3D, parentFront, parentBack, m_wallSector, false, renderVertices);
}

void GeometryRenderer::RenderSide3D(Sector3D* sector3D, side_t* parentSide, side_t* oppositeParentSide, const RS& wallSector, bool isFront,
	const RenderVertices3DAction& renderVertices)
{
	WallHeights newWallHeights;
	if (parentSide == nullptr || !sector3D->CalculateWallHeights(parentSide, newWallHeights))
		return;

	// UZDoom: Helion copies parentSide.Offset/Middle.Offset and the control side scale into the fake side;
	// RenderWallSlices3D reads them from parentSide and sector3D->ControlSide directly.
	std::span<SectorPlane3D> traversePlanes3D = m_sector3DTable.SectorPlanes3D(parentSide->sector);

	if (oppositeParentSide != nullptr && sector3D->RenderDataStyle != Data::RenderDataStyle::Normal)
	{
		// Use the other side to split the translucent 3D wall. If both sides have 3D sectors then merge and sort.
		if (traversePlanes3D.empty())
			traversePlanes3D = m_sector3DTable.SectorPlanes3D(oppositeParentSide->sector);
		else
			traversePlanes3D = MergePlanes(m_sector3DTable.SectorPlanes3D(parentSide->sector), m_sector3DTable.SectorPlanes3D(oppositeParentSide->sector), sector3D);
	}

	RS otherSector = oppositeParentSide != nullptr ? RS(oppositeParentSide->sector) : RS();
	// The geometric side of the real line the viewer looks at (Helion's fake front/back normalization).
	(void)isFront;
	bool geometricFront = parentSide->linedef->sidedef[0] == parentSide;

	RenderWallSliceResult result = RenderWallSlices3D(parentSide, WallLocation::Middle3D, geometricFront, oppositeParentSide, wallSector, otherSector,
		traversePlanes3D, m_renderSectorSliceFunc3D, parentSide, false, true, sector3D, &newWallHeights, sector3D->RenderDataStyle, parentSide->sector);

	// UZDoom: Helion hands its fake side (one per fake line face) to the callback and the static cache keys the wall by it,
	// using the fake wall's TextureHandle. Here the real parent side the face belongs to is the key (RenderStaticState::Wall3D,
	// unique per face: the outside face uses the neighbour's side, the inside face the parent's side), and the texture of the
	// fake wall is passed when no 3D plane slice reported one (the slice loop only records it for its first slice).
	if (!result.Vertices.empty() && renderVertices)
	{
		GLLegacyTexture* texture = result.Texture;
		if (texture == nullptr && m_glTextureManager != nullptr)
			texture = m_glTextureManager->GetTexture(m_fakeSide->GetTexture(side_t::mid).GetIndex());
		renderVertices(parentSide, WallLocation::Middle3D, wallSector, texture, result.Vertices, sector3D);
	}
}

std::span<GeometryRenderer::SectorPlane3D> GeometryRenderer::MergePlanes(std::vector<SectorPlane3D>& a, std::vector<SectorPlane3D>& b, Sector3D* ignorePlane)
{
	int checkCount = ::Helion::World::WorldStatic::CheckCounter++;
	m_mergePlanes.clear();
	m_mergePlanes.reserve(a.size() + b.size());

	size_t indexA = 0;
	size_t indexB = 0;

	// Merge sort
	while (indexA < a.size() && indexB < b.size())
	{
		SectorPlane3D& planeA = a[indexA];
		SectorPlane3D& planeB = b[indexB];

		bool validA = planeA.CheckCount != checkCount || planeA.Sector3DPtr == ignorePlane;
		bool validB = planeB.CheckCount != checkCount || planeB.Sector3DPtr == ignorePlane;
		planeA.CheckCount = checkCount;
		planeB.CheckCount = checkCount;

		if (!validA)
		{
			if (validB)
				m_mergePlanes.push_back(planeB);
			indexA++;
			indexB++;
			continue;
		}

		if (!validB)
		{
			if (validA)
				m_mergePlanes.push_back(planeA);
			indexA++;
			indexB++;
			continue;
		}

		if (::Helion::World::Geometry::Sectors::Sector3DTable::PlaneHeightKeyCompare(planeA, planeB) <= 0)
		{
			m_mergePlanes.push_back(planeA);
			indexA++;
		}
		else
		{
			m_mergePlanes.push_back(planeB);
			indexB++;
		}
	}

	while (indexA < a.size())
		m_mergePlanes.push_back(a[indexA++]);

	while (indexB < b.size())
		m_mergePlanes.push_back(b[indexB++]);

	return std::span<SectorPlane3D>(m_mergePlanes.data(), m_mergePlanes.size());
}

RenderWallSliceResult GeometryRenderer::RenderWallSlices3D(side_t* side, WallLocation wall, bool isFrontSide,
	side_t* otherSide, const RS& facingSector, const RS& otherSector, std::span<SectorPlane3D> traversePlanes3D,
	const RenderSliceFunc& renderFunc,
	side_t* offsetSide, bool renderSkySide, bool allowAlpha,
	Sector3D* anchorSector3D, const WallHeights* wallHeights3D, Data::RenderDataStyle style, sector_t* lightSector3D)
{
	assert(wall != WallLocation::Middle3D || wallHeights3D != nullptr);	// Rendering 3D middle requires WallHeights3D to be set.

	RenderWallSliceResult finalResult;
	m_vertices.clear();

	// UZDoom: sloped planes. A plane's Z is its texture height, which can be far from where a sloped plane crosses this side.
	// The slices are cut at the height of each sloped plane at the middle of the line (re-sorted), the corners of one sided
	// slices then follow the slope exactly (ApplySliceSlopes). Clipped 3D planes (FakeTop/FakeBottom) keep their height.
	bool hasSlope = false;
	for (const SectorPlane3D& plane3D : traversePlanes3D)
	{
		if (plane3D.Plane.Sector != nullptr && plane3D.Plane.Plane().isSlope())
		{
			hasSlope = true;
			break;
		}
	}
	if (hasSlope)
	{
		const DVector2 mid = (side->linedef->v1->fPos() + side->linedef->v2->fPos()) * 0.5;
		m_slopePlanes.assign(traversePlanes3D.begin(), traversePlanes3D.end());
		for (SectorPlane3D& plane3D : m_slopePlanes)
		{
			RenderPlane& plane = plane3D.Plane;
			if (plane.Sector == nullptr || !plane.Plane().isSlope() || plane.Z != plane3D.ControlPlane.Z)
				continue;
			const double z = plane.Plane().ZatPoint(mid);
			plane.PrevZ += z - plane.Z;
			plane.Z = z;
			plane3D.UpdateSortKey();
		}
		std::stable_sort(m_slopePlanes.begin(), m_slopePlanes.end(),
			[](const SectorPlane3D& a, const SectorPlane3D& b) { return ::Helion::World::Geometry::Sectors::Sector3DTable::PlaneHeightKeyCompare(a, b) < 0; });
		traversePlanes3D = std::span<SectorPlane3D>(m_slopePlanes.data(), m_slopePlanes.size());
	}

	// UZDoom: anchorSector3D != nullptr means side is the real parent side the viewer sees, and the Helion fake side's
	// sector is the parent sector of the 3D sector.
	sector_t* sideSectorPtr = anchorSector3D != nullptr ? anchorSector3D->ParentSector : side->sector;
	RS sideSector(sideSectorPtr);
	line_t* line = side->linedef;
	// Helion Side.PartnerSide != null (the fake line of a 3D sector never has a partner for Middle3D walls).
	bool hasPartner = anchorSector3D == nullptr && line->sidedef[1] != nullptr;
	const WallLocation sliceLocation = wall == WallLocation::Middle3D ? WallLocation::Middle : wall;
	const int tier = TierOf(sliceLocation);

	// Because of how the WorldTriangulator handles mapping UV coordinates based on flags they are fudged here to fix alignment.
	RenderPlane anchorPlane = CalculateAnchorPlane(side, wall, sideSector, otherSector, anchorSector3D);
	double anchorZ = anchorPlane.Z;
	double prevAnchorZ = anchorPlane.PrevZ;

	// UZDoom: the real linedef flags are changed temporarily (the fake line of a 3D sector has no pegging of its own).
	const uint32_t saveFlags = line->flags;
	const bool originalLower = anchorSector3D == nullptr && (saveFlags & ML_DONTPEGBOTTOM) != 0;
	const bool unpeggedLower = wall == WallLocation::Middle && hasPartner && originalLower;
	const bool unpeggedUpper = wall == WallLocation::Upper;
	line->flags = (saveFlags & ~UnpeggedMask) | (unpeggedLower ? (uint32_t)ML_DONTPEGBOTTOM : 0u) | (unpeggedUpper ? (uint32_t)ML_DONTPEGTOP : 0u);

	double saveGapZ = ::Helion::World::WorldStatic::LineVertexGapBottomZ;
	::Helion::World::WorldStatic::LineVertexGapBottomZ = 0;

	SetWallSliceSector(sideSector, m_wallSector, m_sliceSector);

	// UZDoom: Side.Offset + Wall.Offset + scroll collapse into the sidedef texture offset.
	// For 3D sectors the offset comes from the visible parent side (middle tier) plus the control side offset.
	const int offsetTier = anchorSector3D != nullptr ? (int)side_t::mid : tier;
	float offsetX = (float)side->GetTextureXOffset(offsetTier);
	float offsetY = (float)side->GetTextureYOffset(offsetTier);
	if (anchorSector3D != nullptr)
	{
		offsetX += anchorSector3D->GetOffsetX();
		offsetY += anchorSector3D->GetOffsetY();
	}

	double scaleX;
	double scaleY;
	FTextureID textureId;
	if (anchorSector3D != nullptr)
	{
		scaleX = anchorSector3D->ControlSide->GetTextureXScale(side_t::mid);
		scaleY = anchorSector3D->ControlSide->GetTextureYScale(side_t::mid);
		// Helion Sector3D.CreateSector3DLines: control side middle, or the neighbour of the parent line for the parent upper/lower texture flags.
		int logical = anchorSector3D->GetLogicalWallLocation();
		if (logical == side_t::mid)
		{
			textureId = anchorSector3D->ControlSide->GetTexture(side_t::mid);
		}
		else
		{
			line_t* parentLine = side->linedef;
			side_t* neighbour = parentLine->frontsector == anchorSector3D->ParentSector ? parentLine->sidedef[1] : parentLine->sidedef[0];
			textureId = neighbour != nullptr ? neighbour->GetTexture(logical) : FTextureID();
		}
	}
	else
	{
		scaleX = side->GetTextureXScale(tier);
		scaleY = side->GetTextureYScale(tier);
		textureId = side->GetTexture(tier);
	}

	side_t* fakeSide = m_fakeSide.get();
	fakeSide->linedef = line;
	fakeSide->sector = sideSectorPtr;
	fakeSide->Flags = anchorSector3D != nullptr ? 0 : side->Flags;
	if (anchorSector3D == nullptr)
	{
		fakeSide->Light = side->Light;
		for (int i = 0; i < 3; i++)
			fakeSide->TierLights[i] = side->TierLights[i];
	}
	fakeSide->SetAlpha(anchorSector3D == nullptr ? 1.0 : (double)anchorSector3D->Alpha);
	for (int i = 0; i < 3; i++)
	{
		// Helion shares one m_fakeWall between the three walls of m_fakeSide.
		side_t::part& part = fakeSide->textures[i];
		part.texture = textureId;
		part.xScale = scaleX;
		part.yScale = scaleY;
		part.xOffset = offsetX;
		part.yOffset = 0;
		part.skew = side_t::skew_none;
	}
	m_wallScrollY = 0;
	m_wallScrollPrevY = 0;

	GLLegacyTexture* fakeTexture = nullptr;
	if (m_glTextureManager != nullptr && textureId.isValid())
		fakeTexture = m_glTextureManager->GetTexture(textureId.GetIndex());
	// UZDoom: the two sided middle slice moves Z instead of V, only the V based walls get the previous scroll fix.
	const bool adjustPrevV = !(sliceLocation == WallLocation::Middle && hasPartner);

	RS lightSector = lightSector3D != nullptr ? RS(lightSector3D) : sideSector;
	RenderWallSliceResult result;
	RenderWallSliceArgs args;
	args.Side = fakeSide;
	args.IsFrontSide = isFrontSide;
	args.WallSector = m_sliceSector;
	args.LightSector = m_sliceSector;
	args.RenderSkySide = renderSkySide;
	args.OtherSide = otherSide;
	args.FacingSector = facingSector;
	args.OtherSector = otherSector;
	args.OffsetSide = offsetSide;
	args.AllowAlpha = allowAlpha;
	args.Style = style;
	args.WallLocation = sliceLocation;
	args.StartOffsetY = offsetY / scaleY;

	// Helion Side.IsFront of the front/back 3D face: the inside face looks from the parent sector of the 3D sector.
	const bool insideFace = anchorSector3D != nullptr && side->sector == anchorSector3D->ParentSector;

	// Applies the previous-tick scroll to PrevV (Helion interpolates SideScrollData.Previous in the triangulator).
	auto fixPrevScroll = [&](std::span<DynamicVertex> vertices)
	{
		if (!adjustPrevV || fakeTexture == nullptr || m_wallScrollPrevY == m_wallScrollY)
			return;
		float delta = (m_wallScrollPrevY - m_wallScrollY) * fakeTexture->UVInverse.Y / (float)std::abs(scaleY);
		for (DynamicVertex& vertex : vertices)
			vertex.PrevV += delta;
	};

	// UZDoom: sloped light planes. The slices have one height per plane (the plane's texture Z), classic HWWall::SplitWall cuts
	// the wall along the sloped plane at each corner (3dfloortest: a sloped 3D floor shades the walls below it). The one sided
	// slice applies the planes when it builds its vertices (RenderOneSided -> ApplySliceSlopes). 3D floor sides keep flat heights.
	auto renderSlice = [&](const RenderPlane& top, const RenderPlane& bottom)
	{
		if (wallHeights3D == nullptr)
		{
			m_sliceSlopeTop = &top;
			m_sliceSlopeBottom = &bottom;
		}
		RenderWallSliceResult sliceResult = renderFunc(args);
		m_sliceSlopeTop = nullptr;
		m_sliceSlopeBottom = nullptr;
		return sliceResult;
	};

	const bool renderThrough = style != Data::RenderDataStyle::Normal;
	int lastPlane3D = -1;

	double addOffsetZ = 0;
	double prevAddOffsetZ = 0;
	// If this 3D sector is above the first plane then add the offset since it's not traversed. This happens when an upper is covering a 3D sector.
	if (wallHeights3D != nullptr && !traversePlanes3D.empty() && (wallHeights3D->TopZ > traversePlanes3D[0].Plane.Z || wallHeights3D->PrevTopZ > traversePlanes3D[0].Plane.PrevZ))
	{
		addOffsetZ = wallHeights3D->TopZ - traversePlanes3D[0].Plane.Z;
		prevAddOffsetZ = wallHeights3D->PrevTopZ - traversePlanes3D[0].Plane.PrevZ;
	}

	SetWallOffset(fakeSide, offsetY, GetStartAnchorZ(side, wall, sideSector, otherSector, wallHeights3D), anchorZ, prevAnchorZ, addOffsetZ, prevAddOffsetZ);

	for (int i = 0; i < (int)traversePlanes3D.size() - 1; i++)
	{
		args.Count = i;
		SectorPlane3D& plane3D = traversePlanes3D[i];
		SectorPlane3D& nextPlane3D = traversePlanes3D[i + 1];
		double planeZ = plane3D.GetZ();
		double nextPlaneZ = nextPlane3D.GetZ();
		lastPlane3D = i + 1;
		// Can't skip first since it can generate sky vertices
		bool canSkip = i != 0 || anchorSector3D != nullptr;
		if (canSkip && planeZ <= nextPlaneZ)
			continue;

		if (plane3D.NoRenderWall || nextPlane3D.NoRenderWall)
		{
			SetWallOffset(fakeSide, offsetY, nextPlane3D.Plane, anchorZ, prevAnchorZ);
			continue;
		}

		SetSectorToSlice(m_sliceSector, plane3D.Plane, nextPlane3D.Plane, wallHeights3D);

		if (canSkip && m_sliceSector.Ceiling.Z <= m_sliceSector.Floor.Z)
			continue;

		if (renderThrough && plane3D.Sector3DPtr != anchorSector3D && plane3D.Sector3DPtr != nullptr && plane3D.Sector3DPtr->IsSolid() &&
			plane3D.Face == PlaneFace3D::Top && nextPlane3D.Face == PlaneFace3D::Bottom)
		{
			if (anchorSector3D != nullptr && anchorSector3D->ParentSectorId == plane3D.Sector3DPtr->ParentSectorId)
			{
				anchorZ = planeZ;
				prevAnchorZ = plane3D.GetPrevZ();
			}

			if (m_sliceSector.Ceiling.Z > m_sliceSector.Floor.Z)
				SetWallOffset(fakeSide, offsetY, nextPlane3D.Plane, anchorZ, prevAnchorZ);

			continue;
		}

		// Front side is normal light sector. Back side is the inside of the 3D sector and needs to use inside light sector that can be set from Sector3D.RestrictLighting flag.
		args.WallSector = m_sliceSector;
		args.LightSector = LightSectorOf(insideFace ? nextPlane3D.LightInsideSector : nextPlane3D.LightSector, m_sliceSector);
		// Helion forces the fake side to ignore the cached vertices (LastRenderGametick = -1): the sentinel never uses the caches.

		result = renderSlice(plane3D.Plane, nextPlane3D.Plane);
		fixPrevScroll(result.Vertices);

		// Skip inside slices that are not visible.
		if (ShouldSkipInsideSlice(plane3D, nextPlane3D, m_sliceSector))
		{
			// If either planes have transparent pixels then it's possible for this slice to be visible.
			// glTextureManager is null for integration tests.
			if (m_glTextureManager == nullptr ||
				(m_glTextureManager->GetTexture(plane3D.ControlPlane.TextureHandle)->TransparentPixelCount == 0 &&
				m_glTextureManager->GetTexture(nextPlane3D.ControlPlane.TextureHandle)->TransparentPixelCount == 0))
			{
				SetWallOffsetFromResult(result, anchorSector3D, offsetY, nextPlane3D, anchorZ, prevAnchorZ);
				continue;
			}
		}

		// Ignore vertices if the plane heights are invalid. Still may need to set generated sky vertices below.
		if (planeZ > nextPlaneZ && m_sliceSector.Ceiling.Z > m_sliceSector.Floor.Z)
			AddVertices(m_vertices, result.Vertices);

		if (i == 0)
		{
			finalResult.Texture = result.Texture;
			finalResult.SkyVertices = result.SkyVertices;
			finalResult.SkyVertices2 = result.SkyVertices2;
			args.RenderSkySide = false;
			::Helion::World::WorldStatic::LineVertexGapTopZ = 0;
			::Helion::World::WorldStatic::LineVertexGapBottomZ = 0;
		}

		SetWallOffsetFromResult(result, anchorSector3D, offsetY, nextPlane3D, anchorZ, prevAnchorZ);
	}

	::Helion::World::WorldStatic::LineVertexGapTopZ = saveGapZ;
	::Helion::World::WorldStatic::LineVertexGapBottomZ = saveGapZ;

	SetSectorToSlice(m_sliceSector, lastPlane3D == -1 ? sideSector.Ceiling : traversePlanes3D[lastPlane3D].Plane, sideSector.Floor, wallHeights3D);

	if (m_sliceSector.Ceiling.Z > m_sliceSector.Floor.Z)
	{
		args.WallSector = m_sliceSector;
		args.LightSector = lightSector;
		result = renderSlice(lastPlane3D == -1 ? sideSector.Ceiling : traversePlanes3D[lastPlane3D].Plane, sideSector.Floor);
		fixPrevScroll(result.Vertices);
		AddVertices(m_vertices, result.Vertices);
	}

	line->flags = saveFlags;
	finalResult.Vertices = std::span<DynamicVertex>(m_vertices.data(), m_vertices.size());
	return finalResult;
}

void GeometryRenderer::SetWallOffsetFromResult(const RenderWallSliceResult& result, Sector3D* anchorSector3D, float offsetY, const SectorPlane3D& nextPlane3D, double anchorZ, double prevAnchorZ)
{
	if (result.AddOffset && m_sliceSector.Ceiling.Z > m_sliceSector.Floor.Z && (anchorSector3D == nullptr || !result.Vertices.empty()))
		SetWallOffset(m_fakeSide.get(), offsetY, nextPlane3D.Plane, anchorZ, prevAnchorZ);
}

bool GeometryRenderer::ShouldSkipInsideSlice(const SectorPlane3D& plane3D, const SectorPlane3D& nextPlane3D, const RS& sliceSector)
{
	Sector3D* sector3D = plane3D.Sector3DPtr != nullptr ? plane3D.Sector3DPtr : nextPlane3D.Sector3DPtr;
	return sector3D != nullptr && plane3D.Face == PlaneFace3D::Top && nextPlane3D.Face == PlaneFace3D::Bottom &&
		sliceSector.Floor.Z < sliceSector.Ceiling.Z &&
		!sector3D->IsLightTransfer() &&
		sector3D->RenderDataStyle == Data::RenderDataStyle::Normal && sector3D->IsSolid();
}

void GeometryRenderer::SetWallSliceSector(const RS& sideSector, const RS& wallSector3D, RS& wallSector)
{
	wallSector.Ceiling.Z = wallSector3D.Ceiling.Z;
	wallSector.Ceiling.PrevZ = wallSector3D.Ceiling.PrevZ;
	wallSector.Floor.Z = wallSector3D.Floor.Z;
	wallSector.Floor.PrevZ = wallSector3D.Floor.PrevZ;
	wallSector.Ceiling.TextureHandle = sideSector.Ceiling.TextureHandle;
	wallSector.Floor.TextureHandle = sideSector.Floor.TextureHandle;
	wallSector.CeilingSkyTextureHandle = sideSector.CeilingSkyTextureHandle;
	wallSector.FloorSkyTextureHandle = sideSector.FloorSkyTextureHandle;
}

GeometryRenderer::RenderPlane GeometryRenderer::GetStartAnchorZ(side_t* side, WallLocation wall, const RS& sideSector, const RS& otherSector, const WallHeights* wallHeights3D)
{
	if (wall == WallLocation::Middle3D && wallHeights3D != nullptr)
	{
		m_fakeAnchorTopPlane.Z = wallHeights3D->TopZ;
		m_fakeAnchorTopPlane.PrevZ = wallHeights3D->PrevTopZ;
		return m_fakeAnchorTopPlane;
	}
	// Lower walls anchored to floor
	else if (wall == WallLocation::Lower)
	{
		return otherSector.Floor;
	}
	else if (wall == WallLocation::Middle && side->linedef->sidedef[1] != nullptr)
	{
		if (UnpeggedLower(side))
			return sideSector.Floor.Z > otherSector.Floor.Z ? sideSector.Floor : otherSector.Floor;
		return sideSector.Ceiling.Z < otherSector.Ceiling.Z ? sideSector.Ceiling : otherSector.Ceiling;
	}

	// Everything else is anchored to ceiling
	return sideSector.Ceiling;
}

GeometryRenderer::RenderPlane GeometryRenderer::CalculateAnchorPlane(side_t* side, WallLocation wall, const RS& sideSector, const RS& otherSector, Sector3D* anchorSector3D)
{
	// Rules for what z to anchor drawing to based on location (lower, middle, upper, middle 3D) and unpeg flags
	if (wall == WallLocation::Lower)
	{
		if (UnpeggedLower(side))
			return otherSector.Ceiling;
		return otherSector.Floor;
	}
	else if (wall == WallLocation::Upper)
	{
		if ((side->linedef->flags & ML_DONTPEGTOP) != 0)
			return sideSector.Ceiling;
		return otherSector.Ceiling;
	}
	else if (wall == WallLocation::Middle3D && anchorSector3D != nullptr)
	{
		return anchorSector3D->ControlTop;
	}
	else if (wall == WallLocation::Middle && anchorSector3D == nullptr && side->linedef->sidedef[1] != nullptr)
	{
		if (UnpeggedLower(side))
			return sideSector.Floor.Z > otherSector.Floor.Z ? sideSector.Floor : otherSector.Floor;
		return sideSector.Ceiling.Z < otherSector.Ceiling.Z ? sideSector.Ceiling : otherSector.Ceiling;
	}

	return UnpeggedLower(side) ? sideSector.Floor : sideSector.Ceiling;
}

void GeometryRenderer::SetWallOffset(side_t* side, float saveOffsetY, const RenderPlane& top, double anchorZ, double prevAnchorZ, double addOffsetZ, double prevAddOffsetZ)
{
	// Use scrolling data for offset since normal wall offsets can't interpolate.
	// UZDoom: the current scroll is the Y offset of the sentinel tiers (Helion m_fakeWall is shared by all three walls),
	// the previous one is kept in m_wallScrollPrevY and applied to the vertices (PrevV) after the slice.
	m_wallScrollY = saveOffsetY + (float)(anchorZ - top.Z + addOffsetZ);
	m_wallScrollPrevY = saveOffsetY + (float)(prevAnchorZ - top.PrevZ + prevAddOffsetZ);
	for (int i = 0; i < 3; i++)
		side->textures[i].yOffset = m_wallScrollY;
}

void GeometryRenderer::AddVertices(std::vector<DynamicVertex>& vertices, std::span<DynamicVertex> add)
{
	if (add.empty())
		return;
	vertices.insert(vertices.end(), add.begin(), add.end());
}

bool GeometryRenderer::SetSectorsForSlice3D(const RenderWallSliceArgs& args, RS& facing)
{
	facing = m_fakeFacing;

	if (args.WallSector.Ceiling.Z <= args.WallSector.Floor.Z)
		return false;

	double renderTopZ = args.WallSector.Ceiling.Z;
	double renderBottomZ = args.WallSector.Floor.Z;
	double renderTopPrevZ = args.WallSector.Ceiling.PrevZ;
	double renderBottomPrevZ = args.WallSector.Floor.PrevZ;

	double topZ = args.FacingSector.Ceiling.Z;
	double bottomZ = args.FacingSector.Floor.Z;
	double topPrevZ = args.FacingSector.Ceiling.PrevZ;
	double bottomPrevZ = args.FacingSector.Floor.PrevZ;

	if (renderBottomZ >= topZ)
		return false;

	if (renderTopZ < bottomZ)
		return false;

	if (renderTopZ > topZ)
	{
		m_fakeFacing.Ceiling.Z = topZ;
		m_fakeFacing.Ceiling.PrevZ = topPrevZ;
	}
	else
	{
		m_fakeFacing.Ceiling.Z = renderTopZ;
		m_fakeFacing.Ceiling.PrevZ = renderTopPrevZ;
	}

	if (renderBottomZ > bottomZ)
	{
		m_fakeFacing.Floor.Z = renderBottomZ;
		m_fakeFacing.Floor.PrevZ = renderBottomPrevZ;
	}
	else
	{
		m_fakeFacing.Floor.Z = bottomZ;
		m_fakeFacing.Floor.PrevZ = bottomPrevZ;
	}

	facing = m_fakeFacing;
	return m_fakeFacing.Ceiling.Z > m_fakeFacing.Floor.Z || m_fakeFacing.Ceiling.PrevZ > m_fakeFacing.Floor.PrevZ;
}

RenderWallSliceResult GeometryRenderer::RenderSectorSlice3D(const RenderWallSliceArgs& args)
{
	RS facing;
	if (!SetSectorsForSlice3D(args, facing))
		return RenderWallSliceResult::Empty3D();

	DynamicVertexArray* sideVertices = nullptr;
	SkyVertexArray* skyVertices = nullptr;
	GLLegacyTexture* texture = nullptr;
	RenderOneSided(args.Side, args.IsFrontSide, sideVertices, skyVertices, texture,
		&facing, &args.LightSector, nullptr, args.RenderSkySide,
		args.AllowAlpha, args.Style, GeometryType::Middle3D);
	return RenderWallSliceResult(sideVertices, skyVertices, texture);
}

RenderWallSliceResult GeometryRenderer::RenderOneSidedSlice(const RenderWallSliceArgs& args)
{
	DynamicVertexArray* sideVertices = nullptr;
	SkyVertexArray* skyVertices = nullptr;
	GLLegacyTexture* texture = nullptr;
	RenderOneSided(args.Side, args.IsFrontSide, sideVertices, skyVertices, texture,
		&args.WallSector, &args.LightSector, nullptr, args.RenderSkySide, args.AllowAlpha);
	return RenderWallSliceResult(sideVertices, skyVertices, texture);
}

bool GeometryRenderer::SetSectorsForTwoSidedLowerSlice(const RenderWallSliceArgs& args, RS& facing, RS& other)
{
	if (args.WallSector.Ceiling.Z <= args.WallSector.Floor.Z)
		return false;

	double renderTopZ = args.WallSector.Ceiling.Z;
	double renderBottomZ = args.WallSector.Floor.Z;
	double renderTopPrevZ = args.WallSector.Ceiling.PrevZ;
	double renderBottomPrevZ = args.WallSector.Floor.PrevZ;

	double lowerTopZ = args.OtherSector.Floor.Z;
	double lowerBottomZ = args.FacingSector.Floor.Z;
	double lowerTopPrevZ = args.OtherSector.Floor.PrevZ;
	double lowerBottomPrevZ = args.FacingSector.Floor.PrevZ;

	m_fakeFacing.FloorSkyTextureHandle = args.FacingSector.FloorSkyTextureHandle;
	m_fakeFacing.Floor.TextureHandle = args.FacingSector.Floor.TextureHandle;
	m_fakeOther.FloorSkyTextureHandle = args.OtherSector.FloorSkyTextureHandle;
	m_fakeOther.Floor.TextureHandle = args.OtherSector.Floor.TextureHandle;

	if (renderBottomZ > lowerBottomZ)
	{
		m_fakeFacing.Floor.Z = renderBottomZ;
		m_fakeFacing.Ceiling.Z = renderBottomZ;
		m_fakeFacing.Floor.PrevZ = renderBottomPrevZ;
		m_fakeFacing.Ceiling.PrevZ = renderBottomPrevZ;
	}
	else
	{
		m_fakeFacing.Floor.Z = lowerBottomZ;
		m_fakeFacing.Ceiling.Z = lowerBottomZ;
		m_fakeFacing.Floor.PrevZ = lowerBottomPrevZ;
		m_fakeFacing.Ceiling.Z = lowerBottomPrevZ;
	}

	if (renderTopZ < lowerTopZ)
	{
		m_fakeOther.Floor.Z = renderTopZ;
		m_fakeOther.Ceiling.Z = renderTopZ;
		m_fakeOther.Floor.PrevZ = renderTopPrevZ;
		m_fakeOther.Ceiling.PrevZ = renderTopPrevZ;
	}
	else
	{
		m_fakeOther.Floor.Z = lowerTopZ;
		m_fakeOther.Ceiling.Z = lowerTopZ;
		m_fakeOther.Floor.PrevZ = lowerTopPrevZ;
		m_fakeOther.Ceiling.PrevZ = lowerTopPrevZ;
	}

	facing = m_fakeFacing;
	other = m_fakeOther;
	return m_fakeFacing.Ceiling.Z <= lowerTopZ || m_fakeOther.Ceiling.Z <= lowerBottomZ;
}

RenderWallSliceResult GeometryRenderer::RenderTwoSidedLowerSlice(const RenderWallSliceArgs& args)
{
	RS facing;
	RS other;
	if (!SetSectorsForTwoSidedLowerSlice(args, facing, other))
		return RenderWallSliceResult::EmptyNoAddOffset();

	DynamicVertexArray* sideVertices = nullptr;
	SkyVertexArray* skyVertices = nullptr;
	SkyVertexArray* skyVertices2 = nullptr;
	RenderTwoSidedLower(args.Side, args.OtherSide, facing, other, args.IsFrontSide, sideVertices, skyVertices, skyVertices2,
		&args.LightSector, args.RenderSkySide);
	return RenderWallSliceResult(sideVertices, skyVertices, nullptr, skyVertices2);
}

bool GeometryRenderer::SetSectorsForTwoSidedUpperSlice(const RenderWallSliceArgs& args, RS& facing, RS& other)
{
	if (args.WallSector.Ceiling.Z <= args.WallSector.Floor.Z)
		return false;

	double renderTopZ = args.WallSector.Ceiling.Z;
	double renderBottomZ = args.WallSector.Floor.Z;
	double renderTopPrevZ = args.WallSector.Ceiling.PrevZ;
	double renderBottomPrevZ = args.WallSector.Floor.PrevZ;

	double upperTopZ = args.FacingSector.Ceiling.Z;
	double upperBottomZ = args.OtherSector.Ceiling.Z;
	double upperTopPrevZ = args.FacingSector.Ceiling.PrevZ;
	double upperBottomPrevZ = args.OtherSector.Ceiling.PrevZ;

	m_fakeFacing.CeilingSkyTextureHandle = args.FacingSector.CeilingSkyTextureHandle;
	m_fakeFacing.Ceiling.TextureHandle = args.FacingSector.Ceiling.TextureHandle;
	m_fakeOther.CeilingSkyTextureHandle = args.OtherSector.CeilingSkyTextureHandle;
	m_fakeOther.Ceiling.TextureHandle = args.OtherSector.Ceiling.TextureHandle;
	// UZDoom: the flood fill of the slice (TwoSidedUpperFloodRenderSkySide) looks the plane's sector light up; the fake planes keep
	// the identity (sector, face, light) of the planes they stand for, only their heights change.
	m_fakeFacing.Floor.Sector = args.FacingSector.Floor.Sector;
	m_fakeFacing.Floor.Facing = args.FacingSector.Floor.Facing;
	m_fakeFacing.Floor.LightLevelAdd = args.FacingSector.Floor.LightLevelAdd;
	m_fakeFacing.Ceiling.Sector = args.FacingSector.Ceiling.Sector;
	m_fakeFacing.Ceiling.Facing = args.FacingSector.Ceiling.Facing;
	m_fakeFacing.Ceiling.LightLevelAdd = args.FacingSector.Ceiling.LightLevelAdd;
	m_fakeOther.Floor.Sector = args.OtherSector.Floor.Sector;
	m_fakeOther.Floor.Facing = args.OtherSector.Floor.Facing;
	m_fakeOther.Floor.LightLevelAdd = args.OtherSector.Floor.LightLevelAdd;
	m_fakeOther.Ceiling.Sector = args.OtherSector.Ceiling.Sector;
	m_fakeOther.Ceiling.Facing = args.OtherSector.Ceiling.Facing;
	m_fakeOther.Ceiling.LightLevelAdd = args.OtherSector.Ceiling.LightLevelAdd;

	if (renderTopZ < upperTopZ)
	{
		m_fakeFacing.Floor.Z = renderTopZ;
		m_fakeFacing.Ceiling.Z = renderTopZ;
		m_fakeFacing.Floor.PrevZ = renderTopPrevZ;
		m_fakeFacing.Ceiling.PrevZ = renderTopPrevZ;
	}
	else
	{
		m_fakeFacing.Floor.Z = upperTopZ;
		m_fakeFacing.Ceiling.Z = upperTopZ;
		m_fakeFacing.Floor.PrevZ = upperTopPrevZ;
		m_fakeFacing.Ceiling.PrevZ = upperTopPrevZ;
	}

	if (renderBottomZ > upperBottomZ)
	{
		m_fakeOther.Floor.Z = renderBottomZ;
		m_fakeOther.Ceiling.Z = renderBottomZ;
		m_fakeOther.Floor.PrevZ = renderBottomPrevZ;
		m_fakeOther.Ceiling.PrevZ = renderBottomPrevZ;
	}
	else
	{
		m_fakeOther.Floor.Z = upperBottomZ;
		m_fakeOther.Ceiling.Z = upperBottomZ;
		m_fakeOther.Floor.PrevZ = upperBottomPrevZ;
		m_fakeOther.Ceiling.PrevZ = upperBottomPrevZ;
	}

	facing = m_fakeFacing;
	other = m_fakeOther;
	return m_fakeFacing.Ceiling.Z >= upperTopZ || m_fakeOther.Ceiling.Z >= upperBottomZ;
}

RenderWallSliceResult GeometryRenderer::RenderTwoSidedUpperSlice(const RenderWallSliceArgs& args)
{
	RS facing;
	RS other;
	if (!SetSectorsForTwoSidedUpperSlice(args, facing, other))
		return RenderWallSliceResult::EmptyNoAddOffset();

	DynamicVertexArray* sideVertices = nullptr;
	SkyVertexArray* skyVertices = nullptr;
	SkyVertexArray* skyVertices2 = nullptr;
	RenderTwoSidedUpper(args.Side, args.OtherSide, facing, other, args.IsFrontSide, sideVertices, skyVertices, skyVertices2,
		&args.LightSector, args.RenderSkySide);
	return RenderWallSliceResult(sideVertices, skyVertices, nullptr, skyVertices2);
}

bool GeometryRenderer::SetSectorsForTwoMiddleSlice(const RenderWallSliceArgs& args, RS& facing, RS& other, double& bottomZ)
{
	bottomZ = 0;

	FTextureID middleTexture = args.Side->textures[side_t::mid].texture;
	if (!middleTexture.isValid())
		return false;

	FGameTexture* texture = TexMan.GetGameTexture(middleTexture, true);
	if (texture == nullptr || !texture->isValid() || args.OtherSide == nullptr)
		return false;

	// Helion texture.Image.Dimension.
	Helion::Geometry::Dimension dimension((int)floorf(texture->GetDisplayWidth() + 0.5f), (int)floorf(texture->GetDisplayHeight() + 0.5f));

	// UZDoom: the sentinel's middle tier Y offset is Helion's current scroll.
	double saveScroll = args.Side->textures[side_t::mid].yOffset;
	args.Side->textures[side_t::mid].yOffset = 0;

	double startOffsetY = 0.0;
	if (args.Count == 0)
	{
		startOffsetY = args.StartOffsetY;
		m_midTexSpan = std::make_unique<MidTexSpan>(GetMidTexSpan(dimension, args.Side, args.OtherSide, args.FacingSector, args.OtherSector, startOffsetY, startOffsetY));
	}

	if (m_midTexSpan == nullptr)
	{
		args.Side->textures[side_t::mid].yOffset = saveScroll;
		return false;
	}

	bottomZ = m_midTexSpan->BottomZ;

	double renderTopZ = args.WallSector.Ceiling.Z;
	double renderTopPrevZ = args.WallSector.Ceiling.PrevZ;
	double renderBottomZ = args.WallSector.Floor.Z;
	double renderBottomPrevZ = args.WallSector.Floor.PrevZ;

	if (UnpeggedLower(args.Side))
	{
		renderBottomZ += startOffsetY;
		renderBottomPrevZ += startOffsetY;
	}
	else
	{
		renderTopZ += startOffsetY;
		renderTopPrevZ += startOffsetY;
	}

	args.Side->textures[side_t::mid].yOffset = saveScroll;

	if (renderTopZ > m_midTexSpan->TopZ && renderBottomZ > m_midTexSpan->TopZ)
		return false;
	if (renderBottomZ < m_midTexSpan->TopZ && renderTopZ < m_midTexSpan->BottomZ)
		return false;

	startOffsetY = std::max(startOffsetY, 0.0);
	if (renderTopZ < m_midTexSpan->TopZ)
	{
		m_fakeFacing.Ceiling.Z = renderTopZ;
		m_fakeOther.Ceiling.Z = renderTopZ;
		m_fakeFacing.Ceiling.PrevZ = renderTopPrevZ;
		m_fakeOther.Ceiling.PrevZ = renderTopPrevZ;
	}
	else
	{
		m_fakeFacing.Ceiling.Z = m_midTexSpan->TopZ + startOffsetY;
		m_fakeOther.Ceiling.Z = m_midTexSpan->TopZ + startOffsetY;
		m_fakeFacing.Ceiling.PrevZ = m_midTexSpan->PrevTopZ + startOffsetY;
		m_fakeOther.Ceiling.PrevZ = m_midTexSpan->PrevTopZ + startOffsetY;
	}

	if (renderBottomZ > m_midTexSpan->BottomZ)
	{
		m_fakeFacing.Floor.Z = renderBottomZ;
		m_fakeOther.Floor.Z = renderBottomZ;
		m_fakeFacing.Floor.PrevZ = renderBottomPrevZ;
		m_fakeOther.Floor.PrevZ = renderBottomPrevZ;
	}
	else
	{
		m_fakeFacing.Floor.Z = m_midTexSpan->BottomZ;
		m_fakeOther.Floor.Z = m_midTexSpan->BottomZ;
		m_fakeFacing.Floor.PrevZ = m_midTexSpan->PrevBottomZ;
		m_fakeOther.Floor.PrevZ = m_midTexSpan->PrevBottomZ;
	}

	facing = m_fakeFacing;
	other = m_fakeOther;
	return true;
}

RenderWallSliceResult GeometryRenderer::RenderTwoSidedMiddleSlice(const RenderWallSliceArgs& args)
{
	// This is a mess because of how RenderTwoSidedMiddle currently functions
	RS facing;
	RS other;
	double bottomZ;
	if (!SetSectorsForTwoMiddleSlice(args, facing, other, bottomZ))
		return UnpeggedLower(args.Side) ? RenderWallSliceResult::EmptyNoAddOffset() : RenderWallSliceResult::Empty3D();

	// UZDoom: Helion's Middle.Offset.Y and ScrollData.Y both live in the sentinel's middle Y offset.
	// Helion's m_fakeWall.Offset.Y is 0, so Middle.Offset.Y is 0 or the unpegged delta; ScrollData is dropped for Count == 0.
	double saveOffset = args.Side->textures[side_t::mid].yOffset;
	double middleOffsetY = 0;

	if (UnpeggedLower(args.Side))
		middleOffsetY = bottomZ - facing.Floor.Z;

	args.Side->textures[side_t::mid].yOffset = args.Count == 0 ? middleOffsetY : saveOffset + middleOffsetY;

	DynamicVertexArray* sideVertices = nullptr;
	MidTexSpan restrictSpan(facing.Floor.Z, facing.Ceiling.Z, facing.Floor.PrevZ, facing.Ceiling.PrevZ);
	RenderTwoSidedMiddle(args.Side, args.OtherSide, facing, other, args.IsFrontSide, sideVertices,
		&args.LightSector, &restrictSpan);

	args.Side->textures[side_t::mid].yOffset = saveOffset;

	return RenderWallSliceResult(sideVertices, nullptr, nullptr, nullptr, !UnpeggedLower(args.Side));
}

void GeometryRenderer::SetSectorToSlice(RS& wallSector, const RenderPlane& top, const RenderPlane& bottom, const WallHeights* wallHeights3D)
{
	wallSector.Ceiling.Z = top.Z;
	wallSector.Ceiling.PrevZ = top.PrevZ;
	wallSector.Floor.Z = bottom.Z;
	wallSector.Floor.PrevZ = bottom.PrevZ;

	// Clip to calculated min/max heights of 3D sectors
	if (wallHeights3D != nullptr)
	{
		wallSector.Ceiling.Z = std::min(wallSector.Ceiling.Z, wallHeights3D->TopZ);
		wallSector.Floor.Z = std::max(wallSector.Floor.Z, wallHeights3D->BottomZ);
		wallSector.Ceiling.PrevZ = std::min(wallSector.Ceiling.PrevZ, wallHeights3D->PrevTopZ);
		wallSector.Floor.PrevZ = std::max(wallSector.Floor.PrevZ, wallHeights3D->PrevBottomZ);
	}

	if (wallSector.Floor.Z > wallSector.Ceiling.Z)
		wallSector.Floor.Z = wallSector.Ceiling.Z;
	if (wallSector.Floor.PrevZ > wallSector.Ceiling.PrevZ)
		wallSector.Floor.PrevZ = wallSector.Ceiling.PrevZ;
}

void GeometryRenderer::SetSectorToSlice(RS& wallSector, const WallHeights& heights)
{
	wallSector.Ceiling.Z = heights.TopZ;
	wallSector.Ceiling.PrevZ = heights.PrevTopZ;
	wallSector.Floor.Z = heights.BottomZ;
	wallSector.Floor.PrevZ = heights.PrevBottomZ;

	if (wallSector.Floor.Z > wallSector.Ceiling.Z)
		wallSector.Floor.Z = wallSector.Ceiling.Z;
}

void GeometryRenderer::SetSectorToSlice(RS& wallSector, const WallHeights& prevHeights, const WallHeights& heights)
{
	wallSector.Ceiling.Z = prevHeights.BottomZ;
	wallSector.Ceiling.PrevZ = prevHeights.PrevBottomZ;
	wallSector.Floor.Z = heights.TopZ;
	wallSector.Floor.PrevZ = heights.PrevTopZ;

	if (wallSector.Floor.Z > wallSector.Ceiling.Z)
		wallSector.Floor.Z = wallSector.Ceiling.Z;
}

}
