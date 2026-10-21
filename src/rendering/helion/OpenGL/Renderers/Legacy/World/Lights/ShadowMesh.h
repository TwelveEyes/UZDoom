/*
** ShadowMesh.h
**
** UZDoom-only (plan M6.3): the wall-only mesh the Vulkan ray traced light shadows (VkRaytrace::SetLevelMesh,
** shadowAttenuation() with SUPPORTS_RAYTRACING in main.fp) trace against. It replaces DoomLevelMesh as the source of the
** acceleration structure; nothing but hwrenderer::LevelMesh's vertex / index arrays is consumed.
**
** One quad per line that can block light (vertices in map coordinates x, y, z like DoomLevelMesh's):
**   - one-sided lines: full height (-32768 .. 32768)
**   - two-sided lines: the part below the higher of the two floors and the part above the lower of the two ceilings
**     (sloped planes are evaluated at the line's end points); lines whose two sectors have the same planes at both
**     end points, line portals, polyobject lines and lines without a sidedef produce nothing.
** The plan text says "one quad per blocking two-sided line, full height"; full height quads on every two-sided line would
** shut every doorway, so the two-sided lines contribute their actual blocking parts instead.
**
** The mesh is a snapshot of the sector planes at build time (the old levelmesh was static in the same way). Call
** Build again, and hand the new mesh to screen->SetLevelMesh, when a map's sector planes have to be re-read.
**
**---------------------------------------------------------------------------
**
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include <memory>
#include "hwrenderer/data/hw_levelmesh.h"

struct FLevelLocals;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

class ShadowMesh : public hwrenderer::LevelMesh
{
public:
	// The extent of "full height" quads.
	static constexpr float MaxHeight = 32768.0f;

	static std::unique_ptr<ShadowMesh> Build(FLevelLocals& level);

	int QuadCount() const { return (int)MeshSurfaces.Size() / 2; }
};

}
