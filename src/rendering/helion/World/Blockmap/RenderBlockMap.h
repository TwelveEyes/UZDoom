/*
** RenderBlockMap.h
**
** Render-side parts of Helion.World.Blockmap.BlockMap: the block grid used by the
** static render mode to find entities (HeadRenderEntities / RenderLink), dynamic
** sectors (moving or scrolling) and dynamic sides (scrolling, polyobject).
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

#include "tarray.h"

class AActor;
struct sector_t;
struct side_t;
struct line_t;
struct FLevelLocals;

namespace Helion::World::Blockmap
{

struct BlockmapBoxIteration
{
	int BlockStartX = 0;
	int BlockStartY = 0;
	int BlockEndX = -1;
	int BlockEndY = -1;
	int Width = 0;
};

class RenderBlockMap;

// Walks every block a line segment passes through (Helion BlockmapSegIterator).
class BlockmapSegIterator
{
public:
	BlockmapSegIterator(const RenderBlockMap& grid, double startX, double startY, double endX, double endY);

	// Returns the next block index, or -1 when the walk is finished.
	int NextIndex();

private:
	int m_totalBlocks;
	int m_numBlocks = 1;
	int m_verticalStep = 0;
	int m_horizontalStep = 0;
	int m_blockIndex;
	int m_blocksVisited = 0;
	double m_error = 0;
	double m_absDeltaX;
	double m_absDeltaY;
};

class RenderBlockMap
{
public:
	// Helion's default render block dimension.
	static constexpr int DefaultDimension = 128;

	int Dimension = 0;
	int Width = 0;
	int Height = 0;
	int TotalBlocks = 0;
	int DimensionShift = 0;

	double OriginX = 0;
	double OriginY = 0;
	double BoundsMaxX = 0;
	double BoundsMaxY = 0;

	// First entity of each block's intrusive AActor::renderBlockNext list.
	TArray<AActor*> HeadRenderEntities;
	// Moving/scrolling sectors and scrolling/polyobject sides touching each block.
	TArray<TArray<sector_t*>> DynamicSectors;
	TArray<TArray<side_t*>> DynamicSides;

	// Bounds are taken from every line of the level, expanded by one block on each side like Helion.
	explicit RenderBlockMap(FLevelLocals* level, int blockDimension = DefaultDimension);

	// Detaches every still-linked entity (e.g. travelling players) so no stale links outlive the grid.
	~RenderBlockMap();

	// Drops all dynamic sector and side links. Entities are unlinked via AActor::UnlinkFromWorld.
	void Clear();

	// Returns -1 when the position is outside the grid.
	int GetBlockIndex(double xPos, double yPos) const;
	BlockmapBoxIteration CreateBoxIteration(double minX, double minY, double maxX, double maxY) const;
	BlockmapBoxIteration CreateBoxIteration(double x, double y, double radius) const;

	// Entities. AActor::renderBlock is the block index + 1 (0 = not linked).
	void RenderLink(AActor* entity);
	void RemoveRenderLink(AActor* entity);

	// Dynamic sectors: linked into every block touched by one of the sector's FSection bounds.
	void LinkDynamic(sector_t* sector);
	void UnlinkDynamic(sector_t* sector);
	bool IsLinkedDynamic(const sector_t* sector) const;

	// Dynamic sides: linked into every block the side's line passes through.
	void LinkDynamicSide(side_t* side);
	void UnlinkDynamicSide(side_t* side);
	bool IsLinkedDynamicSide(const side_t* side) const;

private:
	FLevelLocals* m_level;
	TArray<TArray<int>> m_sectorBlocks;	// per sector: blocks it is linked in
	TArray<TArray<int>> m_sideBlocks;	// per side: blocks it is linked in
};

}
