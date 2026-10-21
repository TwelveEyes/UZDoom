/*
** Atlas2D.cpp
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

#include "Atlas2D.h"

#include <algorithm>
#include <cassert>
#include <climits>

namespace Helion::Util::Atlas
{

using namespace Helion::Geometry;

Atlas2D::Atlas2D(Helion::Geometry::Dimension initialDimension, int maxSize)
{
	assert(initialDimension.Width > 0 && initialDimension.Height > 0 && "Bad atlas dimensions");

	if (maxSize == 0)
		maxSize = std::min(initialDimension.Width, initialDimension.Height);
	Dimension = initialDimension;
	m_maxSize = maxSize;
	m_root = std::make_unique<AtlasNode>(initialDimension);
}

AtlasHandle* Atlas2D::Add(Helion::Geometry::Dimension dimension)
{
	if (dimension.Width == 0 || dimension.Height == 0 || TooLarge(dimension))
		return nullptr;

	AtlasNode* node = TryAdd(dimension);
	if (node == nullptr)
		return nullptr;

	m_ownedHandles.emplace_back(new AtlasHandle(node));
	AtlasHandle* handle = m_ownedHandles.back().get();
	m_handles.insert(handle);
	return handle;
}

void Atlas2D::Remove(AtlasHandle* handle)
{
	assert(m_handles.count(handle) != 0 && "Trying to remove an atlas handle this atlas doesn't own");

	handle->Node->Delete();
	m_handles.erase(handle);
	m_ownedHandles.erase(std::find_if(m_ownedHandles.begin(), m_ownedHandles.end(),
		[handle](const std::unique_ptr<AtlasHandle>& h) { return h.get() == handle; }));
}

bool Atlas2D::TooLarge(Helion::Geometry::Dimension dimension) const
{
	return dimension.Width > m_maxSize || dimension.Height > m_maxSize;
}

AtlasNode* Atlas2D::TryAdd(Helion::Geometry::Dimension dimension)
{
	AtlasNode* node = m_root->RecursivelyAdd(dimension);
	if (node != nullptr)
		return node;

	Helion::Geometry::Dimension nextDimension = FindSmallestAccommodatingDimension(dimension);
	if (TooLarge(nextDimension))
		return nullptr;

	ResizeAtlas(nextDimension);
	node = m_root->RecursivelyAdd(dimension);

	assert(node != nullptr && "Should not fail when we made sure enough space exists to add to atlas");
	return node;
}

Helion::Geometry::Dimension Atlas2D::FindSmallestAccommodatingDimension(Helion::Geometry::Dimension dimension) const
{
	assert((dimension.Width > 0 || dimension.Height > 0) && "Dimension for atlas cannot be negative or zero");
	assert(dimension.Width < INT_MAX / 2 && "Width too large for atlas component, risking integer overflow");
	assert(dimension.Height < INT_MAX / 2 && "Height too large for atlas component, risking integer overflow");

	// We either want to pick the doubled size of the current dimension
	// (in case we're having trouble adding something small) or the
	// dimension from something that is larger than the current atlas.
	int desiredWidth = std::max(Dimension.Width * 2, dimension.Width);
	int desiredHeight = std::max(Dimension.Height * 2, dimension.Height);

	auto FindNextLargestOrEqual = [](int current, int target)
	{
		while (current < target)
			current *= 2;
		return current;
	};

	int width = FindNextLargestOrEqual(Dimension.Width, desiredWidth);
	int height = FindNextLargestOrEqual(Dimension.Height, desiredHeight);
	return Helion::Geometry::Dimension(width, height);
}

void Atlas2D::ResizeAtlas(Helion::Geometry::Dimension newDimension)
{
	Helion::Geometry::Dimension prevDimension = Dimension;
	m_retiredRoots.push_back(std::move(m_root));
	AtlasNode& prevRoot = *m_retiredRoots.back();

	Dimension = newDimension;
	m_root = std::make_unique<AtlasNode>(newDimension);

	AtlasNode* node = m_root->RecursivelyAdd(prevDimension);
	assert(node != nullptr && "Should never fail to add previous atlas when we make the atlas larger");

	if (node != nullptr)
		node->EmplaceExistingTree(prevRoot);
}

}
