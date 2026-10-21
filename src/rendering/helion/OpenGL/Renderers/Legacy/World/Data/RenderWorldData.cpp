/*
** RenderWorldData.cpp
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

#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldData.h"
#include "BindTextures.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

RenderWorldData::RenderWorldData(::Helion::Render::OpenGL::Shader::RenderProgram& program)
	: m_vbo(std::make_unique<Buffer::Array::Vertex::DynamicVertexBuffer<DynamicVertex>>("DynamicVertex")),
	  Pipeline(program, *m_vbo, "DynamicVertex")
{
}

RenderWorldData::RenderWorldData(GLLegacyTexture* texture, ::Helion::Render::OpenGL::Shader::RenderProgram& program, GLLegacyTexture* brightmapTexture)
	: RenderWorldData(program)
{
	Set(texture, brightmapTexture);
}

void RenderWorldData::Set(GLLegacyTexture* texture, GLLegacyTexture* brightmapTexture)
{
	Texture = texture;
	BrightmapTexture = brightmapTexture;
}

void RenderWorldData::Clear()
{
	Pipeline.Clear();
}

std::function<bool(GLLegacyTexture* texture, const std::function<void()>& draw)> RenderWorldData::MaterialDraw;

bool RenderWorldData::Draw()
{
	if (Pipeline.Empty() || Texture == nullptr)
		return false;

	if (MaterialDraw && MaterialDraw(Texture, [this]()
	{
		GLLegacyTexture::Unbind(BindTextures::BrightmapTexture);
		Pipeline.Bind(true);
		Pipeline.Vbo.Upload();
		Pipeline.DrawArrays();
		Pipeline.Unbind();
	}))
		return true;

	Texture->Bind(BindTextures::BoundTexture);
	if (BrightmapTexture != nullptr)
		BrightmapTexture->Bind(BindTextures::BrightmapTexture);
	else
		GLLegacyTexture::Unbind(BindTextures::BrightmapTexture);

	Pipeline.Bind(true);
	Pipeline.Vbo.Upload();
	Pipeline.DrawArrays();
	Pipeline.Unbind();
	GLLegacyTexture::Unbind(BindTextures::BoundTexture);
	return true;
}

FString RenderWorldData::ToString() const
{
	return Texture != nullptr ? Texture->Name : FString();
}

}
