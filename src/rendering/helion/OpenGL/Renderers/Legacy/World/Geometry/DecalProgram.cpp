/*
** DecalProgram.cpp
**
** UZDoom-only (plan M4.5), see DecalProgram.h.
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

#include "DecalProgram.h"
#include "DecalVertex.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderVars.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderText.h"
#include "OpenGL/Renderers/Legacy/World/Shader/VertexFunction.h"
#include "OpenGL/Renderers/Legacy/World/Shader/FragFunction.h"
#include "OpenGL/Renderers/Legacy/World/Shader/LightLevel.h"
#include "OpenGL/Renderers/Legacy/World/Shader/SectorColorMap.h"
#include "OpenGL/Renderers/Legacy/World/Shader/PlaneClip.h"
#include "OpenGL/Renderers/Legacy/World/Shader/UzdoomContract.h"
#include "OpenGL/Vertex/Attributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

using namespace Shader;
using ::Helion::Render::OpenGL::Shader::ProgramUniforms;

DecalProgram::DecalProgram() : RenderProgramBase("World Decal")
{
}

const TArray<Vertex::VaoAttribute>* DecalProgram::VertexLayout() const
{
	return &Vertex::Attributes::ReadStructAttributes<DecalVertex>();
}

void DecalProgram::ProgramCreated()
{
	RenderProgramBase::ProgramCreated();

	m_alphaLocation = Uniforms.GetLocation("decalAlpha");
	m_layerLocation = Uniforms.GetLocation("decalLayer");
	m_lightBufferIndexLocation = Uniforms.GetLocation("decalLightBufferIndex");
	m_lightOffsetLocation = Uniforms.GetLocation("decalLightOffset");
	m_blackFogLocation = Uniforms.GetLocation("decalBlackFog");
	m_decolorizeLocation = Uniforms.GetLocation("decalDecolorize");
	m_objectColorLocation = Uniforms.GetLocation("decalObjectColor");
	m_textureModeLocation = Uniforms.GetLocation("decalTextureMode");
	m_alphaThresholdLocation = Uniforms.GetLocation("decalAlphaThreshold");
}

void DecalProgram::DecalAlpha(float alpha) { ProgramUniforms::Set(alpha, m_alphaLocation); }
void DecalProgram::DecalLayer(int layer) { ProgramUniforms::Set(layer, m_layerLocation); }
void DecalProgram::DecalLightBufferIndex(int index) { ProgramUniforms::Set(index, m_lightBufferIndexLocation); }
void DecalProgram::DecalLightOffset(float offset) { ProgramUniforms::Set(offset, m_lightOffsetLocation); }
void DecalProgram::DecalBlackFog(bool value) { ProgramUniforms::Set(value, m_blackFogLocation); }
void DecalProgram::DecalDecolorize(bool value) { ProgramUniforms::Set(value, m_decolorizeLocation); }

void DecalProgram::DecalStyle(int textureMode, const FVector4& objectColor, float alphaThreshold)
{
	ProgramUniforms::Set(textureMode, m_textureModeLocation);
	ProgramUniforms::Set(objectColor, m_objectColorLocation);
	ProgramUniforms::Set(alphaThreshold, m_alphaThresholdLocation);
}

FString DecalProgram::VertexShader()
{
	FString text = R"GLSL(
        #version 330

        layout(location = 0) in vec3 pos;
        layout(location = 1) in vec2 uv;

        out vec2 uvFrag;
        flat out float alphaFrag;
        flat out float colorMapIndexFrag;
        flat out float vertexLightLevelFrag;
        flat out float boundTextureIndex;
        out vec3 worldPosFrag;
        ${VertexDistVar3D}

        ${SectorColorMapVertexFragVariables}
        ${LightLevelVertexVariables}
        ${VertexLightBufferVariables}
        ${SectorColorMapVertexUniformVariables}

        uniform mat4 mvp;
        uniform vec4 helionClipPlane;
        uniform int useSectorColor;
        uniform int useSectorFog;
        uniform float decalAlpha;
        uniform int decalLayer;
        uniform int decalLightBufferIndex;
        uniform float decalLightOffset;
        uniform int decalBlackFog;
        uniform int decalDecolorize;

        void main() {
            uvFrag = uv;
            alphaFrag = decalAlpha;
            boundTextureIndex = float(decalLayer);
            vertexLightLevelFrag = decalLightOffset;
            float lightLevelBufferIndex = float(decalLightBufferIndex);
            colorMapIndexFrag = lightLevelBufferIndex;
            float lightLevelAddValue = 0.0;

            vec4 mixPos = vec4(pos, 1.0);
            // The world position in the frame of the contract's pixelpos / lights[] (x, height, y).
            worldPosFrag = vec3(mixPos.x, mixPos.z, mixPos.y);

            ${SectorColorMapVertexFunction}
            // Additively drawn decals must not apply the fog colour a second time.
            if (decalBlackFog == 1)
                sectorFogColorFrag.rgb = vec3(0.0);
            // LEVEL3_NOCOLOREDSPRITELIGHTING: PalEntry::Decolorize on a true color mix (b >= 0; b < 0 is a colormap index).
            if (decalDecolorize == 1 && sectorColorMapIndexFrag.b >= 0.0)
                sectorColorMapIndexFrag = vec3((3.0 + 2.0 * (sectorColorMapIndexFrag.r + sectorColorMapIndexFrag.g + sectorColorMapIndexFrag.b)) / 9.0);
            ${VertexLightBuffer}
            ${LightLevelVertexDist}
            ${SetVertexDist3D}
            gl_Position = mvp * mixPos;
            gl_ClipDistance[0] = dot(helionClipPlane, mixPos);
            #ifdef VULKAN_COORDINATE_SYSTEM
            gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
            #endif
        }
    )GLSL";

	text = Replace(text, "${LightLevelVertexVariables}", LightLevel::VertexVariables(LightLevelOptions::Default));
	text = Replace(text, "${VertexLightBufferVariables}", LightLevel::VertexLightBufferVariables());
	text = Replace(text, "${VertexLightBuffer}", LightLevel::VertexLightBuffer(VertexLightBufferOptions::LightLevelAdd));
	text = Replace(text, "${LightLevelVertexDist}", LightLevel::VertexDist("mixPos"));
	text = Replace(text, "${SetVertexDist3D}", VertexFunction::SetVertexDist3D("mixPos"));
	text = Replace(text, "${SectorColorMapVertexFragVariables}", SectorColorMap::VertexFragVariables());
	text = Replace(text, "${SectorColorMapVertexUniformVariables}", SectorColorMap::VertexUniformVariables());
	text = Replace(text, "${SectorColorMapVertexFunction}", SectorColorMap::VertexFunctionWorld());
	text = Replace(text, "${VertexDistVar3D}", VertexFunction::VertexDistVar3D());
	return text;
}

std::optional<FString> DecalProgram::FragmentShader()
{
	bool palette = ShaderVars::PaletteColorMode;

	FString text = R"GLSL(
            #version 330

            in vec2 uvFrag;
            flat in float alphaFrag;
            flat in float boundTextureIndex;
            in vec3 worldPosFrag;
            ${VertexDistVar3D}

            ${OutTargets}

            uniform int hasInvulnerability;
            uniform sampler2DArray boundTexture;
            ${BrightmapUniform}
            uniform vec3 colorMix;
            uniform int paletteIndex;
            uniform int colormapIndex;
            uniform int useBrightmaps;
            uniform int useSectorFog;

            ${LightLevelFragVariables}
            ${SectorColorMapFragVariables}
            ${DecalVariables}
            ${UzdoomPrelude}

            void main() {
                float colorClamp = 1;
                ${LightLevelFragFunction}
                ${SectorColorMapFragFunction}
                ${FragColorFunction}
            }
        )GLSL";

	FragColorFunctionOptions options = FragColorFunctionOptions::Alpha;
	if (palette)
		options = options | FragColorFunctionOptions::Colormap | FragColorFunctionOptions::Brightmaps;
	else
		options = options | FragColorFunctionOptions::UzdoomLightMode | FragColorFunctionOptions::DynamicLights;

	// The texture modes of getTexel (main.fp) change the texel before the light; palette textures hold an index, so there
	// the modes can only act on the colour after the colormap fetch.
	FString decalVariables = "uniform vec4 decalObjectColor;\n            uniform int decalTextureMode;\n            uniform float decalAlphaThreshold;\n";
	FString postProcess;
	if (!palette)
	{
		decalVariables << R"GLSL(
            vec4 decalTexel(vec3 coord)
            {
                vec4 texel = texture(boundTexture, coord);
                switch (decalTextureMode)
                {
                    case 1: // TM_STENCIL
                        texel.rgb = vec3(1.0);
                        break;
                    case 2: // TM_OPAQUE
                        texel.a = 1.0;
                        break;
                    case 3: // TM_INVERSE
                        texel = vec4(1.0 - texel.r, 1.0 - texel.b, 1.0 - texel.g, texel.a);
                        break;
                    case 4: // TM_ALPHATEXTURE
                    {
                        float gray = dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722));
                        texel = vec4(1.0, 1.0, 1.0, gray * texel.a);
                        break;
                    }
                    case 5: // TM_CLAMPY
                        break;
                    case 6: // TM_OPAQUEINVERSE
                        texel = vec4(1.0 - texel.r, 1.0 - texel.b, 1.0 - texel.g, 1.0);
                        break;
                }
                return texel * decalObjectColor;
            }
)GLSL";
	}
	else
	{
		postProcess = R"GLSL(
                switch (decalTextureMode)
                {
                    case 1: // TM_STENCIL
                        fragColor.rgb = decalObjectColor.rgb;
                        break;
                    case 3: // TM_INVERSE
                    case 6: // TM_OPAQUEINVERSE
                        fragColor.rgb = vec3(1.0) - fragColor.rgb;
                        fragColor.rgb *= decalObjectColor.rgb;
                        break;
                    case 4: // TM_ALPHATEXTURE
                        fragColor.a *= dot(fragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
                        fragColor.rgb = decalObjectColor.rgb;
                        break;
                    default:
                        fragColor.rgb *= decalObjectColor.rgb;
                        break;
                }
                fragColor.a *= decalObjectColor.a;
)GLSL";
	}
	postProcess << "                if (fragColor.a < decalAlphaThreshold)\n                    discard;\n";

	FString colorFunction = FragFunction::FragColorFunction(options, ColorMapFetchContext::Default, OitOptions::None, postProcess);
	if (!palette)
		colorFunction = Replace(colorFunction, "= texture(boundTexture, vec3(texUV, boundTextureIndex));", "= decalTexel(vec3(texUV, boundTextureIndex));");

	text = Replace(text, "${LightLevelFragFunction}", LightLevel::FragFunction());
	text = Replace(text, "${LightLevelFragVariables}", LightLevel::FragVariables(LightLevelOptions::Default));
	text = Replace(text, "${FragColorFunction}", colorFunction);
	text = Replace(text, "${SectorColorMapFragVariables}", SectorColorMap::FragVariables());
	text = Replace(text, "${SectorColorMapFragFunction}", SectorColorMap::FragFunction());
	text = Replace(text, "${OutTargets}", PlaneClip::GetOutTargets(false));
	text = Replace(text, "${BrightmapUniform}", palette ? "uniform sampler2DArray brightmapTexture;" : "");
	text = Replace(text, "${VertexDistVar3D}", FragFunction::VertexDistVar3D());
	text = Replace(text, "${DecalVariables}", decalVariables);
	text = Replace(text, "${UzdoomPrelude}", palette ? FString() : UzdoomContract::LightModePrelude(true));
	return text;
}

}
