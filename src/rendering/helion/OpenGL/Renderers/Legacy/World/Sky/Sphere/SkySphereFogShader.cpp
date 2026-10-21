/*
** SkySphereFogShader.cpp
**
** UZDoom-only: the sky fog layer (UZDoom HWSkyPortal::DrawContents fog / skymist dome).
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

#include "SkySphereFogShader.h"
#include "SkySphereVertex.h"
#include "OpenGL/Renderers/Legacy/World/Shader/FragFunction.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderText.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderVars.h"
#include "OpenGL/Vertex/Attributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

using namespace Shader;

void SkySphereFogShader::ProgramCreated()
{
	m_boundTextureLocation = Uniforms.GetLocation("boundTexture");
	m_mvpLocation = Uniforms.GetLocation("mvp");
	m_flipULocation = Uniforms.GetLocation("flipU");
	m_fogColorLocation = Uniforms.GetLocation("fogColor");
	m_useTextureLocation = Uniforms.GetLocation("useTexture");
	m_mistParamsLocation = Uniforms.GetLocation("mistParams");
	m_colorMixLocation = Uniforms.GetLocation("colorMix");
	m_gammaCorrectionLocation = Uniforms.GetLocation("gammaCorrection");
}

const TArray<Vertex::VaoAttribute>* SkySphereFogShader::VertexLayout() const
{
	return &Vertex::Attributes::ReadStructAttributes<SkySphereVertex>();
}

FString SkySphereFogShader::VertexShader()
{
	return R"GLSL(
        #version 330

        layout(location = 0) in vec3 pos;
        layout(location = 1) in vec2 uv;

        out vec2 uvFrag;

        uniform mat4 mvp;
        uniform int flipU;

        void main() {
            uvFrag = uv;
            if (flipU == 1)
                uvFrag.x = -uvFrag.x;
            gl_Position = mvp * vec4(pos, 1.0);
            #ifdef VULKAN_COORDINATE_SYSTEM
            gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
            #endif
        }
    )GLSL";
}

std::optional<FString> SkySphereFogShader::FragmentShader()
{
	// The index texture of the palette color mode cannot be tinted: the fog is solid there.
	bool texture = !ShaderVars::PaletteColorMode;

	FString text = R"GLSL(
        #version 330

        in vec2 uvFrag;

        out vec4 fragColor;

        uniform vec4 fogColor;
        uniform vec4 mistParams;
        uniform int useTexture;
        uniform vec3 colorMix;
        uniform float gammaCorrection;
        ${TextureDeclaration}

        void main() {
            fragColor = fogColor;
            ${TextureSample}
            fragColor.rgb *= min(colorMix, 1);
            ${GammaCorrection}
        }
    )GLSL";

	text = Replace(text, "${TextureDeclaration}", texture ? "uniform sampler2DArray boundTexture;" : "");
	// The texture matrix of UZDoom's RenderDome (scale, then translate by (1, yoffset / height)); the dome v
	// runs 0..1 from 60 degrees up to the horizon, which is 3 * uv.y - 0.5 on the sphere.
	text = Replace(text, "${TextureSample}", texture ? R"GLSL(
            if (useTexture != 0) {
                vec2 textureUV = vec2(mistParams.x * (uvFrag.x + 0.5 + mistParams.w), mistParams.y * (3.0 * uvFrag.y - 0.5 + mistParams.z));
                fragColor *= texture(boundTexture, vec3(textureUV, 0));
            })GLSL" : "");
	text = Replace(text, "${GammaCorrection}", Shader::FragFunction::GammaCorrection());
	return text;
}

}
