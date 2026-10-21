/*
** LegacyWorldRenderer.Validate.cpp
**
** UZDoom-only partial of LegacyWorldRenderer: render signature sweep (plan D14).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Direct writes to sector / side state that raise none of the world events are caught by comparing the
** render signature (r_defs.h sector_t::renderSignature / side_t::renderSignature) of every sector and side
** with the copy the static cache holds. The comparison and the dirty marking are owned by
** StaticCacheGeometryRenderer::ValidateSignatures (it is the same code path as its event handlers); this
** partial calls it once per game tic (the playsim writes only in tics), before anything is built from the sector /
** side state. The views of a tic (camera textures, portal views, every frame of an uncapped frame rate) share the sweep.
*/

#include "OpenGL/Renderers/Legacy/World/LegacyWorldRenderer.h"
#include "OpenGL/Renderers/Legacy/World/BspBox.h"
#include "Common/Shared/Frustum.h"
#include "HelionCVars.h"
#include "World/WorldStatic.h"
#include "g_levellocals.h"
#include "actor.h"
#include "r_utility.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Static/StaticCacheGeometryRenderer.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

void LegacyWorldRenderer::ValidateSignatures(FLevelLocals* world)
{
	if (m_lastValidatedTick == world->maptime)
		return;

	m_lastValidatedTick = world->maptime;
	m_geometryRenderer->StaticRenderer().ValidateSignatures();
}

}
