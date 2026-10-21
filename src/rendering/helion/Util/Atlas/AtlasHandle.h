/*
** AtlasHandle.h
**
** Port of Helion.Util.Atlas.AtlasHandle.
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
#include "Util/Atlas/AtlasNode.h"

namespace Helion::Util::Atlas
{

class Atlas2D;

/// <summary>
/// A handle to a location in the tree.
/// </summary>
class AtlasHandle
{
	friend class Atlas2D;

public:
	AtlasHandle(const AtlasHandle&) = delete;
	AtlasHandle& operator=(const AtlasHandle&) = delete;

	/// <summary>
	/// The location on the atlas.
	/// </summary>
	Helion::Geometry::Box2I Location() const { return Node->Location; }

	/// <summary>
	/// The node that this wraps around.
	/// </summary>
	AtlasNode* Node;

private:
	/// <summary>
	/// Creates a new handle from an existing node.
	/// </summary>
	/// <param name="node">The node that was allocated.</param>
	explicit AtlasHandle(AtlasNode* node) : Node(node) { }
};

}
