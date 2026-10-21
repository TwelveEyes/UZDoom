/*
** WorldTriangulator.cpp
**
** Port of Helion.Render.OpenGL.Shared.World.WorldTriangulator.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Side.Offset + Side.<Part>.Offset collapse into one value in UZDoom
** (side_t::GetTextureXOffset(part)), and so does the scale. Helion's
** SideScrollData has no counterpart: UZDoom scrollers (DScroller) write the
** texture offsets directly, so the scroll terms of the Helion formulas are
** already part of the offsets and the previous-frame value of a scroll equals
** the current one (D12 interpolation of scrollers is handled by the geometry
** renderer, not here).
*/

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include "Common/Shared/World/WorldTriangulator.h"
#include "RenderBlock.h"
#include "World/WorldStatic.h"
#include "World/Static/RenderStaticState.h"
#include "earcut.hpp"
#include "g_levellocals.h"

namespace Helion::Render::OpenGL::Shared::World
{

using namespace Helion::Geometry;
using ::Helion::World::WorldStatic;
using ::Helion::World::Geometry::Lines::MidTexSpan;
using ::Helion::World::Geometry::Sectors::SectorPlane;
using ::Helion::World::Geometry::Sectors::SectorPlaneFace;
using ::Helion::World::Geometry::Sectors::SectorPlanes;
using ::Helion::World::Geometry::Sectors::FlatTransformMethod;
using Common::Shared::World::MidTexOpening;

namespace
{

constexpr int Upper = side_t::top;
constexpr int Middle = side_t::mid;
constexpr int Lower = side_t::bottom;

bool WrapMidTex(side_t* side)
{
	return (side->Flags & WALLF_WRAP_MIDTEX) || (side->linedef->flags & ML_WRAP_MIDTEX);
}

float OffsetX(side_t* side, int part) { return (float)side->GetTextureXOffset(part); }
float OffsetY(side_t* side, int part) { return (float)side->GetTextureYOffset(part); }
float ScaleX(side_t* side, int part) { return (float)side->GetTextureXScale(part); }
float ScaleY(side_t* side, int part) { return (float)side->GetTextureYScale(part); }

void SetWall(WallVertices& wall, const Vec2F& left, const Vec2F& right, double topZ, double prevTopZ,
	double bottomZ, double prevBottomZ, const WallUV& uv, const WallUV& prevUV)
{
	wall.TopLeft.X = left.X;
	wall.TopLeft.Y = left.Y;
	wall.TopLeft.Z = (float)topZ;
	wall.TopLeft.PrevZ = (float)prevTopZ;
	wall.TopLeft.U = uv.TopLeft.X;
	wall.TopLeft.V = uv.TopLeft.Y;
	wall.TopLeft.PrevU = prevUV.TopLeft.X;
	wall.TopLeft.PrevV = prevUV.TopLeft.Y;

	wall.BottomRight.X = right.X;
	wall.BottomRight.Y = right.Y;
	wall.BottomRight.Z = (float)bottomZ;
	wall.BottomRight.PrevZ = (float)prevBottomZ;
	wall.BottomRight.U = uv.BottomRight.X;
	wall.BottomRight.V = uv.BottomRight.Y;
	wall.BottomRight.PrevU = prevUV.BottomRight.X;
	wall.BottomRight.PrevV = prevUV.BottomRight.Y;

	wall.PrevTopZ = (float)prevTopZ;
	wall.PrevBottomZ = (float)prevBottomZ;
}

}

void WorldTriangulator::HandleOneSided(side_t* side, side_t* offsetSide, const SectorPlane& floor, const SectorPlane& ceiling,
	const Vec2F& textureUVInverse, WallVertices& wall, double overrideFloor, double overrideCeiling, bool isFront, bool calculateUV)
{
	line_t* line = side->linedef;
	Vec2F left, right;
	GetLeftRightVertices(isFront, line, left, right);

	double topZ, bottomZ, prevTopZ, prevBottomZ;
	if (overrideCeiling == NoOverride)
	{
		topZ = ceiling.Z + WorldStatic::LineVertexGapTopZ;
		prevTopZ = ceiling.PrevZ + WorldStatic::LineVertexGapTopZ;
	}
	else
	{
		topZ = overrideCeiling;
		prevTopZ = overrideCeiling;
	}

	if (overrideFloor == NoOverride)
	{
		bottomZ = floor.Z - WorldStatic::LineVertexGapBottomZ;
		prevBottomZ = floor.PrevZ - WorldStatic::LineVertexGapBottomZ;
	}
	else
	{
		bottomZ = overrideFloor;
		prevBottomZ = overrideFloor;
	}

	double length = line->Delta().Length();
	double spanZ = topZ - bottomZ;
	double prevSpanZ = prevTopZ - prevBottomZ;
	WallUV uv;
	WallUV prevUV;

	if (calculateUV)
	{
		uv = CalculateOneSidedWallUV(line, side, offsetSide, length, textureUVInverse, spanZ, false);
		prevUV = CalculateOneSidedWallUV(line, side, offsetSide, length, textureUVInverse, prevSpanZ, true);
	}

	SetWall(wall, left, right, topZ, prevTopZ, bottomZ, prevBottomZ, uv, prevUV);
}

void WorldTriangulator::HandleTwoSidedLower(side_t* facingSide, const SectorPlane& topFlat, const SectorPlane& bottomFlat,
	const Vec2F& textureUVInverse, bool isFrontSide, WallVertices& wall, bool calculateUV)
{
	line_t* line = facingSide->linedef;
	Vec2F left, right;
	GetLeftRightVertices(isFrontSide, line, left, right);

	double topZ = topFlat.Z + WorldStatic::LineVertexGapTopZ;
	double bottomZ = bottomFlat.Z - WorldStatic::LineVertexGapBottomZ;
	double prevTopZ = topFlat.PrevZ + WorldStatic::LineVertexGapTopZ;
	double prevBottomZ = bottomFlat.PrevZ - WorldStatic::LineVertexGapBottomZ;

	double length = line->Delta().Length();
	WallUV uv, prevUV;
	if (calculateUV)
	{
		uv = CalculateTwoSidedLowerWallUV(line, facingSide, length, textureUVInverse, topZ, bottomZ, false);
		prevUV = CalculateTwoSidedLowerWallUV(line, facingSide, length, textureUVInverse, prevTopZ, prevBottomZ, true);
	}

	SetWall(wall, left, right, topZ, prevTopZ, bottomZ, prevBottomZ, uv, prevUV);
}

void WorldTriangulator::HandleTwoSidedMiddle(side_t* facingSide,
	const Dimension& textureDimension, const Vec2F& textureUVInverse, const MidTexOpening& opening, const MidTexOpening& prevOpening,
	bool isFrontSide, WallVertices& wall, bool& nothingVisible, double offset, double prevOffset,
	SectorPlanes clipPlanes, bool vertexGap, const MidTexSpan* restrictSpan)
{
	if (::Helion::Render::RenderBlock::IsBlocked(facingSide->linedef, isFrontSide))
	{
		nothingVisible = true;
		return;
	}

	line_t* line = facingSide->linedef;
	// Helion adds the scroll Y offset here. UZDoom scrollers already modify the texture offset that
	// CalculateMiddleDrawSpan reads, so there is nothing to add.

	MiddleDrawSpan drawSpan = CalculateMiddleDrawSpan(line, facingSide, opening, prevOpening, textureDimension, offset, prevOffset, clipPlanes, vertexGap, restrictSpan);
	if (drawSpan.NotVisible())
	{
		nothingVisible = true;
		return;
	}

	Vec2F left, right;
	GetLeftRightVertices(isFrontSide, line, left, right);

	double length = line->Delta().Length();
	WallUV uv = CalculateTwoSidedMiddleWallUV(facingSide, length, drawSpan.TopZ, drawSpan.BottomZ,
		drawSpan.VisibleTopZ, drawSpan.VisibleBottomZ, textureUVInverse, false);
	WallUV prevUV = CalculateTwoSidedMiddleWallUV(facingSide, length, drawSpan.PrevTopZ, drawSpan.PrevBottomZ,
		drawSpan.PrevVisibleTopZ, drawSpan.PrevVisibleBottomZ, textureUVInverse, true);

	SetWall(wall, left, right, drawSpan.VisibleTopZ, drawSpan.PrevVisibleTopZ, drawSpan.VisibleBottomZ, drawSpan.PrevVisibleBottomZ, uv, prevUV);
	nothingVisible = false;
}

void WorldTriangulator::HandleTwoSidedUpper(side_t* facingSide, const SectorPlane& topPlane, const SectorPlane& bottomPlane,
	const Vec2F& textureUVInverse, bool isFrontSide, WallVertices& wall, double overrideTopZ, bool calculateUV)
{
	line_t* line = facingSide->linedef;
	Vec2F left, right;
	GetLeftRightVertices(isFrontSide, line, left, right);

	double topZ, prevTopZ;
	if (overrideTopZ == NoOverride)
	{
		topZ = topPlane.Z + WorldStatic::LineVertexGapTopZ;
		prevTopZ = topPlane.PrevZ + WorldStatic::LineVertexGapTopZ;
	}
	else
	{
		topZ = overrideTopZ;
		prevTopZ = overrideTopZ;
	}

	double bottomZ = bottomPlane.Z - WorldStatic::LineVertexGapBottomZ;
	double prevBottomZ = bottomPlane.PrevZ - WorldStatic::LineVertexGapBottomZ;

	double length = line->Delta().Length();
	double spanZ = topZ - bottomZ;
	double prevSpanZ = prevTopZ - prevBottomZ;
	WallUV uv;
	WallUV prevUV;

	if (calculateUV)
	{
		uv = CalculateTwoSidedUpperWallUV(line, facingSide, length, textureUVInverse, spanZ, false);
		prevUV = CalculateTwoSidedUpperWallUV(line, facingSide, length, textureUVInverse, prevSpanZ, true);
	}

	SetWall(wall, left, right, topZ, prevTopZ, bottomZ, prevBottomZ, uv, prevUV);
}

void WorldTriangulator::HandleSubsector(subsector_t* subsector, const SectorPlane& sectorPlane, bool floor,
	const Vec2F& textureVector, TArray<TriangulatedWorldVertex>& verticesToPopulate, double overrideZ)
{
	assert(subsector->numlines >= 3 && "Cannot render subsector when it's degenerate (should have 3+ edges)");

	int index = 0;
	int length = (int)subsector->numlines;
	verticesToPopulate.Resize(subsector->numlines);

	// A flat plane has one Z. For slopes UZDoom has no per-plane Z, so each vertex gets the height
	// of the plane at its position; the previous-frame Z keeps the same delta Helion's PrevZ has.
	bool overridden = overrideZ != INT32_MAX;
	bool slope = !overridden && sectorPlane.Sector != nullptr && sectorPlane.Plane().isSlope();
	double z = overridden ? overrideZ : sectorPlane.Z;
	double prevZ = overridden ? overrideZ : sectorPlane.PrevZ;
	double prevZDelta = prevZ - z;

	Vec2D uv(0, 0);
	Vec2D prevUV(0, 0);
	const Vec2D& offset = sectorPlane.RenderOffsets.Offset;
	const Vec2D& lastOffset = sectorPlane.RenderOffsets.LastOffset;
	const double rotate = sectorPlane.RenderOffsets.Rotate;
	const Vec2D& scale = sectorPlane.RenderOffsets.Scale;

	int edgeIndex = index;
	int count = length - index;
	int add = 1;

	if (floor)
	{
		edgeIndex = length - 1;
		add = -1;
	}

	TriangulatedWorldVertex* worldVertex = verticesToPopulate.Data();
	for (int i = 0; i < count; i++)
	{
		vertex_t* v = subsector->firstline[edgeIndex].v1;
		const Vec2D vertex = ::Helion::World::WorldStatic::FlatVertexPosition(v);
		if (rotate == 0)
		{
			uv.X = vertex.X / textureVector.X;
			uv.Y = -(vertex.Y / textureVector.Y);
			prevUV = uv;

			uv.X += offset.X / textureVector.X;
			uv.Y += offset.Y / textureVector.Y;
			prevUV.X += lastOffset.X / textureVector.X;
			prevUV.Y += lastOffset.Y / textureVector.Y;
		}
		else
		{
			Vec2D uvVertex = vertex;
			if (sectorPlane.FlatTransformMethod == FlatTransformMethod::OffsetThenRotate)
			{
				uvVertex.X += offset.X;
				uvVertex.Y -= offset.Y;
				uvVertex = Rotate(uvVertex, rotate);
			}
			else
			{
				uvVertex = Rotate(uvVertex, rotate);
				uvVertex.X += offset.X;
				uvVertex.Y -= offset.Y;
			}
			uv.X = uvVertex.X / textureVector.X;
			uv.Y = -(uvVertex.Y / textureVector.Y);

			Vec2D prevUVVertex = vertex;
			if (sectorPlane.FlatTransformMethod == FlatTransformMethod::OffsetThenRotate)
			{
				prevUVVertex.X += lastOffset.X;
				prevUVVertex.Y -= lastOffset.Y;
				prevUVVertex = Rotate(prevUVVertex, rotate);
			}
			else
			{
				prevUVVertex = Rotate(prevUVVertex, rotate);
				prevUVVertex.X += lastOffset.X;
				prevUVVertex.Y -= lastOffset.Y;
			}
			// Helion defect: the C# assigns prevUV from uvVertex here, so the previous-frame UV is
			// identical to the current one for rotated flats. Evident intent: use prevUVVertex.
			prevUV.X = prevUVVertex.X / textureVector.X;
			prevUV.Y = -(prevUVVertex.Y / textureVector.Y);
		}

		if (scale.X != 0)
		{
			uv.X *= scale.X;
			prevUV.X *= scale.X;
		}

		if (scale.Y != 0)
		{
			uv.Y *= scale.Y;
			prevUV.Y *= scale.Y;
		}

		double vertexZ = slope ? sectorPlane.Plane().ZatPoint(vertex) : z;

		worldVertex->X = (float)vertex.X;
		worldVertex->Y = (float)vertex.Y;
		worldVertex->Z = (float)vertexZ;
		worldVertex->PrevZ = (float)(vertexZ + prevZDelta);
		worldVertex->U = (float)uv.X;
		worldVertex->V = (float)uv.Y;
		worldVertex->PrevU = (float)prevUV.X;
		worldVertex->PrevV = (float)prevUV.Y;
		worldVertex++;
		edgeIndex += add;
	}
}

// Triangulation of CreateVerticesForSubsector() in hw_vertexbuilder.cpp. The polygon is numbered in
// seg order (i = 0..numlines-1); HandleSubsector emits it in that order for ceilings and reversed for
// floors, so 'pos' maps a seg-order number to the position of the vertex in the emitted array, and
// floors flip every triangle to keep the plane facing the right way.
void WorldTriangulator::HandleSubsectorIndices(subsector_t* subsector, bool floor, uint32_t firstVertex, TArray<uint32_t>& indices)
{
	const unsigned n = subsector->numlines;
	if (n < 3)
		return;

	auto pos = [&](unsigned i) { return firstVertex + (floor ? n - 1 - i : i); };
	auto addTriangle = [&](unsigned a, unsigned b, unsigned c)
	{
		indices.Push(pos(a));
		if (floor)
		{
			indices.Push(pos(c));
			indices.Push(pos(b));
		}
		else
		{
			indices.Push(pos(b));
			indices.Push(pos(c));
		}
	};

	if ((subsector->flags & SSECF_HOLE) && n > 3)
	{
		// Hole filling "subsectors" are not necessarily convex so they require real triangulation.
		// These things are extremely rare so performance is secondary here.
		using Point = std::pair<double, double>;
		std::vector<std::vector<Point>> polygon(1);
		std::vector<Point>& curPoly = polygon.back();
		curPoly.resize(n);

		for (unsigned i = 0; i < n; i++)
		{
			DVector2 pos = ::Helion::World::WorldStatic::FlatVertexPosition(subsector->firstline[i].v1);
			curPoly[i] = { pos.X, pos.Y };
		}

		auto triangles = mapbox::earcut(polygon);
		for (size_t i = 0; i + 2 < triangles.size(); i += 3)
			addTriangle(triangles[i], triangles[i + 1], triangles[i + 2]);
	}
	else
	{
		for (unsigned k = 2; k < n; k++)
			addTriangle(0, k - 1, k);
	}
}

bool WorldTriangulator::IncludeSubsector(const subsector_t* subsector, bool skipFlood)
{
	if (subsector->numlines < 3)
		return false;
	return !skipFlood || !::Helion::World::Static::RenderStaticState::SubsectorFlood(subsector);
}

void WorldTriangulator::HandleSection(FSection* section, const SectorPlane& sectorPlane, bool floor, const Vec2F& textureVector,
	TArray<TriangulatedWorldVertex>& verticesToPopulate, TArray<uint32_t>& indices, double overrideZ, bool skipFlood)
{
	verticesToPopulate.Clear();
	indices.Clear();

	TArray<TriangulatedWorldVertex> subsectorVertices;
	for (subsector_t* subsector : section->subsectors)
	{
		if (!IncludeSubsector(subsector, skipFlood))
			continue;

		uint32_t firstVertex = verticesToPopulate.Size();
		HandleSubsector(subsector, sectorPlane, floor, textureVector, subsectorVertices, overrideZ);
		for (unsigned i = 0; i < subsectorVertices.Size(); i++)
			verticesToPopulate.Push(subsectorVertices[i]);
		HandleSubsectorIndices(subsector, floor, firstVertex, indices);
	}
}

MiddleDrawSpan WorldTriangulator::CalculateMiddleDrawSpan(line_t* line, side_t* facingSide, const MidTexOpening& opening, const MidTexOpening& prevOpening,
	const Dimension& textureDimension, double offset, double prevOffset, SectorPlanes clipPlanes, bool vertexGap, const MidTexSpan* restrictSpan)
{
	if (WrapMidTex(facingSide))
		return MiddleDrawSpan(opening.BottomZ, opening.TopZ, opening.BottomZ, opening.TopZ, prevOpening.BottomZ, prevOpening.TopZ, prevOpening.BottomZ, prevOpening.TopZ);

	double textureHeight = textureDimension.Height / std::fabs(ScaleY(facingSide, Middle));
	// Default rendering top down. Unpegged.Lower renders bottom up
	// TopZ is the top of the texture to render and BottomZ is the bottom
	// MaxTopZ and MinBottomZ are the min/max areas to render with Y offset. (e.g. a middle texture can render over a missing lower texture)
	double topZ = opening.TopZ;
	double bottomZ = topZ - textureHeight;
	double prevTopZ = prevOpening.TopZ;
	double prevBottomZ = prevTopZ - textureHeight;

	if (line->flags & ML_DONTPEGBOTTOM)
	{
		bottomZ = opening.BottomZ;
		topZ = bottomZ + textureHeight;
		prevBottomZ = prevOpening.BottomZ;
		prevTopZ = prevBottomZ + textureHeight;
	}

	double offsetY = OffsetY(facingSide, Middle) / ScaleY(facingSide, Middle);
	topZ += offsetY + offset;
	bottomZ += offsetY + offset;
	prevTopZ += offsetY + prevOffset;
	prevBottomZ += offsetY + prevOffset;

	// Check clipping to min/max floor/ceiling. Typically ignored for skies or mid-texture hacks.
	double visibleTopZ = (clipPlanes & SectorPlanes::Ceiling) == SectorPlanes::None ? topZ : std::min(topZ, opening.MaxTopZ);
	double visiblePrevTopZ = (clipPlanes & SectorPlanes::Ceiling) == SectorPlanes::None ? prevTopZ : std::min(prevTopZ, prevOpening.MaxTopZ);

	double visibleBottomZ = (clipPlanes & SectorPlanes::Floor) == SectorPlanes::None ? bottomZ : std::max(bottomZ, opening.MinBottomZ);
	double visiblePrevBottomZ = (clipPlanes & SectorPlanes::Floor) == SectorPlanes::None ? prevBottomZ : std::max(prevBottomZ, prevOpening.MinBottomZ);

	if (restrictSpan != nullptr)
	{
		if (visibleTopZ > restrictSpan->TopZ)
			visibleTopZ = restrictSpan->TopZ;
		if (visiblePrevTopZ > restrictSpan->PrevTopZ)
			visiblePrevTopZ = restrictSpan->PrevTopZ;
		if (visibleBottomZ < restrictSpan->BottomZ)
			visibleBottomZ = restrictSpan->BottomZ;
		if (visiblePrevBottomZ < restrictSpan->PrevBottomZ)
			visiblePrevBottomZ = restrictSpan->PrevBottomZ;
	}

	if (vertexGap)
	{
		return MiddleDrawSpan(bottomZ - WorldStatic::LineVertexGapBottomZ, topZ + WorldStatic::LineVertexGapTopZ, visibleBottomZ - WorldStatic::LineVertexGapBottomZ, visibleTopZ + WorldStatic::LineVertexGapTopZ,
			prevBottomZ - WorldStatic::LineVertexGapBottomZ, prevTopZ + WorldStatic::LineVertexGapTopZ, visiblePrevBottomZ - WorldStatic::LineVertexGapBottomZ, visiblePrevTopZ + WorldStatic::LineVertexGapTopZ);
	}

	return MiddleDrawSpan(bottomZ, topZ, visibleBottomZ, visibleTopZ,
		prevBottomZ, prevTopZ, visiblePrevBottomZ, visiblePrevTopZ);
}

WallUV WorldTriangulator::CalculateOneSidedWallUV(line_t* line, side_t* side, side_t* offsetSide, double length,
	const Vec2F& textureUVInverse, double spanZ, bool previous)
{
	float scaleX = ScaleX(side, Middle);
	float scaleY = ScaleY(side, Middle);
	float absScaleX = std::fabs(scaleX);
	float absScaleY = std::fabs(scaleY);
	float offsetU = OffsetX(offsetSide, Middle) * textureUVInverse.X / absScaleX + (WorldStatic::LineVertexOffset * textureUVInverse.X);
	float offsetV = OffsetY(offsetSide, Middle) * textureUVInverse.Y / absScaleY + (WorldStatic::LineVertexOffset * textureUVInverse.Y);

	float wallSpanU = (float)length * textureUVInverse.X;
	float spanV = (float)spanZ * textureUVInverse.Y;

	float leftU = offsetU;
	float rightU = offsetU + wallSpanU;
	float topV;
	float bottomV;

	if (line->flags & ML_DONTPEGBOTTOM)
	{
		bottomV = (1.0f / absScaleY) + offsetV;
		topV = bottomV - spanV;
	}
	else
	{
		topV = offsetV;
		bottomV = offsetV + spanV;
	}

	return WallUV(Vec2F(leftU * scaleX, topV * scaleY), Vec2F(rightU * scaleX, bottomV * scaleY));
}

WallUV WorldTriangulator::CalculateTwoSidedLowerWallUV(line_t* line, side_t* side, double length,
	const Vec2F& textureUVInverse, double topZ, double bottomZ, bool previous)
{
	float scaleX = ScaleX(side, Lower);
	float scaleY = ScaleY(side, Lower);
	float absScaleX = std::fabs(scaleX);
	float absScaleY = std::fabs(scaleY);
	float offsetU = OffsetX(side, Lower) * textureUVInverse.X / absScaleX + (WorldStatic::LineVertexOffset * textureUVInverse.X);
	float offsetV = OffsetY(side, Lower) * textureUVInverse.Y / absScaleY + (WorldStatic::LineVertexOffset * textureUVInverse.Y);

	float wallSpanU = (float)length * textureUVInverse.X;
	float leftU = offsetU;
	float rightU = offsetU + wallSpanU;
	float topV;
	float bottomV;

	if (line->flags & ML_DONTPEGBOTTOM)
	{
		// Helion reads side.Sector.Ceiling.PrevZ when 'previous'; sector_t has no previous plane
		// height, the previous top/bottom the caller passes in already carry the interpolation.
		double ceilZ = side->sector->GetPlaneTexZ(sector_t::ceiling);
		float topDistFromCeil = (float)(ceilZ - topZ);
		float bottomDistFromCeil = (float)(ceilZ - bottomZ);

		topV = offsetV + (topDistFromCeil * textureUVInverse.Y);
		bottomV = offsetV + (bottomDistFromCeil * textureUVInverse.Y);
	}
	else
	{
		topV = offsetV;
		bottomV = offsetV + (float)(topZ - bottomZ) * textureUVInverse.Y;
	}

	return WallUV(Vec2F(leftU * scaleX, topV * scaleY), Vec2F(rightU * scaleX, bottomV * scaleY));
}

WallUV WorldTriangulator::CalculateTwoSidedMiddleWallUV(side_t* side, double length, double topZ, double bottomZ,
	double visibleTopZ, double visibleBottomZ, const Vec2F& textureUVInverse, bool previous)
{
	if (WrapMidTex(side))
		return CalculateOneSidedWallUV(side->linedef, side, side, length, textureUVInverse, visibleTopZ - visibleBottomZ, previous);

	float scaleX = ScaleX(side, Middle);
	float absScaleX = std::fabs(scaleX);
	float offsetU = OffsetX(side, Middle) * textureUVInverse.X / absScaleX + (WorldStatic::LineVertexOffset * textureUVInverse.X);

	float wallSpanU = (float)length * textureUVInverse.X;
	float leftU = offsetU;
	float rightU = offsetU + wallSpanU;

	// Since we only draw one of the texture, all we need to do is find
	// out where the texture is clamped by and find that value between
	// [0.0, 1.0]. For example if a texture height of 10 only has two
	// pixels available between 6 -> 7 for the line opening, then
	// the top V would be 0.6 and the bottom V would be 0.7.
	double textureHeight = topZ - bottomZ;
	float topV = 1.0f - (float)((visibleTopZ - bottomZ) / textureHeight);
	float bottomV = 1.0f - (float)((visibleBottomZ - bottomZ) / textureHeight);

	if (ScaleY(side, Middle) < 0)
	{
		topV = 1.0f - topV;
		bottomV = 1.0f - bottomV;
	}

	return WallUV(Vec2F(leftU * scaleX, topV), Vec2F(rightU * scaleX, bottomV));
}

WallUV WorldTriangulator::CalculateTwoSidedUpperWallUV(line_t* line, side_t* side, double length,
	const Vec2F& textureUVInverse, double spanZ, bool previous)
{
	float scaleX = ScaleX(side, Upper);
	float scaleY = ScaleY(side, Upper);
	float absScaleX = std::fabs(scaleX);
	float absScaleY = std::fabs(scaleY);
	float offsetU = OffsetX(side, Upper) * textureUVInverse.X / absScaleX + (WorldStatic::LineVertexOffset * textureUVInverse.X);
	float offsetV = OffsetY(side, Upper) * textureUVInverse.Y / absScaleY + (WorldStatic::LineVertexOffset * textureUVInverse.Y);

	float wallSpanU = (float)length * textureUVInverse.X;
	float spanV = (float)spanZ * textureUVInverse.Y;

	float leftU = offsetU;
	float rightU = offsetU + wallSpanU;
	float topV;
	float bottomV;

	if (line->flags & ML_DONTPEGTOP)
	{
		topV = offsetV;
		bottomV = topV + spanV;
	}
	else
	{
		bottomV = 1.0f + offsetV;
		topV = bottomV - spanV;
	}

	return WallUV(Vec2F(leftU * scaleX, topV * scaleY), Vec2F(rightU * scaleX, bottomV * scaleY));
}

void WorldTriangulator::GetLeftRightVertices(bool isFront, line_t* line, Vec2F& left, Vec2F& right)
{
	// line.RenderSegStart/End are the line's v1/v2.
	if (isFront)
	{
		left = Vec2F((float)line->v1->fX(), (float)line->v1->fY());
		right = Vec2F((float)line->v2->fX(), (float)line->v2->fY());
	}
	else
	{
		left = Vec2F((float)line->v2->fX(), (float)line->v2->fY());
		right = Vec2F((float)line->v1->fX(), (float)line->v1->fY());
	}
}

}
