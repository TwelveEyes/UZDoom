/*
** AtlasNode.h
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

#pragma once

#include <memory>
#include "Util/Geometry.h"

namespace Helion::Util::Atlas
{

class Atlas2D;
class AtlasHandle;

/// <summary>
/// A binary tree node which either represents a cut (and thus has two
/// children) or is a leaf node which represents a used up space.
/// </summary>
/// <remarks>
/// It uses a binary tree structure to allow for O(lg n) insertion and
/// deletion times. Also note that this atlas grows from the origin as if
/// it were a cartesian plane, so `left` and `right` when there is a
/// vertical split is actually `bottom` and `top` (implying it grows up to
/// the right).
/// </remarks>
class AtlasNode
{
	friend class Atlas2D;
	friend class AtlasHandle;

public:
	/// <summary>
	/// The location in the owning atlas.
	/// </summary>
	Helion::Geometry::Box2I Location;

	/// <summary>
	/// Creates a root node from the dimension of the atlas.
	/// </summary>
	/// <param name="atlasDimension">The dimension of the entire atlas.
	/// </param>
	explicit AtlasNode(Helion::Geometry::Dimension atlasDimension);

	AtlasNode(const AtlasNode&) = delete;
	AtlasNode& operator=(const AtlasNode&) = delete;

	/// <summary>
	/// Attempts to recursively add the space provided into the atlas. If
	/// it cannot be done, it will return null.
	/// </summary>
	/// <param name="dimension">The space to try to add.</param>
	/// <returns>The node that represents the allocated space, or null if
	/// there is no space in any child of this.</returns>
	AtlasNode* RecursivelyAdd(Helion::Geometry::Dimension dimension);

	/// <summary>
	/// Deletes the node by making its space available. This will invoke
	/// recursive traversal to clean up space.
	/// </summary>
	void Delete();

	/// <summary>
	/// Make this current node be the tree from another atlas node. The
	/// children of treeRoot are moved into this node.
	/// </summary>
	/// <remarks>
	/// Right now we do not support adding an existing tree to anywhere but
	/// the origin, so we can only add an existing tree to a new tree if it
	/// was the first one to be added. We could however fix this by doing a
	/// recursive traversal and adding the offset of this node to the child
	/// location coordinates. This unfortunately would break invariants for
	/// the class since things that depend on this (like texture managers)
	/// depend on the location staying the exact same, and is why we do not
	/// do this.
	/// </remarks>
	/// <param name="treeRoot">The root of another atlas tree.</param>
	void EmplaceExistingTree(AtlasNode& treeRoot);

private:
	AtlasNode(Helion::Geometry::Box2I location, AtlasNode* parent);

	/// <summary>
	/// A convenience helper for iteration over both children (left, then
	/// right). Entries may be null.
	/// </summary>
	template<typename Func>
	void ForEachChild(Func&& func) const
	{
		func(m_left.get());
		func(m_right.get());
	}

	/// <summary>
	/// Checks if this node has any children.
	/// </summary>
	bool HasChildren() const { return m_left != nullptr || m_right != nullptr; }

	/// <summary>
	/// A recursive call that bubbles upwards to the parent.
	/// </summary>
	void RecursivelyNotifySomeChildWasAdded();

	/// <summary>
	/// A heuristic function for checking if this node can possibly hold
	/// the dimension provided. This may return a false negative, but that
	/// is the nature of this heuristic.
	/// </summary>
	bool CanPossiblyAdd(Helion::Geometry::Dimension dimension) const;

	AtlasNode* AddAsWidthFit(Helion::Geometry::Dimension dimension);
	AtlasNode* AddAsHeightFit(Helion::Geometry::Dimension dimension);
	AtlasNode* AddToBottomLeftCornerRecursively(Helion::Geometry::Dimension dimension);

	/// <summary>
	/// The parent owner of this node, if any. Used in recursive ascent
	/// when setting parent values.
	/// </summary>
	AtlasNode* m_parent = nullptr;

	/// <summary>
	/// The left node (or the bottom node).
	/// </summary>
	std::unique_ptr<AtlasNode> m_left;

	/// <summary>
	/// The right node (or top node).
	/// </summary>
	std::unique_ptr<AtlasNode> m_right;

	/// <summary>
	/// A cached value which knows the maximum dimensions from either child
	/// for this node. This only contains the max values, so if the left is
	/// (5, 8) and the right is (6, 2) then this contains (6, 8). If this
	/// is a leaf node that is not occupied, it equals the dimension. If
	/// this is an occupied leaf node, it is (0, 0).
	/// </summary>
	Helion::Geometry::Dimension m_maxAvailableDimensions;

	/// <summary>
	/// True if this node is a leaf node that represents an allocated space
	/// or also true if this is a parent node and every child is occupied.
	/// </summary>
	bool m_occupied = false;
};

}
