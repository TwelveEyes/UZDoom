/*
** FramebufferRenderer.h
**
** Port of Helion.Render.OpenGL.Renderers.FramebufferRenderer (and its FramebufferProgram / FramebufferVertex).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Draws the colour attachment of a GLFramebuffer as a full screen quad into the current render
** target. The blit overwrites (blending is disabled for it); depth test and depth writes are left to
** the caller, like the viewport. For the scene framebuffer the source is the CopySceneColor() copy.
**
*/

#pragma once

#include <memory>
#include <optional>
#include <span>
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/Framebuffer/GLFramebuffer.h"
#include "OpenGL/Shader/RenderProgram.h"
#include "OpenGL/Vertex/VertexAttributes.h"
#include "OpenGL/VertexPipeline.h"
#include "Util/Geometry.h"

namespace Helion::Render::OpenGL::Renderers
{

#pragma pack(push, 1)
struct FramebufferVertex
{
	// [VertexAttribute]
	Geometry::Vec2F Pos;
	// [VertexAttribute]
	Geometry::Vec2F UV;

	FramebufferVertex() = default;
	FramebufferVertex(const Geometry::Vec2F& pos, const Geometry::Vec2F& uv) : Pos(pos), UV(uv) { }

	static std::span<const Vertex::VertexAttributeAttribute> VertexAttributes()
	{
		static constexpr Vertex::VertexAttributeAttribute attributes[] =
		{
			{ "pos", Vertex::VertexAttributeAttribute::InferFromField, 2 },
			{ "uv", Vertex::VertexAttributeAttribute::InferFromField, 2 },
		};
		return attributes;
	}
};
#pragma pack(pop)

static_assert(sizeof(FramebufferVertex) == 4 * sizeof(float), "FramebufferVertex has to stay tightly packed");

// Create with RenderProgram::Create<FramebufferProgram>().
class FramebufferProgram : public Shader::RenderProgram
{
public:
	FramebufferProgram() : RenderProgram("Framebuffer") { }

	void BoundTexture(TextureUnit unit) { Shader::ProgramUniforms::Set(unit, m_boundTextureLocation); }
	void Mvp(const Geometry::mat4& mvp) { Shader::ProgramUniforms::Set(mvp, m_mvpLocation); }

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;
	void ProgramCreated() override;

private:
	int m_boundTextureLocation = -1;
	int m_mvpLocation = -1;
};

class FramebufferRenderer
{
public:
	FramebufferRenderer();
	~FramebufferRenderer();

	FramebufferRenderer(const FramebufferRenderer&) = delete;
	FramebufferRenderer& operator=(const FramebufferRenderer&) = delete;

	void Render(const Framebuffer::GLFramebuffer& buffer, const Geometry::mat4& mvp);

private:
	FramebufferProgram* m_program = nullptr;	// HelionProgramRegistry
	std::unique_ptr<Buffer::Array::Vertex::StaticVertexBuffer<FramebufferVertex>> m_vbo;
	std::unique_ptr<VertexPipeline<FramebufferVertex>> m_pipeline;

	void UploadVertices();
};

}
