/*
** AtlasNode.cpp
**
** Port of Helion.Util.Atlas.AtlasNode.
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

#include "AtlasNode.h"

#include <algorithm>
#include <cassert>
#include "engineerrors.h"

namespace Helion::Util::Atlas
{

using namespace Helion::Geometry;

AtlasNode::AtlasNode(Dimension atlasDimension)
{
	Location = Box2I(Vec2I(0, 0), Vec2I(atlasDimension.Width, atlasDimension.Height));
	m_maxAvailableDimensions = atlasDimension;
}

AtlasNode::AtlasNode(Box2I location, AtlasNode* parent)
{
	Location = location;
	m_parent = parent;
	m_maxAvailableDimensions.Width = location.Width();
	m_maxAvailableDimensions.Height = location.Height();
}

AtlasNode* AtlasNode::RecursivelyAdd(Dimension dimension)
{
	if (m_occupied || dimension.Width > Location.Width() || dimension.Height > Location.Height())
		return nullptr;

	// This is a heuristic to quickly determine if we can place it. It
	// will lead to some holes in the map as it is not optimal, but in
	// practice this does a pretty nice job thus far. I am not opposed
	// to a better implementation getting in, however due to the simple
	// nature of this, it'd only be worth upgrading if truly needed.
	if (HasChildren())
	{
		AtlasNode* result = nullptr;
		ForEachChild([&](AtlasNode* child)
		{
			if (result != nullptr || child == nullptr || !child->CanPossiblyAdd(dimension))
				return;

			result = child->RecursivelyAdd(dimension);
		});
		if (result != nullptr)
			return result;

		// If we have children but cannot add them, that means there is
		// likely not enough space for them, so we exit early as part
		// of the heuristic.
		return nullptr;
	}

	bool fitsWidthExactly = (dimension.Width == Location.Width());
	bool fitsHeightExactly = (dimension.Height == Location.Height());

	if (fitsWidthExactly && fitsHeightExactly)
	{
		m_occupied = true;
		m_maxAvailableDimensions = Dimension(0, 0);
		if (m_parent != nullptr)
			m_parent->RecursivelyNotifySomeChildWasAdded();
		return this;
	}

	if (fitsWidthExactly)
		return AddAsWidthFit(dimension);
	if (fitsHeightExactly)
		return AddAsHeightFit(dimension);

	return AddToBottomLeftCornerRecursively(dimension);
}

void AtlasNode::Delete()
{
	assert(!HasChildren() && "Trying to delete a parent atlas node, we should only delete from leaves");
	assert(m_occupied && "Trying to delete an atlas node that has not been used");

	// TODO
	I_FatalError("AtlasNode.Delete() not implemented yet");
}

void AtlasNode::EmplaceExistingTree(AtlasNode& treeRoot)
{
	assert(!HasChildren() && "Trying to fuse a child atlas tree into to a non-empty node");
	assert(Location.BottomLeft().X == 0 && Location.BottomLeft().Y == 0 && "All the locations of the child nodes will be wrong");

	m_left = std::move(treeRoot.m_left);
	m_right = std::move(treeRoot.m_right);
	m_occupied = treeRoot.m_occupied;
	m_maxAvailableDimensions = treeRoot.m_maxAvailableDimensions;

	// The moved children must ascend through this node now, not the old root.
	if (m_left != nullptr)
		m_left->m_parent = this;
	if (m_right != nullptr)
		m_right->m_parent = this;
}

void AtlasNode::RecursivelyNotifySomeChildWasAdded()
{
	if (m_left == nullptr || m_right == nullptr)
	{
		assert(false && "Propagating child addition should never encounter null children");
		return;
	}

	// We have to do it this way because being occupied means the value
	// of the min dimensions for that will be zero, which will mess up
	// our tracking. We want the minimum values on each dimension which
	// are *not* occupied.

	if (m_left->m_occupied && m_right->m_occupied)
	{
		m_occupied = true;
		m_maxAvailableDimensions = Dimension(0, 0);
	}

	if (!m_left->m_occupied)
	{
		m_maxAvailableDimensions.Width = std::max(m_maxAvailableDimensions.Width, m_left->m_maxAvailableDimensions.Width);
		m_maxAvailableDimensions.Height = std::max(m_maxAvailableDimensions.Height, m_left->m_maxAvailableDimensions.Height);
	}

	if (!m_right->m_occupied)
	{
		m_maxAvailableDimensions.Width = std::max(m_maxAvailableDimensions.Width, m_right->m_maxAvailableDimensions.Width);
		m_maxAvailableDimensions.Height = std::max(m_maxAvailableDimensions.Height, m_right->m_maxAvailableDimensions.Height);
	}

	if (m_parent != nullptr)
		m_parent->RecursivelyNotifySomeChildWasAdded();
}

bool AtlasNode::CanPossiblyAdd(Dimension dimension) const
{
	return !m_occupied &&
		dimension.Width <= m_maxAvailableDimensions.Width &&
		dimension.Height <= m_maxAvailableDimensions.Height;
}

AtlasNode* AtlasNode::AddAsWidthFit(Dimension dimension)
{
	Box2I bottom(Location.Min, Location.Min + Vec2I(dimension.Width, dimension.Height));
	m_left.reset(new AtlasNode(bottom, this));

	Box2I top(bottom.TopLeft(), Location.TopRight());
	m_right.reset(new AtlasNode(top, this));

	return m_left->RecursivelyAdd(dimension);
}

AtlasNode* AtlasNode::AddAsHeightFit(Dimension dimension)
{
	Box2I left(Location.Min, Location.Min + Vec2I(dimension.Width, dimension.Height));
	m_left.reset(new AtlasNode(left, this));

	Box2I right(left.BottomRight(), Location.TopRight());
	m_right.reset(new AtlasNode(right, this));

	return m_left->RecursivelyAdd(dimension);
}

AtlasNode* AtlasNode::AddToBottomLeftCornerRecursively(Dimension dimension)
{
	Box2I first;
	Box2I second;

	// We want to split along the bigger axis. If the width is bigger,
	// then we will partition this area with a vertical line. Similarly
	// if the height is larger then cut across with a horizontal line.
	//
	// The cut that is done needs to make it so that the space we want
	// to consume tightly fits along one axis. This helps the algorithm
	// move towards completion as it solves an axis, then another axis,
	// and then places it in the area it sliced out.
	if (Location.Width() >= Location.Height())
	{
		// This is represented by the following:
		//
		//         Middle
		//          Top      Max
		//    +------+------+
		//    |      .      |
		//    |      .      |
		//    +------+------+
		// Min     Middle
		//         Bottom
		//
		// Note that the dots are where we want to cut, and the width
		// from Min -> MiddleBottom is equal to the dimension width
		// provided since we are cutting vertically.
		Vec2I middleBottom(Location.Min.X + dimension.Width, Location.Min.Y);
		Vec2I middleTop(Location.Min.X + dimension.Width, Location.Max.Y);
		first = Box2I(Location.BottomLeft(), middleTop);
		second = Box2I(middleBottom, Location.TopRight());
	}
	else
	{
		// This is represented by the following:
		//
		//              +-------+ Max
		//              |       |
		//  Left Middle + . . . + Right Middle
		//              |       |
		//          Min +-------+
		//
		// Note that the dots are where we want to cut, and the width
		// from Min -> Left Middle is equal to the dimension height
		// provided since we are cutting horizontally.
		Vec2I leftMiddle(Location.Min.X, Location.Min.Y + dimension.Height);
		Vec2I rightMiddle(Location.Max.X, Location.Min.Y + dimension.Height);
		first = Box2I(Location.BottomLeft(), rightMiddle);
		second = Box2I(leftMiddle, Location.TopRight());
	}

	m_left.reset(new AtlasNode(first, this));
	m_right.reset(new AtlasNode(second, this));

	return m_left->RecursivelyAdd(dimension);
}

}
