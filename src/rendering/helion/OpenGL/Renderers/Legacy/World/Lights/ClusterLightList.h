/*
** ClusterLightList.h
**
** UZDoom-only (plan M6.1/M6.2): the flat light array of one view, built from FLevelLocals::lights (FDynamicLight).
** The record encoding is AddLightToList (hw_dynlightdata.cpp) copied verbatim, so ProcessMaterialLight and
** lightContribution() in the material shaders read the same data as with the table shaders.
**
**---------------------------------------------------------------------------
**
** Copyright 2002-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include <vector>
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterAssignCpu.h"

struct FDynamicLight;
struct FLevelLocals;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

// Which lights a program's surfaces get from the view's clusters.
enum class ClusterLightSet
{
	// Walls, flats, 3D floors, flood fills, decals (hw_walls.cpp / hw_flats.cpp SetupLights): active lights without
	// LF_DONTLIGHTMAP, attenuated only with LF_ATTENUATE / LEVEL3_ATTENUATE.
	World,
	// No clustered lights (sprites use the SetDynLight probe of hw_spritelight.cpp; models have a per draw list,
	// ModelLightList).
	None,
};

class ClusterLightList
{
public:
	// One light into the AddLightToList record (ClusterLight::f[14] = ClusterBucket). group = the portal group the
	// positions are made relative to (FDynamicLight::PosRelative).
	static void Encode(FDynamicLight* light, int group, bool forceAttenuate, ClusterLight& out);

	// The world lights of the view; empty when r_dynlights is off or the level has no lights.
	// group = the portal group of the view's sector.
	static void Build(FLevelLocals* level, int group, std::vector<ClusterLight>& lights);
};

}
