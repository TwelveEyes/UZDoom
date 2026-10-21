/*
** RenderWorldData.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Data.RenderWorldData.
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

#include <functional>
#include <memory>
#include "zstring.h"
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/Renderers/Legacy/World/DynamicVertex.h"
#include "OpenGL/Shader/RenderProgram.h"
#include "OpenGL/Texture/Legacy/GLLegacyTexture.h"
#include "OpenGL/VertexPipeline.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

using Texture::Legacy::GLLegacyTexture;

class RenderWorldData
{
public:
	GLLegacyTexture* Texture = nullptr;
	GLLegacyTexture* BrightmapTexture = nullptr;
	std::unique_ptr<Buffer::Array::Vertex::DynamicVertexBuffer<DynamicVertex>> m_vbo;
	VertexPipeline<DynamicVertex> Pipeline;
	int RenderCount = 0;

	explicit RenderWorldData(::Helion::Render::OpenGL::Shader::RenderProgram& program);
	RenderWorldData(GLLegacyTexture* texture, ::Helion::Render::OpenGL::Shader::RenderProgram& program, GLLegacyTexture* brightmapTexture = nullptr);

	RenderWorldData(const RenderWorldData&) = delete;
	RenderWorldData& operator=(const RenderWorldData&) = delete;

	void Set(GLLegacyTexture* texture, GLLegacyTexture* brightmapTexture = nullptr);
	void Clear();
	bool Draw();

	// UZDoom (plan D13): draws geometry whose texture is not an array texture through the material path; set by
	// LegacyWorldRenderer (DrawMaterialGeometry). Returns false when the texture is drawn by the array programs.
	static std::function<bool(GLLegacyTexture* texture, const std::function<void()>& draw)> MaterialDraw;
	FString ToString() const;
};

}
