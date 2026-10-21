/*
** CoronaProgram.cpp
**
** UZDoom-only (plan M4.1), see CoronaProgram.h.
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

#include "CoronaProgram.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderVars.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderText.h"
#include "OpenGL/Vertex/Attributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

using ::Helion::Render::OpenGL::Shader::ProgramUniforms;

CoronaProgram::CoronaProgram() : RenderProgramBase("World Corona")
{
}

const TArray<Vertex::VaoAttribute>* CoronaProgram::VertexLayout() const
{
	return &Vertex::Attributes::ReadStructAttributes<CoronaVertex>();
}

void CoronaProgram::ProgramCreated()
{
	RenderProgramBase::ProgramCreated();
	m_layerLocation = Uniforms.GetLocation("coronaLayer");
	m_alphaLocation = Uniforms.GetLocation("coronaAlpha");
}

void CoronaProgram::CoronaLayer(int layer) { ProgramUniforms::Set(layer, m_layerLocation); }
void CoronaProgram::CoronaAlpha(float alpha) { ProgramUniforms::Set(alpha, m_alphaLocation); }

FString CoronaProgram::VertexShader()
{
	return R"GLSL(
        #version 330

        layout(location = 0) in vec2 pos;
        layout(location = 1) in vec2 uv;

        out vec2 uvFrag;

        void main() {
            uvFrag = uv;
            gl_Position = vec4(pos, 0.0, 1.0);
        }
    )GLSL";
}

std::optional<FString> CoronaProgram::FragmentShader()
{
	// A corona is always fullbright (SetSoftLightLevel(255) in the original): light level 0 of the colormap.
	FString colorMapFetch;
	if (Shader::ShaderVars::PaletteColorMode)
	{
		colorMapFetch = R"GLSL(
                const int paletteSize = 256 * 34;
                float colormapFetchFlag = float(fragColor.w == 0.9960784313725490196078431372549);
                fragColor.w = mix(fragColor.w, 1, colormapFetchFlag);
                int texIndex = (paletteIndex * paletteSize) + int(fragColor.r * 255.0);
                vec3 fetchColor = texelFetch(colormapTexture, texIndex).rgb;
                fragColor.rgb = mix(fragColor.rgb, fetchColor, vec3(colormapFetchFlag));
)GLSL";
	}

	FString text = R"GLSL(
            #version 330

            in vec2 uvFrag;

            out vec4 fragColor;

            uniform sampler2DArray boundTexture;
            uniform samplerBuffer colormapTexture;
            uniform int paletteIndex;
            uniform int coronaLayer;
            uniform float coronaAlpha;

            void main() {
                fragColor = texture(boundTexture, vec3(uvFrag, float(coronaLayer)));
                ${ColorMapFetch}
                fragColor.a *= coronaAlpha;
                if (fragColor.a <= 0.0)
                    discard;
            }
        )GLSL";

	return Shader::Replace(text, "${ColorMapFetch}", colorMapFetch);
}

}
