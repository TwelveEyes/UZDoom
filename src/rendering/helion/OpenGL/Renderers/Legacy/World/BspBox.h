/*
** BspBox.h
**
** UZDoom-only: Box2D helpers of Helion's Box2D (Contains, GetSpanningEdge) and the UZDoom BSP bounding boxes.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion's BspNodeCompact / BspSubsector carry Box2D bounding boxes; UZDoom's node_t::bbox[child] is a
** float[4] in BOXTOP/BOXBOTTOM/BOXLEFT/BOXRIGHT order and subsector_t::bbox is an FBoundingBox.
*/

#pragma once

#include "Util/Geometry.h"
#include "m_bbox.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::BspBox
{

inline ::Helion::Geometry::Box2D FromNodeBBox(const float* bbox)
{
	return ::Helion::Geometry::Box2D(::Helion::Geometry::Vec2D(bbox[BOXLEFT], bbox[BOXBOTTOM]), ::Helion::Geometry::Vec2D(bbox[BOXRIGHT], bbox[BOXTOP]));
}

inline ::Helion::Geometry::Box2D FromBoundingBox(const FBoundingBox& box)
{
	return ::Helion::Geometry::Box2D(::Helion::Geometry::Vec2D(box.Left(), box.Bottom()), ::Helion::Geometry::Vec2D(box.Right(), box.Top()));
}

// Box2D.Contains(Vec2D), made inclusive. Helion's is exclusive: a view position exactly on a box edge (the camera standing on a
// grid aligned line) then gets a spanning edge through the view position, which spans 180 degrees in the view clipper and culls
// the whole box (skytest MAP01: the sector on the other side of the line rendered black in BSP mode).
inline bool Contains(const ::Helion::Geometry::Box2D& box, const ::Helion::Geometry::Vec2D& point)
{
	return point.X >= box.Min.X && point.X <= box.Max.X && point.Y >= box.Min.Y && point.Y <= box.Max.Y;
}

// Box2D.GetSpanningEdge
inline void GetSpanningEdge(const ::Helion::Geometry::Box2D& box, const ::Helion::Geometry::Vec2D& position, double& x1, double& y1, double& x2, double& y2)
{
	// Cohen-Sutherland style bit code: 0bYYXX, each pair counts the box edges the position is beyond.
	unsigned horizontalBits = (position.X > box.Min.X ? 1u : 0u) + (position.X > box.Max.X ? 1u : 0u);
	unsigned verticalBits = (position.Y > box.Min.Y ? 1u : 0u) + (position.Y > box.Max.Y ? 1u : 0u);

	switch (horizontalBits | (verticalBits << 2))
	{
	case 0x1: // Bottom middle
		x1 = box.Min.X; y1 = box.Min.Y;
		x2 = box.Max.X; y2 = box.Min.Y;
		return;
	case 0x2: // Bottom right
		x1 = box.Min.X; y1 = box.Min.Y;
		x2 = box.Max.X; y2 = box.Max.Y;
		return;
	case 0x4: // Middle left
		x1 = box.Min.X; y1 = box.Max.Y;
		x2 = box.Min.X; y2 = box.Min.Y;
		return;
	case 0x6: // Middle right
		x1 = box.Max.X; y1 = box.Min.Y;
		x2 = box.Max.X; y2 = box.Max.Y;
		return;
	case 0x8: // Top left
		x1 = box.Max.X; y1 = box.Max.Y;
		x2 = box.Min.X; y2 = box.Min.Y;
		return;
	case 0x9: // Top middle
		x1 = box.Max.X; y1 = box.Max.Y;
		x2 = box.Min.X; y2 = box.Max.Y;
		return;
	case 0xA: // Top right
		x1 = box.Max.X; y1 = box.Min.Y;
		x2 = box.Min.X; y2 = box.Max.Y;
		return;
	case 0x0: // Bottom left
	case 0x5: // Center (should not occur)
	default:
		x1 = box.Min.X; y1 = box.Max.Y;
		x2 = box.Max.X; y2 = box.Min.Y;
		return;
	}
}

}
