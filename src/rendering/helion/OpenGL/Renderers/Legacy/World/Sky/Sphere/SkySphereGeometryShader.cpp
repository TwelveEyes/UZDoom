/*
** SkySphereGeometryShader.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkySphereGeometryShader.
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

#include "SkySphereGeometryShader.h"
#include "SkyGeometryVertex.h"
#include "OpenGL/Vertex/Attributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

void SkySphereGeometryShader::ProgramCreated()
{
	m_mvpLocation = Uniforms.GetLocation("mvp");
	m_timeFracLocation = Uniforms.GetLocation("timeFrac");
}

const TArray<Vertex::VaoAttribute>* SkySphereGeometryShader::VertexLayout() const
{
	return &Vertex::Attributes::ReadStructAttributes<SkyGeometryVertex>();
}

FString SkySphereGeometryShader::VertexShader()
{
	return R"GLSL(
        #version 330

        layout(location = 0) in vec3 pos;
        layout(location = 1) in float prevZ;

        uniform mat4 mvp;
        uniform vec4 helionClipPlane;
        uniform float timeFrac;

        void main() {
            float z = mix(prevZ, pos.z, timeFrac);
            gl_Position = mvp * vec4(pos.x, pos.y, z, 1.0);
            gl_ClipDistance[0] = dot(helionClipPlane, vec4(pos.x, pos.y, z, 1.0));
            #ifdef VULKAN_COORDINATE_SYSTEM
            gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
            #endif
        }
    )GLSL";
}

std::optional<FString> SkySphereGeometryShader::FragmentShader()
{
	return FString(R"GLSL(
        #version 330

        out vec4 fragColor;

        void main() {
            fragColor = vec4(1.0, 1.0, 1.0, 1.0);
        }
    )GLSL");
}

}
