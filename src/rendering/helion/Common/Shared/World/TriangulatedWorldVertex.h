/*
** TriangulatedWorldVertex.h
**
** Port of Helion.Render.OpenGL.Shared.World.TriangulatedWorldVertex.
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

namespace Helion::Render::OpenGL::Shared::World
{

struct TriangulatedWorldVertex
{
	float X = 0;
	float Y = 0;
	float Z = 0;
	float U = 0;
	float V = 0;
	float PrevZ = 0;
	float PrevU = 0;
	float PrevV = 0;

	TriangulatedWorldVertex() = default;

	TriangulatedWorldVertex(float x, float y, float z, float prevZ, float u, float v, float prevU, float prevV) :
		X(x), Y(y), Z(z), U(u), V(v), PrevZ(prevZ), PrevU(prevU), PrevV(prevV)
	{
	}

	TriangulatedWorldVertex(double x, double y, double z, double prevZ, double u, double v, double prevU, double prevV) :
		TriangulatedWorldVertex((float)x, (float)y, (float)z, (float)prevZ, (float)u, (float)v, (float)prevU, (float)prevV)
	{
	}

	TriangulatedWorldVertex(const Geometry::Vec3F& position, float prevZ, const Geometry::Vec2F& uv, const Geometry::Vec2F& prevUV) :
		TriangulatedWorldVertex(position.X, position.Y, position.Z, prevZ, uv.X, uv.Y, prevUV.X, prevUV.Y)
	{
	}
};

}
