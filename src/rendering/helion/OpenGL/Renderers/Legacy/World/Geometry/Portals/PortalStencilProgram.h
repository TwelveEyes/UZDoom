/*
** PortalStencilProgram.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Portals.PortalStencilProgram.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Create with RenderProgram::Create<PortalStencilProgram>() (HelionProgramRegistry: ProgramType::PortalStencil).
**
*/

#pragma once

#include "OpenGL/Shader/RenderProgram.h"
#include "Util/Geometry.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

/// <summary>
/// A simple program designed just to render primitives so that the stencil
/// buffer gets populated.
/// </summary>
class PortalStencilProgram : public ::Helion::Render::OpenGL::Shader::RenderProgram
{
public:
	PortalStencilProgram() : RenderProgram("Portal stencil")
	{
	}

	void SetMvp(const ::Helion::Geometry::mat4& mvp);

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;
	void ProgramCreated() override;

private:
	int m_mvpLocation = -1;
};

}
