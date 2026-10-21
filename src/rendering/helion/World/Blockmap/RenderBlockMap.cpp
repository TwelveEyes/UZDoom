/*
** RenderBlockMap.cpp
**
** Render-side parts of Helion.World.Blockmap.BlockMap.
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

#include "RenderBlockMap.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "actor.h"
#include "g_levellocals.h"
#include "r_data/r_sections.h"

namespace Helion::World::Blockmap
{

static int NextPowerOfTwo(int value)
{
	int result = 1;
	while (result < value)
		result <<= 1;
	return result;
}

static bool IsZero(double value)
{
	return std::fabs(value) < 1e-9;
}

//==========================================================================
//
// BlockmapSegIterator
//
//==========================================================================

BlockmapSegIterator::BlockmapSegIterator(const RenderBlockMap& grid, double startX, double startY, double endX, double endY)
{
	m_totalBlocks = grid.TotalBlocks;

	// The grid only covers the map bounds (+1 block); polyobjects can move a line outside of it.
	// Clamp so the walk can never wrap into a neighbouring row.
	const double maxX = grid.Width * grid.Dimension - 1e-6;
	const double maxY = grid.Height * grid.Dimension - 1e-6;
	startX = std::clamp(startX - grid.OriginX, 0.0, maxX);
	startY = std::clamp(startY - grid.OriginY, 0.0, maxY);
	endX = std::clamp(endX - grid.OriginX, 0.0, maxX);
	endY = std::clamp(endY - grid.OriginY, 0.0, maxY);

	double blockUnitStartX = startX / grid.Dimension;
	double blockUnitStartY = startY / grid.Dimension;
	double blockUnitEndX = endX / grid.Dimension;
	double blockUnitEndY = endY / grid.Dimension;

	int startingBlockX = (int)blockUnitStartX;
	int startingBlockY = (int)blockUnitStartY;
	m_absDeltaX = std::fabs(blockUnitEndX - blockUnitStartX);
	m_absDeltaY = std::fabs(blockUnitEndY - blockUnitStartY);
	m_blockIndex = startingBlockX + (startingBlockY * grid.Width);

	if (IsZero(m_absDeltaX))
	{
		m_error = DBL_MAX;
	}
	else if (blockUnitEndX > blockUnitStartX)
	{
		m_horizontalStep = 1;
		m_numBlocks += (int)std::floor(blockUnitEndX) - startingBlockX;
		m_error = (std::floor(blockUnitStartX) + 1 - blockUnitStartX) * m_absDeltaY;
	}
	else
	{
		m_horizontalStep = -1;
		m_numBlocks += startingBlockX - (int)std::floor(blockUnitEndX);
		m_error = (blockUnitStartX - std::floor(blockUnitStartX)) * m_absDeltaY;
	}

	if (IsZero(m_absDeltaY))
	{
		m_error = -DBL_MAX;
	}
	else if (blockUnitEndY > blockUnitStartY)
	{
		m_verticalStep = grid.Width;
		m_numBlocks += (int)std::floor(blockUnitEndY) - startingBlockY;
		m_error -= (std::floor(blockUnitStartY) + 1 - blockUnitStartY) * m_absDeltaX;
	}
	else
	{
		m_verticalStep = -grid.Width;
		m_numBlocks += startingBlockY - (int)std::floor(blockUnitEndY);
		m_error -= (blockUnitStartY - std::floor(blockUnitStartY)) * m_absDeltaX;
	}

	if (m_numBlocks > grid.TotalBlocks)
		m_numBlocks = grid.TotalBlocks;
}

int BlockmapSegIterator::NextIndex()
{
	if (m_blocksVisited >= m_numBlocks || m_blockIndex < 0 || m_blockIndex >= m_totalBlocks)
		return -1;

	int currentBlockIndex = m_blockIndex;
	m_blocksVisited++;

	if (m_error > 0)
	{
		m_blockIndex += m_verticalStep;
		m_error -= m_absDeltaX;
	}
	else
	{
		m_blockIndex += m_horizontalStep;
		m_error += m_absDeltaY;
	}

	return currentBlockIndex;
}

//==========================================================================
//
// RenderBlockMap
//
//==========================================================================

RenderBlockMap::RenderBlockMap(FLevelLocals* level, int blockDimension)
	: m_level(level)
{
	blockDimension = NextPowerOfTwo(blockDimension);

	double minX = DBL_MAX, minY = DBL_MAX, maxX = -DBL_MAX, maxY = -DBL_MAX;
	for (const line_t& line : level->lines)
	{
		for (const vertex_t* v : { line.v1, line.v2 })
		{
			minX = std::min(minX, v->fX());
			minY = std::min(minY, v->fY());
			maxX = std::max(maxX, v->fX());
			maxY = std::max(maxY, v->fY());
		}
	}
	if (minX > maxX)
	{
		minX = minY = 0;
		maxX = maxY = 1;
	}

	Dimension = blockDimension;
	DimensionShift = 0;
	while ((1 << DimensionShift) < blockDimension)
		DimensionShift++;

	// Note that we subtract 1 from the bottom left and add 1 to the top right block, see Helion's
	// BlockMap.ToBounds: padding the grid by one block keeps diagonal edge cases inside the grid.
	int alignedLeftBlock = (int)std::floor(minX / Dimension) - 1;
	int alignedBottomBlock = (int)std::floor(minY / Dimension) - 1;
	int alignedRightBlock = (int)std::ceil(maxX / Dimension) + 1;
	int alignedTopBlock = (int)std::ceil(maxY / Dimension) + 1;

	OriginX = (double)alignedLeftBlock * Dimension;
	OriginY = (double)alignedBottomBlock * Dimension;
	BoundsMaxX = (double)alignedRightBlock * Dimension;
	BoundsMaxY = (double)alignedTopBlock * Dimension;

	Width = alignedRightBlock - alignedLeftBlock;
	Height = alignedTopBlock - alignedBottomBlock;
	TotalBlocks = Width * Height;

	HeadRenderEntities.Resize(TotalBlocks);
	for (AActor*& head : HeadRenderEntities)
		head = nullptr;
	DynamicSectors.Resize(TotalBlocks);
	DynamicSides.Resize(TotalBlocks);
	m_sectorBlocks.Resize(level->sectors.Size());
	m_sideBlocks.Resize(level->sides.Size());
}

RenderBlockMap::~RenderBlockMap()
{
	for (AActor* head : HeadRenderEntities)
	{
		AActor* entity = head;
		while (entity != nullptr)
		{
			AActor* next = entity->renderBlockNext;
			entity->renderBlock = 0;
			entity->renderBlockNext = nullptr;
			entity->renderBlockPrev = nullptr;
			entity = next;
		}
	}
}

void RenderBlockMap::Clear()
{
	for (unsigned i = 0; i < DynamicSectors.Size(); i++)
	{
		DynamicSectors[i].Clear();
		DynamicSides[i].Clear();
	}
	for (auto& blocks : m_sectorBlocks)
		blocks.Clear();
	for (auto& blocks : m_sideBlocks)
		blocks.Clear();
}

int RenderBlockMap::GetBlockIndex(double xPos, double yPos) const
{
	double x = xPos - OriginX;
	double y = yPos - OriginY;
	if (!(x >= 0 && y >= 0 && x < (double)Width * Dimension && y < (double)Height * Dimension))
		return -1;

	return ((int)y >> DimensionShift) * Width + ((int)x >> DimensionShift);
}

BlockmapBoxIteration RenderBlockMap::CreateBoxIteration(double minX, double minY, double maxX, double maxY) const
{
	// Clamp in double space before the integer shift so values far outside the grid stay well defined.
	const double extentX = (double)Width * Dimension - 1;
	const double extentY = (double)Height * Dimension - 1;
	int startX = (int)std::clamp(minX - OriginX, 0.0, extentX) >> DimensionShift;
	int startY = (int)std::clamp(minY - OriginY, 0.0, extentY) >> DimensionShift;
	int endX = (int)std::clamp(maxX - OriginX, 0.0, extentX) >> DimensionShift;
	int endY = (int)std::clamp(maxY - OriginY, 0.0, extentY) >> DimensionShift;
	return { startX, startY, endX, endY, Width };
}

BlockmapBoxIteration RenderBlockMap::CreateBoxIteration(double x, double y, double radius) const
{
	return CreateBoxIteration(x - radius, y - radius, x + radius, y + radius);
}

//==========================================================================
//
// Entities
//
//==========================================================================

void RenderBlockMap::RenderLink(AActor* entity)
{
	// Helion asserts that the entity was unlinked first; be tolerant instead.
	if (entity->renderBlock != 0)
		RemoveRenderLink(entity);

	int block = GetBlockIndex(entity->X(), entity->Y());
	if (block == -1)
		return;

	entity->renderBlock = block + 1;
	entity->renderBlockPrev = nullptr;
	AActor* headEntity = HeadRenderEntities[block];
	entity->renderBlockNext = headEntity;
	if (headEntity != nullptr)
		headEntity->renderBlockPrev = entity;
	HeadRenderEntities[block] = entity;
}

void RenderBlockMap::RemoveRenderLink(AActor* entity)
{
	if (entity->renderBlock == 0)
		return;

	int block = entity->renderBlock - 1;
	entity->renderBlock = 0;
	if (block >= TotalBlocks)
	{
		entity->renderBlockNext = nullptr;
		entity->renderBlockPrev = nullptr;
		return;
	}

	if (HeadRenderEntities[block] == entity)
		HeadRenderEntities[block] = entity->renderBlockNext;
	if (entity->renderBlockNext != nullptr)
		entity->renderBlockNext->renderBlockPrev = entity->renderBlockPrev;
	if (entity->renderBlockPrev != nullptr)
		entity->renderBlockPrev->renderBlockNext = entity->renderBlockNext;

	entity->renderBlockNext = nullptr;
	entity->renderBlockPrev = nullptr;
}

//==========================================================================
//
// Dynamic sectors
//
//==========================================================================

void RenderBlockMap::LinkDynamic(sector_t* sector)
{
	const unsigned sectorIndex = sector->Index();
	if (sectorIndex >= m_sectorBlocks.Size() || m_sectorBlocks[sectorIndex].Size() != 0)
		return;

	auto sections = m_level->sections.SectionsForSector(sector);
	for (unsigned i = 0; i < sections.Size(); i++)
	{
		const BoundingRect& bounds = sections[i].bounds;
		if (bounds.left > bounds.right || bounds.top > bounds.bottom)
			continue;

		BlockmapBoxIteration it = CreateBoxIteration(bounds.left, bounds.top, bounds.right, bounds.bottom);
		for (int by = it.BlockStartY; by <= it.BlockEndY; by++)
		{
			for (int bx = it.BlockStartX; bx <= it.BlockEndX; bx++)
			{
				int index = by * it.Width + bx;
				// A sector with several sections can touch the same block more than once.
				if (m_sectorBlocks[sectorIndex].Find(index) != m_sectorBlocks[sectorIndex].Size())
					continue;

				DynamicSectors[index].Push(sector);
				m_sectorBlocks[sectorIndex].Push(index);
			}
		}
	}
}

void RenderBlockMap::UnlinkDynamic(sector_t* sector)
{
	const unsigned sectorIndex = sector->Index();
	if (sectorIndex >= m_sectorBlocks.Size())
		return;

	for (int index : m_sectorBlocks[sectorIndex])
	{
		unsigned found = DynamicSectors[index].Find(sector);
		if (found != DynamicSectors[index].Size())
			DynamicSectors[index].Delete(found);
	}
	m_sectorBlocks[sectorIndex].Clear();
}

bool RenderBlockMap::IsLinkedDynamic(const sector_t* sector) const
{
	const unsigned sectorIndex = sector->Index();
	return sectorIndex < m_sectorBlocks.Size() && m_sectorBlocks[sectorIndex].Size() != 0;
}

//==========================================================================
//
// Dynamic sides
//
//==========================================================================

void RenderBlockMap::LinkDynamicSide(side_t* side)
{
	const unsigned sideIndex = side->Index();
	if (sideIndex >= m_sideBlocks.Size() || m_sideBlocks[sideIndex].Size() != 0)
		return;

	const line_t* line = side->linedef;
	BlockmapSegIterator it(*this, line->v1->fX(), line->v1->fY(), line->v2->fX(), line->v2->fY());
	while (true)
	{
		int index = it.NextIndex();
		if (index == -1)
			break;

		DynamicSides[index].Push(side);
		m_sideBlocks[sideIndex].Push(index);
	}
}

void RenderBlockMap::UnlinkDynamicSide(side_t* side)
{
	const unsigned sideIndex = side->Index();
	if (sideIndex >= m_sideBlocks.Size())
		return;

	for (int index : m_sideBlocks[sideIndex])
	{
		unsigned found = DynamicSides[index].Find(side);
		if (found != DynamicSides[index].Size())
			DynamicSides[index].Delete(found);
	}
	m_sideBlocks[sideIndex].Clear();
}

bool RenderBlockMap::IsLinkedDynamicSide(const side_t* side) const
{
	const unsigned sideIndex = side->Index();
	return sideIndex < m_sideBlocks.Size() && m_sideBlocks[sideIndex].Size() != 0;
}

}
