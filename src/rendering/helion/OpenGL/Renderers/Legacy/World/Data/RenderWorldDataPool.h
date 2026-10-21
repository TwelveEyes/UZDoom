/*
** RenderWorldDataPool.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Data.RenderWorldDataPool.
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
#include <vector>
#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldData.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

// The pool owns every RenderWorldData it created; Get/Add hand out raw pointers (references in Helion).
class RenderWorldDataPool
{
public:
	int PoolSize;
	int UseCount = 0;

	RenderWorldDataPool(::Helion::Render::OpenGL::Shader::RenderProgram& program, int poolSize);

	RenderWorldDataPool(const RenderWorldDataPool&) = delete;
	RenderWorldDataPool& operator=(const RenderWorldDataPool&) = delete;

	void RefillPool(int size);
	void Add(RenderWorldData* renderData);
	RenderWorldData& Get(GLLegacyTexture* texture, GLLegacyTexture* brightMapTexture = nullptr);

private:
	RenderWorldData* Create();

	::Helion::Render::OpenGL::Shader::RenderProgram* m_program;
	std::vector<RenderWorldData*> m_renderData;
	std::vector<std::unique_ptr<RenderWorldData>> m_owned;
};

}
