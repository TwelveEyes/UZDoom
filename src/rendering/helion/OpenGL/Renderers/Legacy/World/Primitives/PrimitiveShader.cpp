/*
** PrimitiveShader.cpp
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
*/

#include "PrimitiveShader.h"
#include "PrimitiveVertex.h"
#include "OpenGL/Vertex/Attributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Primitives
{

const TArray<Vertex::VaoAttribute>* PrimitiveShader::VertexLayout() const
{
	return &Vertex::Attributes::ReadStructAttributes<PrimitiveVertex>();
}

void PrimitiveShader::ProgramCreated()
{
	m_mvpLocation = Uniforms.GetLocation("mvp");
	m_lineOffsetLocation = Uniforms.GetLocation("lineOffset");
}

FString PrimitiveShader::VertexShader()
{
	return R"(
        #version 330

        layout(location = 0) in vec3 pos;
        layout(location = 1) in vec3 rgb;
        layout(location = 2) in float alpha;

        flat out vec3 rgbFrag;
        flat out float alphaFrag;

        uniform mat4 mvp;
        uniform vec2 lineOffset;

        void main() 
        {
            rgbFrag = rgb;
            alphaFrag = alpha;

            gl_Position = mvp * vec4(pos, 1.0);
            gl_Position.xy += lineOffset * gl_Position.w;
        }
    )";
}

std::optional<FString> PrimitiveShader::FragmentShader()
{
	return FString(R"(
        #version 330

        flat in vec3 rgbFrag;
        flat in float alphaFrag;

        out vec4 fragColor;

        void main() 
        {
            fragColor = vec4(rgbFrag, alphaFrag);
        }
    )");
}

}
