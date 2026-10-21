/*
** Geometry.h
**
** Helion.Geometry types mapped onto UZDoom's vector types, plus the small
** box/segment/ray/triangle/quad/plane value types that UZDoom has no
** equivalent for.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Vec2D/Vec3D/Vec2F/Vec3F/Vec2I map onto TVector2/TVector3, the GlmSharp
** mat4 maps onto VSMatrix (column-major, OpenGL conventions, same memory
** layout as GLM so that mvp[0..15] indexing is identical).
*/

#pragma once

#include <cmath>
#include "vectors.h"
#include "matrix.h"
#include "palentry.h"

namespace Helion::Geometry
{

using Vec2D = DVector2;
using Vec3D = DVector3;
using Vec2F = FVector2;
using Vec3F = FVector3;
using Vec2I = TVector2<int>;
using mat4 = VSMatrix;
using Color = PalEntry;

// Helion Vec3F.UnitSphere
inline Vec3F UnitSphere(float angle, float pitch)
{
	float sinAngle = sinf(angle);
	float cosAngle = cosf(angle);
	float sinPitch = sinf(pitch);
	float cosPitch = cosf(pitch);
	return Vec3F(cosAngle * cosPitch, sinAngle * cosPitch, sinPitch);
}

// Helion Vec2D.Rotate(radians)
inline Vec2D Rotate(const Vec2D& v, double radians)
{
	double sin = std::sin(radians);
	double cos = std::cos(radians);
	return Vec2D((v.X * cos) - (v.Y * sin), (v.X * sin) + (v.Y * cos));
}

struct Dimension
{
	int Width = 0;
	int Height = 0;

	constexpr Dimension() = default;
	constexpr Dimension(int width, int height) : Width(width), Height(height) { }
	constexpr bool operator==(const Dimension& o) const = default;
};

template<typename V>
struct Box2
{
	V Min;
	V Max;

	constexpr Box2() = default;
	constexpr Box2(const V& min, const V& max) : Min(min), Max(max) { }
	constexpr V TopLeft() const { return V(Min.X, Max.Y); }
	constexpr V BottomLeft() const { return Min; }
	constexpr V BottomRight() const { return V(Max.X, Min.Y); }
	constexpr V TopRight() const { return Max; }
	constexpr auto Top() const { return Max.Y; }
	constexpr auto Bottom() const { return Min.Y; }
	constexpr auto Left() const { return Min.X; }
	constexpr auto Right() const { return Max.X; }
	constexpr auto Width() const { return Max.X - Min.X; }
	constexpr auto Height() const { return Max.Y - Min.Y; }
};

using Box2D = Box2<Vec2D>;
using Box2F = Box2<Vec2F>;
using Box2I = Box2<Vec2I>;

template<typename V>
struct Box3
{
	V Min;
	V Max;

	constexpr Box3() = default;
	constexpr Box3(const V& min, const V& max) : Min(min), Max(max) { }
};

using Box3D = Box3<Vec3D>;

struct Seg3D
{
	Vec3D Start;
	Vec3D End;
};

struct Ray3D
{
	Vec3D Origin;
	Vec3D Direction;
};

struct Triangle3D
{
	Vec3D First;
	Vec3D Second;
	Vec3D Third;
};

struct Quad3D
{
	Vec3D TopLeft;
	Vec3D TopRight;
	Vec3D BottomLeft;
	Vec3D BottomRight;
};

// ax + by + cz + d = 0
struct PlaneD
{
	double A = 0;
	double B = 0;
	double C = 0;
	double D = 0;
};

}
