/*
** ViewClipper.h
**
** Port of Helion.Render.OpenGL.Shared.World.ViewClipping.ViewClipper.
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

#include <span>
#include <utility>
#include "Common/Shared/World/ViewClipping/ClipSpan.h"
#include "Util/Geometry.h"
#include "tarray.h"

namespace Helion::Render::OpenGL::Shared::World::ViewClipping
{

// TODO: Implement this with some kind of discrete fused interval tree for O(lg n).

/// <summary>
/// A helper class that treats a 360 circle as an interval range. Allows
/// adding of ranges and querying whether some angle span is entire blocked
/// or not.
/// </summary>
/// <remarks>
/// The angles stored internally are 'diamond angles', which don't map on
/// exactly to real angles. The purpose of these angles is to allow for an
/// ordering of different sloped lines from some origin point. Before each
/// use of this class, it should be cleared, and have the center set to the
/// central view reference point.
/// </remarks>
class ViewClipper
{
public:
	ViewClipper() { m_spans.Grow(256); }

	/// <summary>
	/// The center point from which we will clip from.
	/// </summary>
	Geometry::Vec2D Center = Geometry::Vec2D(0, 0);

	std::span<const ClipSpan> Elements() const { return std::span<const ClipSpan>(m_spans.Data(), m_spans.Size()); }

	/// <summary>
	/// Takes two positions and finds the diamond angle that exists from
	/// start to end. This is also known as the vector angle, but the
	/// calculations find it with respect to being a diamond. The diamond
	/// angle is an ordered angle that is similar to degrees or radians,
	/// and has absolute ordering.
	/// </summary>
	/// <remarks>
	/// https://stackoverflow.com/questions/1427422/cheap-algorithm-to-find-measure-of-angle-between-vectors
	/// is where the optimization was learned from.
	/// </remarks>
	/// <param name="start">The origin.</param>
	/// <param name="end">The endpoint from the origin forming a vector.
	/// </param>
	/// <returns>The diamond angle for the vertex. This will be zero if the
	/// start and end vertices are the same.</returns>
	static uint32_t ToDiamondAngle(double startX, double startY, double endX, double endY);

	static uint32_t DiamondAngleFromRadians(double radians);

	/// <summary>
	/// Clears all the clip ranges.
	/// </summary>
	/// <remarks>
	/// Unless you know what you are doing, you should also set the
	/// <see cref="Center"/> variable to a new position.
	/// </remarks>
	void Clear() { m_spans.Clear(); }

	/// <summary>
	/// Adds two positions that will be converted into angles and then
	/// added to be a clipping range.
	/// </summary>
	/// <param name="first">The first vertex of a line segment.</param>
	/// <param name="second">The second vertex of a line segment.</param>
	void AddLine(const Geometry::Vec2D& first, const Geometry::Vec2D& second);
	void AddLine(uint32_t smallerAngle, uint32_t largerAngle);

	bool InsideAnyRange(const Geometry::Vec2D& start, const Geometry::Vec2D& end) const { return InsideAnyRange(start.X, start.Y, end.X, end.Y); }

	/// <summary>
	/// Checks if the two points provided are encased in any ranges.
	/// </summary>
	/// <param name="first">The first vertex of a line segment.</param>
	/// <param name="second">The second vertex of a line segment.</param>
	/// <returns>True if they are in a range, false if not.</returns>
	bool InsideAnyRange(double x1, double y1, double x2, double y2) const;
	bool InsideAnyRange(uint32_t smallerAngle, uint32_t largerAngle) const;

	std::pair<uint32_t, uint32_t> GetAngles(const Geometry::Vec2D& first, const Geometry::Vec2D& second) const;
	// The arc from the chord crosses the initial vector angle of zero degrees (smallerAngle <= largerAngle).
	static bool AnglesSpanOriginVector(uint32_t smallerAngle, uint32_t largerAngle);

private:
	static constexpr uint32_t DiamondScale = UINT32_MAX / 4;
	static constexpr uint32_t PiAngle = UINT32_MAX / 2;
	static constexpr double RadiansToDiamondAngleFactor = UINT32_MAX / (2.0 * 3.14159265358979323846);

	TArray<ClipSpan> m_spans;

	bool InRange(uint32_t startAngle, uint32_t endAngle) const;
	void AddRange(uint32_t startAngle, uint32_t endAngle);
	int FindOrMakeStartNode(uint32_t startAngle, uint32_t endAngle);
	int FindIndexJustAfterOrIncluding(uint32_t startAngle) const;
	void MergeUntil(int startIndex, uint32_t endAngle);
};

}
