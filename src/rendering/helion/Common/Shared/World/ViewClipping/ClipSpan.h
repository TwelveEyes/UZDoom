/*
** ClipSpan.h
**
** Port of Helion.Render.OpenGL.Shared.World.ViewClipping.ClipSpan.
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

#include <cassert>
#include <cstdint>

namespace Helion::Render::OpenGL::Shared::World::ViewClipping
{

/// <summary>
/// Represents a range of start and end diamond angles.
/// </summary>
struct ClipSpan
{
	/// <summary>
	/// The starting angle.
	/// </summary>
	uint32_t StartAngle = 0;

	/// <summary>
	/// The ending angle.
	/// </summary>
	uint32_t EndAngle = 0;

	ClipSpan() = default;

	/// <summary>
	/// Creates a new span of angles.
	/// </summary>
	/// <param name="startAngle">The starting angle, must be less than or
	/// equal to the end angle.</param>
	/// <param name="endAngle">The ending angle, must be less than or equal
	/// to the start angle.</param>
	ClipSpan(uint32_t startAngle, uint32_t endAngle) : StartAngle(startAngle), EndAngle(endAngle)
	{
		assert(startAngle <= endAngle && "Cannot have the end angle be less than the start angle");
	}

	/// <summary>
	/// Checks if the angle provided is inclusively contained in the range.
	/// </summary>
	/// <param name="angle">The angle to check.</param>
	/// <returns>True if it is, false if not.</returns>
	bool Contains(uint32_t angle) const { return angle >= StartAngle && angle <= EndAngle; }

	/// <summary>
	/// Checks if the range formed by the first and second angle are in the
	/// clip span. The order of the arguments does not matter, the second
	/// argument can be smaller than the first.
	/// </summary>
	/// <param name="firstAngle">The first angle.</param>
	/// <param name="secondAngle">The second angle.</param>
	/// <returns>True if both are in this span, false otherwise.</returns>
	bool Contains(uint32_t firstAngle, uint32_t secondAngle) const { return Contains(firstAngle) && Contains(secondAngle); }
};

}
