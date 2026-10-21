/*
** RenderBlock.h
**
** Port of Helion.Render.RenderBlock.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Line -> line_t*, Side -> side_t*, Sector -> sector_t*. Helion's
** "TransferHeights != null" is sector_t::GetHeightSec() != nullptr and its
** "TextureHandle > NullCompatibilityTextureIndex" is FTextureID::isValid()
** (a real texture, neither none nor the "-" null texture).
*/

#pragma once

#include "World/Geometry/Sectors/SectorPlane.h"

struct line_t;
struct side_t;
struct sector_t;

namespace Helion::Render
{

// Check null texture handle for rendering tricks. Not blocked if missing texture.
// Works around issue the original game had not adding blocking lines to view clipper.
// E.g. TNT Map02 - see through window that opens as a door
class RenderBlock
{
public:
	static bool IsSkyBlocked(line_t* line);

	static bool IsBlocked(line_t* line, bool onFrontSide);

	static bool IsBlocked(side_t* side, sector_t* frontSector, sector_t* backSector);

	static bool IsBlocked(side_t* side,
		const World::Geometry::Sectors::SectorPlane& frontFloorPlane, const World::Geometry::Sectors::SectorPlane& backFloorPlane,
		const World::Geometry::Sectors::SectorPlane& frontCeilingPlane, const World::Geometry::Sectors::SectorPlane& backCeilingPlane);
};

}
