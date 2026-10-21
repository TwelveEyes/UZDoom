/*
** ViewClipper.cpp
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

#include <algorithm>
#include <cassert>
#include "Common/Shared/World/ViewClipping/ViewClipper.h"

namespace Helion::Render::OpenGL::Shared::World::ViewClipping
{

using namespace Helion::Geometry;

uint32_t ViewClipper::ToDiamondAngle(double startX, double startY, double endX, double endY)
{
	// The code below takes some position and finds the vector from the
	// center to the position.
	//
	// It then is able to take the X and Y components of this vector,
	// and turn them into some ratio between [0.0, 4.0) as follows for
	// this table:
	//
	//      X,  Y    Result
	//     -----------------
	//      1,  0     0.0
	//      0,  1     1.0
	//     -1,  0     2.0
	//      0, -1     3.0
	//
	// As such, we can then multiply it by a big number to turn it into
	// a value between [0, 2^32). The key here is that we get an order
	// out of the values, because this allows us to see what angles are
	// blocked or not by mapping every position onto a unit circle with
	// 2^32 precision.
	double posX = endX - startX;
	double posY = endY - startY;
	if (posX == 0 && posY == 0)
		return 0;

	// TODO: Can we fuse two if statements into one statement somehow?
	if (posY >= 0)
	{
		if (posX >= 0)
			return (uint32_t)(DiamondScale * (posY / (posX + posY)));
		return (uint32_t)(DiamondScale * (1 - (posX / (-posX + posY))));
	}

	if (posX < 0)
		return (uint32_t)(DiamondScale * (2 - (posY / (-posX - posY))));
	return (uint32_t)(DiamondScale * (3 + (posX / (posX - posY))));
}

uint32_t ViewClipper::DiamondAngleFromRadians(double radians)
{
	return (uint32_t)((int64_t)(radians * RadiansToDiamondAngleFactor) % (int64_t)UINT32_MAX);
}

void ViewClipper::AddLine(const Vec2D& first, const Vec2D& second)
{
	uint32_t smallerAngle = ToDiamondAngle(Center.X, Center.Y, first.X, first.Y);
	uint32_t largerAngle = ToDiamondAngle(Center.X, Center.Y, second.X, second.Y);

	if (largerAngle < smallerAngle)
		std::swap(smallerAngle, largerAngle);

	if (AnglesSpanOriginVector(smallerAngle, largerAngle))
	{
		AddRange(0, smallerAngle);
		AddRange(largerAngle, UINT32_MAX);
	}
	else
		AddRange(smallerAngle, largerAngle);
}

void ViewClipper::AddLine(uint32_t smallerAngle, uint32_t largerAngle)
{
	if (AnglesSpanOriginVector(smallerAngle, largerAngle))
	{
		AddRange(0, smallerAngle);
		AddRange(largerAngle, UINT32_MAX);
	}
	else
		AddRange(smallerAngle, largerAngle);
}

bool ViewClipper::InsideAnyRange(double x1, double y1, double x2, double y2) const
{
	if (m_spans.Size() == 0)
		return false;

	uint32_t smallerAngle = ToDiamondAngle(Center.X, Center.Y, x1, y1);
	uint32_t largerAngle = ToDiamondAngle(Center.X, Center.Y, x2, y2);

	if (largerAngle < smallerAngle)
		std::swap(smallerAngle, largerAngle);

	if (AnglesSpanOriginVector(smallerAngle, largerAngle))
		return InRange(0, smallerAngle) && InRange(largerAngle, UINT32_MAX);
	return InRange(smallerAngle, largerAngle);
}

bool ViewClipper::InsideAnyRange(uint32_t smallerAngle, uint32_t largerAngle) const
{
	if (AnglesSpanOriginVector(smallerAngle, largerAngle))
		return InRange(0, smallerAngle) && InRange(largerAngle, UINT32_MAX);
	return InRange(smallerAngle, largerAngle);
}

std::pair<uint32_t, uint32_t> ViewClipper::GetAngles(const Vec2D& first, const Vec2D& second) const
{
	uint32_t smallerAngle = ToDiamondAngle(Center.X, Center.Y, first.X, first.Y);
	uint32_t largerAngle = ToDiamondAngle(Center.X, Center.Y, second.X, second.Y);

	if (largerAngle < smallerAngle)
		std::swap(smallerAngle, largerAngle);

	return { smallerAngle, largerAngle };
}

/// <summary>
/// Checks if the arc from the chord crosses the origin vector [1, 0].
/// </summary>
/// <param name="smallerAngle">The starting angle.</param>
/// <param name="largerAngle">the ending angle.</param>
/// <returns>If the chord from the start and end angle crosses the
/// initial vector angle of zero degrees.</returns>
bool ViewClipper::AnglesSpanOriginVector(uint32_t smallerAngle, uint32_t largerAngle)
{
	assert(smallerAngle <= largerAngle && "Smaller angle should be smaller than the larger angle");

	// In this case, the only place the end point could be is in the
	// range of (start, MAX_ANGLE), since if it was beyond the maximum
	// angle then it'd be less than the smaller angle (which means this
	// wouldn't even be an issue). Also this lets us avoid doing any
	// unchecked calculations and avoid overflow for the next part.
	if (smallerAngle >= PiAngle)
		return false;

	// A proof can be done to demonstrate that the only time it is
	// shorter to go right is when the smaller angle is less than 180
	// degrees, and adding 180 to it is less than the larger angle.
	//
	// The abridged version is that if the gap between the start and
	// the end is larger than 180, then instead of going CCW around
	// the circle, you'd go CW since it's the shortest distance for
	// a chord formed by the two endpoints. This means if you are less
	// starting at the top half of the circle, you must go right, and
	// pass through the origin vector (aka: <1, 0>).
	return smallerAngle + PiAngle < largerAngle;
}

/// <summary>
/// Checks if the start/end angles are contained in any interval.
/// </summary>
/// <param name="startAngle">The starting angle.</param>
/// <param name="endAngle">the ending angle.</param>
/// <returns>True if so, false otherwise.</returns>
bool ViewClipper::InRange(uint32_t startAngle, uint32_t endAngle) const
{
	// TODO: If endAngle > uint.MaxValue / 2, search backwards?

	for (unsigned i = 0; i < m_spans.Size(); i++)
	{
		const ClipSpan& span = m_spans[i];
		if (span.Contains(startAngle, endAngle))
			return true;

		if (endAngle < span.StartAngle)
			return false;
	}

	return false;
}

/// <summary>
/// Adds the range of endpoints inclusively, generating and fusing any
/// nodes that overlap with the range.
/// </summary>
/// <param name="startAngle">The starting angle.</param>
/// <param name="endAngle">The ending angle.</param>
void ViewClipper::AddRange(uint32_t startAngle, uint32_t endAngle)
{
	assert(startAngle <= endAngle && "Range must have the start angle being before the end angle");

	int startIndex = FindOrMakeStartNode(startAngle, endAngle);
	MergeUntil(startIndex, endAngle);
}

/// <summary>
/// Either finds the node that contains the start angle, creates a new
/// node that contains the range, or extends a range from a node after
/// the start angle safely backwards.
/// </summary>
/// <remarks>
/// Return of this function indicates that the start angle is added
/// successfully, and that a merge step should follow after.
/// </remarks>
/// <param name="startAngle">The starting angle.</param>
/// <param name="endAngle">The ending angle.</param>
/// <returns>The node that contains the start angle. This is either an
/// existing node that was expanded to hold the start angle, or a new
/// node that was allocated for it, or a node that already spanned the
/// start angle.</returns>
int ViewClipper::FindOrMakeStartNode(uint32_t startAngle, uint32_t endAngle)
{
	if (m_spans.Size() == 0)
	{
		m_spans.Push(ClipSpan(startAngle, endAngle));
		return 0;
	}

	int index = FindIndexJustAfterOrIncluding(startAngle);

	// If all the nodes end before the starting point, add a new one
	// onto the end.
	if (index == (int)m_spans.Size())
	{
		m_spans.Push(ClipSpan(startAngle, endAngle));
		return (int)m_spans.Size() - 1;
	}

	// startAngle falls inside this span - no insertion needed
	if (m_spans[index].Contains(startAngle))
		return index;

	// If we're in between a gap, we'll make a new node.
	if (m_spans[index].StartAngle > endAngle)
	{
		m_spans.Insert(index, ClipSpan(startAngle, endAngle));
		return index;
	}

	// We can extend the starting node backwards without worrying about
	// creating an overlap, since `startNode` would have been that node
	// instead if such a node existed.
	m_spans[index] = ClipSpan(startAngle, m_spans[index].EndAngle);
	return index;
}

/// <summary>
/// Finds the node with the clip span that either includes the start
/// angle or begins after the start angle (and before any others after
/// that).
/// </summary>
/// <param name="startAngle">The starting angle to find with.</param>
/// <returns>Either the node that includes or is after the start angle,
/// or null if there are no nodes that satisfy this criterion (implying
/// that the start angle is after the end of every span).</returns>
int ViewClipper::FindIndexJustAfterOrIncluding(uint32_t startAngle) const
{
	// Binary search
	int min = 0;
	int max = (int)m_spans.Size() - 1;
	while (min <= max)
	{
		int mid = (min + max) / 2;
		if (m_spans[mid].EndAngle < startAngle)
			min = mid + 1;
		else max = mid - 1;
	}
	return min;
}

/// <summary>
/// Starts at the node provided and goes forward until any nodes after
/// it are merged with the start node. This fuses all the ranges. Upon
/// completion of this function, `startNode` will contain the merged
/// ranges.
/// </summary>
/// <param name="startNode">The node we should start at and fuse with
/// everything afterwards that is before or including endAngle.</param>
/// <param name="endAngle">The ending angle of the span to add.</param>
void ViewClipper::MergeUntil(int startIndex, uint32_t endAngle)
{
	// If we start and end inside the same node, then we're done and
	// have no merging to do.
	if (endAngle <= m_spans[startIndex].EndAngle)
		return;

	uint32_t lastSeenEndAngle = m_spans[startIndex].EndAngle;
	int removeIndex = startIndex + 1;
	int removeCount = 0;

	for (int i = startIndex + 1; i < (int)m_spans.Size(); i++)
	{
		const ClipSpan& span = m_spans[i];

		// If the next node starts after our ending point, we're done.
		if (endAngle < span.StartAngle)
			break;

		lastSeenEndAngle = span.EndAngle;
		removeCount++;

		// We do this last because we need to make sure we unlink the
		// node as we will be extending the starting node onwards.
		if (span.Contains(endAngle))
			break;
	}

	if (removeCount > 0)
		m_spans.Delete(removeIndex, removeCount);

	uint32_t newEndAngle = std::max(endAngle, lastSeenEndAngle);
	m_spans[startIndex] = ClipSpan(m_spans[startIndex].StartAngle, newEndAngle);
}

}
