/*
** Atlas2D.h
**
** Port of Helion.Util.Atlas.Atlas2D.
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
#include <unordered_set>
#include <vector>
#include "Util/Geometry.h"
#include "Util/Atlas/AtlasHandle.h"
#include "Util/Atlas/AtlasNode.h"

namespace Helion::Util::Atlas
{

/// <summary>
/// A two dimensional atlas that tracks consumed space.
/// </summary>
class Atlas2D
{
public:
	Helion::Geometry::Dimension Dimension;

	/// <summary>
	/// Creates a new atlas.
	/// </summary>
	/// <param name="initialDimension">The starting dimension.</param>
	/// <param name="maxSize">The max size beyond which we will not expand.
	/// If 0 (the C# null) it will be set to the smallest side of the
	/// initial dimension. If provided, it should also be positive.
	/// </param>
	explicit Atlas2D(Helion::Geometry::Dimension initialDimension, int maxSize = 0);

	/// <summary>
	/// Consumes a space in the atlas, or returns null if it cannot or if
	/// the dimension is malformed. This will resize the tree if there is
	/// not enough space, but enough space to resize and accomodate it.
	/// </summary>
	/// <param name="dimension">The size to consume.</param>
	/// <returns>A handle (owned by this atlas) to the newly consumed size
	/// with the information, or nullptr if the area could not be allocated
	/// to the caller.</returns>
	AtlasHandle* Add(Helion::Geometry::Dimension dimension);

	/// <summary>
	/// Deletes the handle for some tree, freeing up space so that other
	/// calls can use it. The handle is destroyed.
	/// </summary>
	/// <param name="handle">The handle to free, which must be owned by
	/// this atlas.</param>
	void Remove(AtlasHandle* handle);

private:
	bool TooLarge(Helion::Geometry::Dimension dimension) const;
	AtlasNode* TryAdd(Helion::Geometry::Dimension dimension);
	Helion::Geometry::Dimension FindSmallestAccommodatingDimension(Helion::Geometry::Dimension dimension) const;
	void ResizeAtlas(Helion::Geometry::Dimension newDimension);

	int m_maxSize;
	std::unordered_set<AtlasHandle*> m_handles;
	std::vector<std::unique_ptr<AtlasHandle>> m_ownedHandles;
	std::unique_ptr<AtlasNode> m_root;

	// Previous roots are kept alive because a handle may refer to a
	// root node that was replaced by a resize.
	std::vector<std::unique_ptr<AtlasNode>> m_retiredRoots;
};

}
