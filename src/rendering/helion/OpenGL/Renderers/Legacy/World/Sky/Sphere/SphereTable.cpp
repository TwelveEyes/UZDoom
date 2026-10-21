/*
** SphereTable.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SphereTable.
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

#include <cassert>
#include <cmath>
#include "SphereTable.h"
#include "Util/MathHelper.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

SphereTable::SphereTable(int horizontalPoints, int verticalPoints) : m_horizontalPoints(horizontalPoints), m_verticalPoints(verticalPoints)
{
	assert(horizontalPoints >= 2 && "Insufficient amount of horizontal points for sphere angle table");
	assert(verticalPoints >= 2 && "Insufficient amount of vertical points for sphere angle table");

	std::vector<float> sinYaw;
	std::vector<float> cosYaw;
	std::vector<float> sinPitch;
	std::vector<float> cosPitch;

	float inverseHorizontal = (float)Util::MathHelper::TwoPi / horizontalPoints;
	for (int i = 0; i <= horizontalPoints; i++)
	{
		sinYaw.push_back((float)std::sin(i * inverseHorizontal));
		cosYaw.push_back((float)std::cos(i * inverseHorizontal));
	}

	// The vertical axis goes from [0, pi] as per spherical coordinates
	// require, but due to how we write coordinates, it should start at
	// the bottom. Since iteration begins at 0 and goes to Pi, then the
	// system becomes cos(0) -> cos(pi) which is 1 -> -1... opposite
	// direction of what we want. We'll just do Pi - angle instead to
	// flip this.
	float inverseVertical = (float)Util::MathHelper::Pi / verticalPoints;
	for (int i = 0; i <= verticalPoints; i++)
	{
		sinPitch.push_back((float)std::sin(Util::MathHelper::Pi - (i * inverseVertical)));
		cosPitch.push_back((float)std::cos(Util::MathHelper::Pi - (i * inverseVertical)));
	}

	m_mercatorRectangle.resize((size_t)(verticalPoints + 1) * (size_t)(horizontalPoints + 1));

	float verticalPointsInverse = 1.0f / verticalPoints;
	float horizontalPointsInverse = 1.0f / horizontalPoints;
	for (int row = 0; row <= verticalPoints; row++)
	{
		for (int col = 0; col <= horizontalPoints; col++)
		{
			float x = cosYaw[col] * sinPitch[row];
			float y = cosPitch[row];
			float z = sinYaw[col] * sinPitch[row];

			// We do `1 - v` below because we upload the image upside
			// down (due to OpenGL's image system being cartesian).
			float u = col * horizontalPointsInverse;
			float v = 1.0f - (row * verticalPointsInverse);

			m_mercatorRectangle[(size_t)row * (size_t)(horizontalPoints + 1) + (size_t)col] = SkySphereVertex(x, y, z, u, v);
		}
	}
}

}
