/*
** FramebufferRenderer.cpp
**
** Port of Helion.Render.OpenGL.Renderers.FramebufferRenderer.
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

#include "FramebufferRenderer.h"
#include "BindTextures.h"
#include "OpenGL/Vertex/Attributes.h"
#include "v_video.h"
#include "OpenGL/Shader/ProgramRegistry.h"

namespace Helion::Render::OpenGL::Renderers
{

const TArray<Vertex::VaoAttribute>* FramebufferProgram::VertexLayout() const
{
	return &Vertex::Attributes::ReadStructAttributes<FramebufferVertex>();
}

void FramebufferProgram::ProgramCreated()
{
	m_boundTextureLocation = Uniforms.GetLocation("boundTexture");
	m_mvpLocation = Uniforms.GetLocation("mvp");
}

FString FramebufferProgram::VertexShader()
{
	return R"(
        #version 330

        layout(location = 0) in vec2 pos;
        layout(location = 1) in vec2 uv;

        out vec2 uvFrag;

        uniform mat4 mvp;

        void main()
        {
            uvFrag = uv;

            gl_Position = mvp * vec4(pos, 0, 1);
        }
    )";
}

std::optional<FString> FramebufferProgram::FragmentShader()
{
	return FString(R"(
        #version 330

        in vec2 uvFrag;

        out vec4 fragColor;

        uniform sampler2D boundTexture;

        void main()
        {
            fragColor = texture(boundTexture, uvFrag.st);
        }
    )");
}

FramebufferRenderer::FramebufferRenderer()
{
	// HelionProgramRegistry owns the program (compiled at startup, plan D22).
	m_program = Shader::HelionProgramRegistry::Get<FramebufferProgram>(Shader::ProgramType::Framebuffer);
	m_vbo = std::make_unique<Buffer::Array::Vertex::StaticVertexBuffer<FramebufferVertex>>("Framebuffer");
	m_pipeline = std::make_unique<VertexPipeline<FramebufferVertex>>(*m_program, *m_vbo, "Framebuffer");
	UploadVertices();
}

FramebufferRenderer::~FramebufferRenderer() = default;

void FramebufferRenderer::UploadVertices()
{
	FramebufferVertex topLeft(Geometry::Vec2F(-1, 1), Geometry::Vec2F(0, 1));
	FramebufferVertex topRight(Geometry::Vec2F(1, 1), Geometry::Vec2F(1, 1));
	FramebufferVertex bottomLeft(Geometry::Vec2F(-1, -1), Geometry::Vec2F(0, 0));
	FramebufferVertex bottomRight(Geometry::Vec2F(1, -1), Geometry::Vec2F(1, 0));

	m_pipeline->Vbo.Add(topLeft);
	m_pipeline->Vbo.Add(bottomLeft);
	m_pipeline->Vbo.Add(topRight);
	m_pipeline->Vbo.Add(topRight);
	m_pipeline->Vbo.Add(bottomLeft);
	m_pipeline->Vbo.Add(bottomRight);
	m_pipeline->Vbo.Upload();
}

void FramebufferRenderer::Render(const Framebuffer::GLFramebuffer& buffer, const Geometry::mat4& mvp)
{
	m_program->Bind();
	screen->RenderState()->DisableBlend();

	buffer.ColorAttachment0.Bind(BindTextures::BoundTexture);
	m_program->BoundTexture(BindTextures::BoundTexture);
	m_program->Mvp(mvp);

	m_pipeline->Bind();
	m_pipeline->DrawArrays();
	m_pipeline->Unbind();

	m_program->Unbind();
}

}
