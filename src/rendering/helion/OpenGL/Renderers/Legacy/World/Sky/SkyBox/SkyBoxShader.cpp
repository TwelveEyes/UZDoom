/*
** SkyBoxShader.cpp
**
** UZDoom-only: draws the faces of a sky box (FSkyBox, GLDEFS skybox).
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

#include "SkyBoxShader.h"
#include "OpenGL/Renderers/Legacy/World/Sky/Sphere/SkySphereVertex.h"
#include "OpenGL/Renderers/Legacy/World/Shader/FragFunction.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderText.h"
#include "OpenGL/Vertex/Attributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::SkyBox
{

using namespace Shader;

void SkyBoxShader::ProgramCreated()
{
	m_boundTextureLocation = Uniforms.GetLocation("boundTexture");
	m_colormapTextureLocation = Uniforms.GetLocation("colormapTexture");
	m_mvpLocation = Uniforms.GetLocation("mvp");
	m_hasInvulnerabilityLocation = Uniforms.GetLocation("hasInvulnerability");
	m_paletteIndexLocation = Uniforms.GetLocation("paletteIndex");
	m_colorMapIndexLocation = Uniforms.GetLocation("colormapIndex");
	m_colorMixLocation = Uniforms.GetLocation("colorMix");
	m_gammaCorrectionLocation = Uniforms.GetLocation("gammaCorrection");
}

const TArray<Vertex::VaoAttribute>* SkyBoxShader::VertexLayout() const
{
	return &Vertex::Attributes::ReadStructAttributes<Sphere::SkySphereVertex>();
}

FString SkyBoxShader::VertexShader()
{
	return R"GLSL(
        #version 330

        layout(location = 0) in vec3 pos;
        layout(location = 1) in vec2 uv;

        out vec2 uvFrag;

        uniform mat4 mvp;

        void main() {
            uvFrag = uv;
            gl_Position = mvp * vec4(pos, 1.0);
            #ifdef VULKAN_COORDINATE_SYSTEM
            gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
            #endif
        }
    )GLSL";
}

std::optional<FString> SkyBoxShader::FragmentShader()
{
	FString text = R"GLSL(
        #version 330

        in vec2 uvFrag;

        out vec4 fragColor;

        uniform sampler2DArray boundTexture;
        uniform samplerBuffer colormapTexture;
        uniform int hasInvulnerability;
        uniform int paletteIndex;
        uniform int colormapIndex;
        uniform vec3 colorMix;
        uniform float gammaCorrection;

        void main() {
            fragColor = texture(boundTexture, vec3(uvFrag, 0));

            ${ColorMapFetch}
            fragColor.a = 1;
            fragColor.xyz *= min(colorMix, 1);
            ${InvulnerabilityFragColor}
            ${GammaCorrection}
        }
    )GLSL";

	text = Replace(text, "${InvulnerabilityFragColor}", FragFunction::InvulnerabilityFragColor());
	text = Replace(text, "${ColorMapFetch}", FragFunction::ColorMapFetch(false, ColorMapFetchContext::Default));
	text = Replace(text, "${GammaCorrection}", FragFunction::GammaCorrection());
	return text;
}

}
