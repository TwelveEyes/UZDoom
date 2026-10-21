/*
** StaticShader.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.StaticShader.
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

#include "StaticShader.h"
#include "StaticVertex.h"
#include "StaticWallClipShader.h"
#include "StaticWallClipAlphaShader.h"
#include "StaticPlaneClipShader.h"
#include "StaticPlaneClipAlphaShader.h"
#include "StaticPlaneClipShaderMrt.h"
#include "StaticTransparentShader.h"
#include "StaticCompositeShader.h"
#include "Shader/ShaderVars.h"
#include "Shader/ShaderText.h"
#include "Shader/VertexFunction.h"
#include "Shader/FragFunction.h"
#include "Shader/LightLevel.h"
#include "Shader/SectorColorMap.h"
#include "Shader/PlaneClip.h"
#include "OpenGL/Vertex/Attributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

using namespace Shader;

static FString LabelOf(const FString& name, const MaterialProgramOptions& material)
{
	FString label;
	label << "WorldStatic - " << name;
	if (material.IsMaterial())
		label.AppendFormat(" - material %s%s", UzdoomContract::MaterialShaderName(material.MaterialShaderIndex).GetChars(), material.Pass == GBUFFER_PASS ? " (gbuffer)" : "");
	return label;
}

StaticShader::StaticShader(const FString& name, const MaterialProgramOptions& material) : RenderProgramBase(LabelOf(name, material)), m_material(material)
{
}

const TArray<Vertex::VaoAttribute>* StaticShader::VertexLayout() const
{
	return &Vertex::Attributes::ReadStructAttributes<StaticVertex>();
}

FString StaticShader::VertexShader()
{
	if (dynamic_cast<const StaticWallClipShader*>(this) != nullptr || dynamic_cast<const StaticWallClipAlphaShader*>(this) != nullptr ||
		dynamic_cast<const StaticPlaneClipShader*>(this) != nullptr || dynamic_cast<const StaticPlaneClipAlphaShader*>(this) != nullptr)
		return VertexShaderInternal(true);

	return VertexShaderInternal(false);
}

FString StaticShader::VertexShaderInternal(bool planeClip)
{
	// A material program samples through the mod shader contract, there is no array texture to take the gap from.
	bool gap = !planeClip && !m_material.IsMaterial();
	// Material programs are always truecolor (FragmentShader), the vertex stage has to match.
	PaletteColorModeScope colorMode(m_material.IsMaterial() ? false : ShaderVars::PaletteColorMode);

	FString text = R"GLSL(
        #version 330

        layout(location = 0) in vec3 pos;
        layout(location = 1) in vec2 uv;
        layout(location = 2) in float lightLevelAdd;
        layout(location = 3) in float surfaceOptions;
        layout(location = 4) in float renderOptions;
        layout(location = 5) in vec2 lightmapUV;
        layout(location = 6) in float lightmapIndex;

        out vec2 uvFrag;
        flat out float alphaFrag;
        flat out float addAlphaFrag;
        flat out float colorMapIndexFrag;
        flat out float uvFlags;
        flat out float vertexLightLevelFrag;
        flat out float zPos;
        flat out float mapIdFrag;
        flat out float upperFrag;
        flat out float lowerFrag;
        flat out float boundTextureIndex;
        out float depthFrag;
        ${WorldPosVariables}
        ${LightmapVariables}
        ${VertexGapVariables}
        ${VertexDistVar3D}

        ${SectorColorMapVertexFragVariables}
        ${LightLevelVertexVariables}
        ${VertexLightBufferVariables}
        ${SectorColorMapVertexUniformVariables}

        uniform mat4 mvp;
        uniform vec4 helionClipPlane;
        uniform float timeFrac;
        ${VertexGapUniforms}
        uniform int useSectorColor;
        uniform int useSectorFog;

        void main() {
            uvFrag = uv;

            ${VertexOptionsSet}
            ${ColorMapAndLightLevelSet}
            ${LightLevelAddAndMapIdSet}

            vec4 mixPos = vec4(pos, 1.0);
            ${WorldPosSet}
            ${LightmapSet}
            ${VertexGapSet}

            ${SectorColorMapVertexFunction}
            ${VertexLightBuffer}
            ${LightLevelVertexDist}
            ${SetVertexDist3D}
            gl_Position = mvp * mixPos;
            gl_ClipDistance[0] = dot(helionClipPlane, mixPos);
            zPos = pos.z;
            depthFrag = gl_Position.z;
            #ifdef VULKAN_COORDINATE_SYSTEM
            gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
            #endif
        }
    )GLSL";

	text = Replace(text, "${LightLevelVertexVariables}", planeClip ? "" : LightLevel::VertexVariables(LightLevelOptions::Default));
	text = Replace(text, "${VertexLightBufferVariables}", planeClip ? "" : LightLevel::VertexLightBufferVariables());
	text = Replace(text, "${VertexLightBuffer}", planeClip ? "" : LightLevel::VertexLightBuffer(VertexLightBufferOptions::LightLevelAdd));
	text = Replace(text, "${LightLevelVertexDist}", planeClip ? "" : LightLevel::VertexDist("mixPos"));
	text = Replace(text, "${SetVertexDist3D}", planeClip ? "" : VertexFunction::SetVertexDist3D("mixPos"));
	text = Replace(text, "${SectorColorMapVertexFragVariables}", planeClip ? "" : SectorColorMap::VertexFragVariables());
	text = Replace(text, "${SectorColorMapVertexUniformVariables}", planeClip ? "" : SectorColorMap::VertexUniformVariables());
	text = Replace(text, "${SectorColorMapVertexFunction}", planeClip ? "" : SectorColorMap::VertexFunctionWorld());
	text = Replace(text, "${VertexGapVariables}", gap ? VertexFunction::VertexGapVariables() : FString());
	text = Replace(text, "${VertexGapUniforms}", m_material.IsMaterial() ? "" : "uniform int vertexGapClampUV;\n        uniform sampler2DArray boundTexture;");
	text = Replace(text, "${VertexGapSet}", gap ? VertexFunction::VertexGapSet() : FString());
	text = Replace(text, "${ColorMapAndLightLevelSet}", planeClip ? "" : VertexFunction::ColorMapAndLightLevelSet());
	text = Replace(text, "${VertexDistVar3D}", planeClip ? "" : VertexFunction::VertexDistVar3D());
	text = Replace(text, "${VertexOptionsSet}", VertexFunction::VertexOptionsSet());
	text = Replace(text, "${LightLevelAddAndMapIdSet}", VertexFunction::LightLevelAddAndMapIdSet());
	// UZDoom-only: the world position in the frame of the contract's pixelpos / lights[] (x, height, y).
	text = Replace(text, "${WorldPosVariables}", planeClip ? "" : "out vec3 worldPosFrag;");
	text = Replace(text, "${LightmapVariables}", planeClip ? "" : "out vec3 lightmapFrag;");
	text = Replace(text, "${LightmapSet}", planeClip ? "" : "lightmapFrag = vec3(lightmapUV, lightmapIndex);");
	text = Replace(text, "${WorldPosSet}", planeClip ? "" : "worldPosFrag = vec3(mixPos.x, mixPos.z, mixPos.y);");
	return text;
}

std::optional<FString> StaticShader::FragmentShader()
{
	if (dynamic_cast<const StaticPlaneClipShader*>(this) != nullptr)
		return PlaneClip::WritePlaneFragFunction(PlaneClipFragOptions::None);

	if (dynamic_cast<const StaticPlaneClipAlphaShader*>(this) != nullptr)
		return PlaneClip::WritePlaneFragFunction(PlaneClipFragOptions::AlphaSample);

	if (dynamic_cast<const StaticWallClipShader*>(this) != nullptr)
		return PlaneClip::WriteWallFragFunction(WallClipFragOptions::None);

	if (dynamic_cast<const StaticWallClipAlphaShader*>(this) != nullptr)
		return PlaneClip::WriteWallFragFunction(WallClipFragOptions::AlphaSample);

	bool planeClip = dynamic_cast<const StaticPlaneClipShaderMrt*>(this) != nullptr;
	bool material = m_material.IsMaterial();

	// Material programs are always truecolor.
	PaletteColorModeScope colorMode(material ? false : ShaderVars::PaletteColorMode);
	bool uzdoomLightMode = !ShaderVars::PaletteColorMode && !material;

	FString text = R"GLSL(
            #version 330

            in vec2 uvFrag;
            flat in float alphaFrag;
            flat in float addAlphaFrag;
            flat in float zPos;
            flat in float distFrag;
            flat in float mapIdFrag;
            flat in float upperFrag;
            flat in float lowerFrag;
            flat in float boundTextureIndex;
            in float depthFrag;
            ${VertexGapVariables}
            ${VertexDistVar3D}

            ${OutTargets}

            uniform int hasInvulnerability;
            ${ArrayTextureUniforms}
            uniform vec3 colorMix;
            uniform int paletteIndex;
            uniform int colormapIndex;
            uniform int useBrightmaps;
            ${ClipUniforms}
            uniform int useSectorFog;

            ${LightLevelFragVariables}
            ${SectorColorMapFragVariables}
            ${OitVariables}
            ${UzdoomPrelude}

            void main() {
                float colorClamp = 1;
                ${TransparentDiscard}
                ${LightLevelFragFunction}
                ${SectorColorMapFragFunction}
                ${FragColorFunction}
                ${OutPlane}
                ${UzdoomMainEnd}
            }
        )GLSL";

	FragColorFunctionOptions options = FragColorFunctionOptions::AddAlpha | FragColorFunctionOptions::Colormap | FragColorFunctionOptions::VertexGapClampUV | FragColorFunctionOptions::Brightmaps;
	if (material)
		options = FragColorFunctionOptions::AddAlpha | FragColorFunctionOptions::UzdoomMaterial;
	else if (uzdoomLightMode)
		options = options | FragColorFunctionOptions::UzdoomLightMode;

	// Plan D25: the world surfaces get the clustered dynamic lights (both colour modes); the composite pass only resolves
	// the OIT buffers.
	bool dynamicLights = !material && !planeClip && GetOitOptions() != OitOptions::OitCompositePass;
	if (dynamicLights)
		options = options | FragColorFunctionOptions::DynamicLights;

	FString prelude;
	if (material)
		prelude = UzdoomContract::MaterialPrelude(m_material);
	else if (uzdoomLightMode || dynamicLights)
		prelude = UzdoomContract::LightModePrelude(dynamicLights);

	text = Replace(text, "${LightLevelFragFunction}", LightLevel::FragFunction());
	text = Replace(text, "${LightLevelFragVariables}", LightLevel::FragVariables(LightLevelOptions::Default));
	text = Replace(text, "${FragColorFunction}", FragFunction::FragColorFunction(options, ColorMapFetchContext::Default, GetOitOptions()));
	text = Replace(text, "${SectorColorMapFragVariables}", SectorColorMap::FragVariables());
	text = Replace(text, "${SectorColorMapFragFunction}", SectorColorMap::FragFunction());
	text = Replace(text, "${OitVariables}", FragFunction::OitFragVariables(GetOitOptions()));
	text = Replace(text, "${OutTargets}", GetOutTargets(planeClip));
	text = Replace(text, "${VertexGapVariables}", material ? FString() : FragFunction::VertexGapVariables());
	text = Replace(text, "${ArrayTextureUniforms}", material ? "" : "uniform sampler2DArray boundTexture;\n            uniform sampler2DArray brightmapTexture;");
	text = Replace(text, "${ClipUniforms}", material ? "" :
		"uniform sampler2D planeClipTexture;\n            uniform sampler2D wallClipTexture;\n            uniform int checkPlaneClip;\n            uniform float downScaleAmount;\n            uniform ivec2 screenBounds;");
	text = Replace(text, "${OutPlane}", PlaneClip::GetOutPlane(planeClip));
	text = Replace(text, "${TransparentDiscard}", PlaneClip::GetTransparentDiscard(GetOitOptions()));
	text = Replace(text, "${VertexDistVar3D}", FragFunction::VertexDistVar3D());
	text = Replace(text, "${UzdoomPrelude}", prelude);
	text = Replace(text, "${UzdoomMainEnd}", material ? UzdoomContract::PassMainEnd(m_material.Pass) : FString());
	return text;
}

OitOptions StaticShader::GetOitOptions() const
{
	if (dynamic_cast<const StaticTransparentShader*>(this) != nullptr)
		return OitOptions::OitTransparentPass;
	if (dynamic_cast<const StaticCompositeShader*>(this) != nullptr)
		return OitOptions::OitCompositePass;
	return OitOptions::None;
}

FString StaticShader::GetOutTargets(bool planeClip) const
{
	OitOptions options = GetOitOptions();
	if (options == OitOptions::OitTransparentPass)
		return "";

	FString targets = PlaneClip::GetOutTargets(planeClip);
	if (m_material.IsMaterial())
		targets << UzdoomContract::PassOutputs(m_material.Pass);
	return targets;
}

}
