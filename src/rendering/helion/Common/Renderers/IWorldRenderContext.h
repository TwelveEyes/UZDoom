/*
** IWorldRenderContext.h
**
** Port of Helion.Render.Common.Renderers.IWorldRenderContext.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** IWorld maps onto FLevelLocals*, T[] parameters onto std::span, Color?
** onto std::optional<Color> (Color is PalEntry).
*/

#pragma once

#include <optional>
#include <span>
#include "Util/Geometry.h"

struct FLevelLocals;

namespace Helion::Render::Common::Renderers
{

/// <summary>
/// Performs world drawing commands.
/// </summary>
class IWorldRenderContext
{
public:
	virtual ~IWorldRenderContext() = default;

	virtual void Draw(FLevelLocals* world) = 0;
	virtual void DrawAutomap(FLevelLocals* world) = 0;
	virtual void DrawLine(const Geometry::Seg3D& seg, Geometry::Color color) = 0;
	virtual void DrawLines(std::span<const Geometry::Seg3D> segs, Geometry::Color color) = 0;
	virtual void DrawRay(const Geometry::Ray3D& ray, Geometry::Color color) = 0;
	virtual void DrawRays(std::span<const Geometry::Ray3D> rays, Geometry::Color color) = 0;
	virtual void DrawTriangle(const Geometry::Triangle3D& triangle, Geometry::Color color) = 0;
	virtual void DrawTriangles(std::span<const Geometry::Triangle3D> triangles, Geometry::Color color) = 0;
	virtual void FillTriangle(const Geometry::Triangle3D& triangle, Geometry::Color color) = 0;
	virtual void FillTriangles(std::span<const Geometry::Triangle3D> triangles, Geometry::Color color) = 0;
	virtual void DrawQuad(const Geometry::Quad3D& quad, Geometry::Color color) = 0;
	virtual void DrawQuads(std::span<const Geometry::Quad3D> quads, Geometry::Color color) = 0;
	virtual void FillQuad(const Geometry::Quad3D& quad, Geometry::Color color) = 0;
	virtual void FillQuads(std::span<const Geometry::Quad3D> quads, Geometry::Color color) = 0;
	virtual void FillPlane(const Geometry::PlaneD& plane, Geometry::Color color) = 0;
	virtual void FillPlanes(std::span<const Geometry::PlaneD> planes, Geometry::Color color) = 0;
	virtual void DrawBox(const Geometry::Box3D& box, Geometry::Color color) = 0;
	virtual void DrawBoxes(std::span<const Geometry::Box3D> boxes, Geometry::Color color) = 0;
	virtual void FillBox(const Geometry::Box3D& box, Geometry::Color color) = 0;
	virtual void FillBoxes(std::span<const Geometry::Box3D> boxes, Geometry::Color color) = 0;
	virtual void DrawImage(const char* texture, const Geometry::Quad3D& quad, std::optional<Geometry::Color> color = std::nullopt) = 0;
	virtual void DrawSurface(const char* surfaceName, const Geometry::Quad3D& quad, std::optional<Geometry::Color> color = std::nullopt) = 0;
};

}
