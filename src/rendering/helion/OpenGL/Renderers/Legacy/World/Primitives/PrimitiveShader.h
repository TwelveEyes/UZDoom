/*
** PrimitiveShader.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Primitives.PrimitiveShader.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion draws wide lines with GL.LineWidth, which the render state does not have (and core GL / Vulkan
** do not guarantee). The program has a `lineOffset` uniform instead: an offset in NDC units that is added
** to the clip position, and PrimitiveWorldRenderer draws the lines once per pixel offset of the line width.
**
*/

#pragma once

#include <optional>
#include "OpenGL/Shader/RenderProgram.h"
#include "Util/Geometry.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Primitives
{

// Create with RenderProgram::Create<PrimitiveShader>().
class PrimitiveShader : public ::Helion::Render::OpenGL::Shader::RenderProgram
{
public:
	PrimitiveShader() : RenderProgram("Primitive") { }

	void Mvp(const ::Helion::Geometry::mat4& mat) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(mat, m_mvpLocation); }
	// UZDoom-only, see the file comment.
	void LineOffset(const ::Helion::Geometry::Vec2F& offset) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(offset, m_lineOffsetLocation); }

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;
	void ProgramCreated() override;

private:
	int m_mvpLocation = -1;
	int m_lineOffsetLocation = -1;
};

}
