/*
** PortalStencilProgram.cpp
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
*/

#include "PortalStencilProgram.h"
#include "PortalStencilVertex.h"
#include "OpenGL/Vertex/Attributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

void PortalStencilProgram::ProgramCreated()
{
	m_mvpLocation = Uniforms.GetLocation("mvp");
}

void PortalStencilProgram::SetMvp(const ::Helion::Geometry::mat4& mvp)
{
	::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(mvp, m_mvpLocation);
}

const TArray<Vertex::VaoAttribute>* PortalStencilProgram::VertexLayout() const
{
	return &Vertex::Attributes::ReadStructAttributes<PortalStencilVertex>();
}

FString PortalStencilProgram::VertexShader()
{
	return R"GLSL(
        #version 330

        layout(location = 0) in vec3 pos;

        uniform mat4 mvp;
        uniform vec4 helionClipPlane;

        void main()
        {
            gl_Position = mvp * vec4(pos, 1.0);
            gl_ClipDistance[0] = dot(helionClipPlane, vec4(pos, 1.0));
            #ifdef VULKAN_COORDINATE_SYSTEM
            gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
            #endif
        }
    )GLSL";
}

std::optional<FString> PortalStencilProgram::FragmentShader()
{
	return FString(R"GLSL(
        #version 330

        out vec4 fragColor;

        void main()
        {
            fragColor = vec4(1, 1, 1, 1);
        }
    )GLSL");
}

}
