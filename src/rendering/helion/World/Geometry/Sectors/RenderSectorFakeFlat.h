/*
** RenderSectorFakeFlat.h
**
** UZDoom-only: fake flat (Boom deep water / transfer heights) helpers of the Helion geometry renderer.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Implemented in OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.FakeFlat.cpp with the rules of
** hw_fakeflat.cpp (hw_FakeFlat). GetRenderSector / ClearFakeFlatSectors are declared in RenderSector.h.
*/

#pragma once

#include "vectors.h"
#include "World/Geometry/Sectors/TransferHeightsView.h"

struct sector_t;

namespace Helion::World::Geometry::Sectors
{

// Helion Sector.TransferHeights != null: the sector has a height sector that is not ignored
// (sector_t::GetHeightSec(): SECMF_IGNOREHEIGHTSEC sectors only trigger sector actions).
bool SectorFakeFlatApplies(sector_t* sector);

// Helion TransferHeights.GetView(viewSector, viewZ) with the area detection of classic SetViewArea:
// the camera position against the planes of the height sector of the camera sector. Middle when the camera
// sector has no (applicable) height sector.
TransferHeightView GetTransferHeightView(sector_t* viewSector, const DVector3& viewPos);

}
