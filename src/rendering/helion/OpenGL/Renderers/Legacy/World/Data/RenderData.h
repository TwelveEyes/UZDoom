/*
** RenderData.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Data.RenderData.
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
#include "tarray.h"
#include "BindTextures.h"
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/Shader/RenderProgram.h"
#include "OpenGL/Texture/Legacy/GLLegacyTexture.h"
#include "OpenGL/Util/PrimitiveType.h"
#include "OpenGL/VertexPipeline.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

using Texture::Legacy::GLLegacyTexture;

// Vertex types that define `static constexpr bool IsInstanced = true;` are drawn as a 4 vertex triangle strip per element.
template<typename TVertex>
class RenderData
{
public:
	// The C# class owns the buffer through the pipeline; the buffer outlives the pipeline here, so it is a member declared first.
	std::unique_ptr<Buffer::Array::Vertex::DynamicVertexBuffer<TVertex>> m_vbo;
	VertexPipeline<TVertex> Pipeline;
	GLLegacyTexture* Texture = nullptr;
	GLLegacyTexture* BrightMapTexture = nullptr;
	TArray<TVertex>& ArrayData;
	int RenderCount = 0;

	RenderData(::Helion::Render::OpenGL::Shader::RenderProgram& program, int capacity, GLLegacyTexture* texture, GLLegacyTexture* brightMapTexture = nullptr)
		: RenderData(program, capacity)
	{
		Set(texture, brightMapTexture);
	}

	RenderData(::Helion::Render::OpenGL::Shader::RenderProgram& program, int capacity)
		: m_vbo(std::make_unique<Buffer::Array::Vertex::DynamicVertexBuffer<TVertex>>("Entity VBO", capacity)),
		  Pipeline(program, *m_vbo, "Entity VAO"),
		  ArrayData(m_vbo->Data)
	{
	}

	RenderData(const RenderData&) = delete;
	RenderData& operator=(const RenderData&) = delete;

	void Set(GLLegacyTexture* texture, GLLegacyTexture* brightMapTexture = nullptr)
	{
		Texture = texture;
		BrightMapTexture = brightMapTexture;
	}

	void Clear()
	{
		Pipeline.Clear();
	}

	bool Draw()
	{
		if (Pipeline.Empty() || Texture == nullptr)
			return false;

		Texture->Bind(BindTextures::BoundTexture);
		if (BrightMapTexture != nullptr)
			BrightMapTexture->Bind(BindTextures::BrightmapTexture);
		else
			GLLegacyTexture::Unbind(BindTextures::BrightmapTexture);

		Pipeline.Bind(true);
		Pipeline.Vbo.Upload();

		if constexpr (requires { TVertex::IsInstanced; })
		{
			Pipeline.Vbo.BindInstance();
			Pipeline.Vbo.DrawArraysInstanced(Util::PrimitiveType::TriangleStrip, 0, 4);
		}
		else
		{
			Pipeline.DrawArrays();
		}

		GLLegacyTexture::Unbind(BindTextures::BoundTexture);
		return true;
	}
};

}
