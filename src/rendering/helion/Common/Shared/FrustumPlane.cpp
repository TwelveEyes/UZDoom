/*
** FrustumPlane.cpp
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

#include "Common/Shared/FrustumPlane.h"

namespace Helion::Render::Common::Shared
{

using namespace Helion::Geometry;

void FrustumPlane::Normalize()
{
	double length = Normal.Length();
	Normal.X /= length;
	Normal.Y /= length;
	Normal.Z /= length;
	Distance /= length;
}

bool FrustumPlanes::BoxInFront(const Box2D& box) const
{
	return BoxInPlane(Near, box.Min, box.Max);
}

bool FrustumPlanes::PointInFrustum(double x, double y) const
{
	return PointInPlane(Left, x, y) && PointInPlane(Right, x, y);
}

bool FrustumPlanes::BoxInPlane(const FrustumPlane& plane, const Vec2D& min, const Vec2D& max)
{
	if (PointInPlane(plane, min.X, min.Y))
		return true;

	if (PointInPlane(plane, max.X, max.Y))
		return true;

	if (PointInPlane(plane, min.X, max.Y))
		return true;

	if (PointInPlane(plane, max.X, min.Y))
		return true;

	return false;
}

bool FrustumPlanes::SphereInFrustum(const Vec3D& pos, double radius) const
{
	return SphereInPlane(Left, pos, radius) && SphereInPlane(Right, pos, radius) &&
		SphereInPlane(Top, pos, radius) && SphereInPlane(Bottom, pos, radius);
}

bool FrustumPlanes::SphereInPlane(const FrustumPlane& plane, const Vec3D& pos, double radius)
{
	return plane.Normal.X * pos.X +
		plane.Normal.Y * pos.Y +
		plane.Normal.Z * pos.Z +
		plane.Distance > -radius;
}

bool FrustumPlanes::PointInPlane(const FrustumPlane& plane, double x, double y)
{
	double dist = (plane.Normal.X * x) + (plane.Normal.Y * y) + plane.Distance;
	return dist >= 0;
}

}
