/*
** FloodFillInfo.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Portals.FloodFill.FloodFillInfo.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The C# record receives its VertexPipeline; VertexPipeline here references its vertex buffer, so the info owns both.
**
*/

#pragma once

#include <memory>
#include "FloodFillVertex.h"
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/VertexPipeline.h"
#include "OpenGL/Texture/Legacy/GLLegacyTexture.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals::FloodFill
{

class FloodFillInfo
{
public:
	int TextureHandle;
	Texture::Legacy::GLLegacyTexture* Texture;
	double Z;
	std::unique_ptr<Buffer::Array::Vertex::StaticVertexBuffer<FloodFillVertex>> Vbo;
	std::unique_ptr<VertexPipeline<FloodFillVertex>> Pipeline;

	FloodFillInfo(int textureHandle, Texture::Legacy::GLLegacyTexture* texture, double z, ::Helion::Render::OpenGL::Shader::RenderProgram& program, const FString& label)
		: TextureHandle(textureHandle), Texture(texture), Z(z)
	{
		Vbo = std::make_unique<Buffer::Array::Vertex::StaticVertexBuffer<FloodFillVertex>>(label);
		Pipeline = std::make_unique<VertexPipeline<FloodFillVertex>>(program, *Vbo, label);
	}
};

}
