/*
** SphereTable.h
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

#pragma once

#include <vector>
#include "SkySphereVertex.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

/// <summary>
/// A collection of all the spherical coordinates which map onto a mercator
/// projection that can be used in texturing a sphere and finding point
/// locations.
/// </summary>
class SphereTable
{
public:
	/// <summary>
	/// Creates a new table from the number of points (+ 1) provided. See
	/// <see cref="MercatorRectangle"/> remarks for more details.
	/// </summary>
	/// <param name="horizontalPoints">The number of points along a sphere
	/// on the horizontal plane.</param>
	/// <param name="verticalPoints">The number of points along a sphere on
	/// the vertical plane.</param>
	SphereTable(int horizontalPoints, int verticalPoints);

	/// <summary>
	/// Access like [row, col], see MercatorRectangle(row, col).
	/// </summary>
	/// <remarks>
	/// <para>This will be 1 element larger than the constructor values on
	/// each dimension because it will simplify the iteration over the
	/// endpoints easier without having to worry about an end case. This
	/// means that anything in the column [0, X] is equal to the column
	/// [Len - 1, X].</para>
	/// <para>The UV coordinate system has [0.0, 0.0] being the bottom
	/// right and [1.0, 1.0] being the top left.</para>
	/// <para>The rectangle grows from the bottom upwards as if it was
	/// cartesian coordinate based.</para>
	/// <para>Finally, this uses OpenGL's Y-axis-is-up system, not the
	/// coordinate system everything else uses where Z is up.</para>
	/// </remarks>
	const SkySphereVertex& MercatorRectangle(int row, int col) const { return m_mercatorRectangle[(size_t)row * (size_t)(m_horizontalPoints + 1) + (size_t)col]; }

private:
	int m_horizontalPoints;
	int m_verticalPoints;
	// A row-major set of 3D vertices mapped onto a 2D grid.
	std::vector<SkySphereVertex> m_mercatorRectangle;
};

}
