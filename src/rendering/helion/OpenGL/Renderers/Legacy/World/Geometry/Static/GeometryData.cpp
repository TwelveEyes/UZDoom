/*
** GeometryData.cpp
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
*/

#include "GeometryData.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

GeometryData::GeometryData(int textureHandle, Texture::Legacy::GLLegacyTexture* texture, ::Helion::Render::OpenGL::Shader::RenderProgram& program, const FString& label,
	int vboSize, Texture::Legacy::GLLegacyTexture* brightmapTexture)
	: TextureHandle(textureHandle), Texture(texture), BrightmapTexture(brightmapTexture)
{
	Vbo = std::make_unique<Buffer::Array::Vertex::StaticVertexBuffer<StaticVertex>>(label, vboSize);
	Pipeline = std::make_unique<VertexPipeline<StaticVertex>>(program, *Vbo, label);
}

void GeometryData::Dispose()
{
	Pipeline.reset();
	Vbo.reset();
}

FString GeometryData::ToString() const
{
	return Texture != nullptr ? Texture->ToString() : FString();
}

}
