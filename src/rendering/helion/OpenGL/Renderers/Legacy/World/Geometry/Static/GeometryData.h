/*
** GeometryData.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Static.GeometryData.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The C# class receives a ready made VertexPipeline; VertexPipeline here references its vertex buffer, so the
** GeometryData creates and owns both (StaticVertexBuffer<StaticVertex> + VertexPipeline<StaticVertex>).
**
*/

#pragma once

#include <memory>
#include "zstring.h"
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/VertexPipeline.h"
#include "OpenGL/Texture/Legacy/GLLegacyTexture.h"
#include "OpenGL/Renderers/Legacy/World/StaticVertex.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

class GeometryData
{
public:
	int TextureHandle;
	Texture::Legacy::GLLegacyTexture* Texture;
	Texture::Legacy::GLLegacyTexture* BrightmapTexture;
	std::unique_ptr<Buffer::Array::Vertex::StaticVertexBuffer<StaticVertex>> Vbo;
	std::unique_ptr<VertexPipeline<StaticVertex>> Pipeline;

	GeometryData(int textureHandle, Texture::Legacy::GLLegacyTexture* texture, ::Helion::Render::OpenGL::Shader::RenderProgram& program, const FString& label, int vboSize,
		Texture::Legacy::GLLegacyTexture* brightmapTexture = nullptr);

	void Dispose();

	FString ToString() const;
};

}
