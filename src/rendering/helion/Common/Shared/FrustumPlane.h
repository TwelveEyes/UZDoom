/*
** FrustumPlane.h
**
** Port of Helion.Render.Common.Shared.FrustumPlane/FrustumPlanes.
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

#pragma once

#include "Util/Geometry.h"

namespace Helion::Render::Common::Shared
{

struct FrustumPlane
{
	Geometry::Vec3D Normal;
	double Distance = 0;

	FrustumPlane() = default;
	FrustumPlane(const Geometry::Vec3D& normal, double distance) : Normal(normal), Distance(distance) { }

	void Normalize();
};

struct FrustumPlanes
{
	FrustumPlane Left;
	FrustumPlane Right;
	FrustumPlane Top;
	FrustumPlane Bottom;
	FrustumPlane Near;
	FrustumPlane Far;

	bool BoxInFront(const Geometry::Box2D& box) const;
	bool PointInFrustum(double x, double y) const;
	bool SphereInFrustum(const Geometry::Vec3D& pos, double radius) const;

private:
	static bool BoxInPlane(const FrustumPlane& plane, const Geometry::Vec2D& min, const Geometry::Vec2D& max);
	static bool SphereInPlane(const FrustumPlane& plane, const Geometry::Vec3D& pos, double radius);
	static bool PointInPlane(const FrustumPlane& plane, double x, double y);
};

}
