/*
** ModelProgram.cpp
**
** UZDoom-only (plan M4.3): see ModelProgram.h.
**
** Vertex stage: main.vp's ApplyBones + frame interpolation, the model matrix takes the vertex to the UZDoom GL frame
** (x, height, y), which is converted to the Helion world frame (x, y, height) the mvp / mvpNoPitch of the Helion programs
** expect (same conversion as the other UZDoom programs, see StaticShader's worldPosFrag).
** The light level of the model is a uniform (like the light level of an EntityVertex), the sector colour and fog come from the
** sector colormap / fog buffers through the light buffer index of the sector (SectorColorMap::VertexFunction).
** Fragment stage: the StaticShader material path (FragColorFunction with UzdoomMaterial, the light level equations of
** LightLevel::FragFunction, sector colour and sector fog, invulnerability, gamma), except that the face normal that
** MaterialColor derives from the screen space derivatives is the interpolated vertex normal of the model (vWorldNormal of main.vp).
**
**---------------------------------------------------------------------------
**
** Copyright 2005-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include <cstddef>
#include "ModelProgram.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderVars.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderText.h"
#include "OpenGL/Renderers/Legacy/World/Shader/VertexFunction.h"
#include "OpenGL/Renderers/Legacy/World/Shader/FragFunction.h"
#include "OpenGL/Renderers/Legacy/World/Shader/LightLevel.h"
#include "OpenGL/Renderers/Legacy/World/Shader/SectorColorMap.h"
#include "OpenGL/Shader/ProgramUniforms.h"
#include "hwrenderer/data/buffers.h"
#include "hwrenderer/data/hw_bonebuffer.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterFragPrelude.h"
#include "hwrenderer/data/hw_lightbuffer.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "i_modelvertexbuffer.h"
#include "engineerrors.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

using namespace Shader;
using ::Helion::Render::OpenGL::Shader::ProgramUniforms;

namespace
{

FString LabelOf(const FString& name, const MaterialProgramOptions& material)
{
	FString label;
	label << "Model - " << name;
	if (material.IsMaterial())
		label.AppendFormat(" - material %s%s", UzdoomContract::MaterialShaderName(material.MaterialShaderIndex).GetChars(), material.Pass == GBUFFER_PASS ? " (gbuffer)" : "");
	return label;
}

MaterialProgramOptions WithMaterial(MaterialProgramOptions material)
{
	// Models always sample through the mod shader contract; no material shader selects the default one.
	if (!material.IsMaterial())
		material.MaterialShaderIndex = 0;
	material.ArrayTexture = false;
	// The per actor light list of hw_GetDynModelLight (ModelLightList), see ModelProgram::DrawLights.
	material.DrawLightList = true;
	return material;
}

// The bone buffer block of the table shaders (gl_shader.cpp): a shader storage buffer when the backend has one, else a
// uniform block of the same size. The renderer binds it with FRenderState::SetProgramBuffer (the type name contains "Buffer").
FString BoneBufferBlock()
{
	FString text;
	if (screen->mBones->GetBufferType())
		text << "buffer BoneBufferSSO\n{\n\tmat4 bones[];\n};\n";
	else
		text.AppendFormat("uniform BoneBufferUBO\n{\n\tmat4 bones[%d];\n};\n", screen->mBones->GetBlockSize());
	return text;
}

// The text of FragFunction::MaterialColor derives vWorldNormal from the derivatives of the position (flat shading), which is
// right for the world surfaces; the model has its vertex normals.
FString UseVertexNormal(FString text)
{
	static const char* const derived = "normalize(cross(dFdx(pixelpos.xyz), dFdy(pixelpos.xyz)))";
	static const char* const flip = "if (dot(faceNormal, uCameraPos.xyz - pixelpos.xyz) < 0.0)";
	if (text.IndexOf(derived) < 0 || text.IndexOf(flip) < 0)
		I_FatalError("ModelProgram: the material colour of FragFunction does not derive the face normal from the position derivatives any more");

	text = Replace(text, derived, "(dot(normalFrag, normalFrag) > 0.0 ? normalize(normalFrag) : vec3(0.0, 1.0, 0.0))");
	text = Replace(text, flip, "if (false)");
	return text;
}

}

ModelProgram::ModelProgram(const FString& name, const MaterialProgramOptions& material)
	: RenderProgramBase(LabelOf(name, WithMaterial(material))), m_material(WithMaterial(material))
{
}

void ModelProgram::ProgramCreated()
{
	RenderProgramBase::ProgramCreated();

	m_modelMatrixLocation = Uniforms.GetLocation("modelMatrix");
	m_normalModelMatrixLocation = Uniforms.GetLocation("normalModelMatrix");
	m_interpolationFactorLocation = Uniforms.GetLocation("interpolationFactor");
	m_boneIndexBaseLocation = Uniforms.GetLocation("uBoneIndexBase");
	m_modelAlphaLocation = Uniforms.GetLocation("modelAlpha");
	m_modelLightLevelLocation = Uniforms.GetLocation("modelLightLevel");
	m_lightBufferIndexLocation = Uniforms.GetLocation("lightBufferIndex");
	m_optionsLocation = Uniforms.GetLocation("modelOptions");
	m_specularMaterialLocation = Uniforms.GetLocation("uSpecularMaterial");
	m_drawLightIndexLocation = Uniforms.GetLocation(Lights::ClusterFragPrelude::DrawIndexUniform);
}

void ModelProgram::DrawLights(int index)
{
	int binding = GetBinding(Lights::ClusterFragPrelude::LightBufferStorage);
	if (binding < 0)
		binding = GetBinding(Lights::ClusterFragPrelude::LightBufferUniform);
	if (index < 0 || binding < 0)
	{
		ProgramUniforms::Set(-1, m_drawLightIndexLocation);
		return;
	}

	// FRenderState::SetLightIndex / ApplyBuffers: the uniform block variant binds the block range that holds the list.
	FLightBuffer* lights = screen->mLights;
	size_t offset = 0, size = 0;
	if (!lights->GetBufferType())
		index = lights->GetBinding(index, &offset, &size);

	screen->RenderState()->SetProgramBuffer(binding, lights->GetBuffer(), offset, size);
	ProgramUniforms::Set(index, m_drawLightIndexLocation);
}

void ModelProgram::ModelMatrix(const ::Helion::Geometry::mat4& matrix) { ProgramUniforms::Set(matrix, m_modelMatrixLocation); }
void ModelProgram::NormalModelMatrix(const ::Helion::Geometry::mat4& matrix) { ProgramUniforms::Set(matrix, m_normalModelMatrixLocation); }
void ModelProgram::InterpolationFactor(float factor) { ProgramUniforms::Set(factor, m_interpolationFactorLocation); }
void ModelProgram::BoneIndexBase(int index) { ProgramUniforms::Set(index, m_boneIndexBaseLocation); }
void ModelProgram::ModelAlpha(float alpha) { ProgramUniforms::Set(alpha, m_modelAlphaLocation); }
void ModelProgram::ModelLightLevel(float lightLevel) { ProgramUniforms::Set(lightLevel, m_modelLightLevelLocation); }
void ModelProgram::LightBufferIndex(int index) { ProgramUniforms::Set(index, m_lightBufferIndexLocation); }
void ModelProgram::Options(int options) { ProgramUniforms::Set(options, m_optionsLocation); }
void ModelProgram::SpecularMaterial(float glossiness, float specularLevel) { ProgramUniforms::Set(::Helion::Geometry::Vec2F(glossiness, specularLevel), m_specularMaterialLocation); }

int ModelProgram::BindBones(int boneStartIndex)
{
	if (boneStartIndex < 0)
		return -1;

	int binding = GetBinding("BoneBufferSSO");
	if (binding < 0)
		binding = GetBinding("BoneBufferUBO");
	if (binding < 0)
		return -1;

	BoneBuffer* bones = screen->mBones;
	size_t offset = 0, size = 0;
	int index = boneStartIndex;
	if (!bones->GetBufferType())
		index = bones->GetBinding(boneStartIndex, &offset, &size);

	screen->RenderState()->SetProgramBuffer(binding, bones->GetBuffer(), offset, size);
	return index;
}

IRenderProgram* ModelProgram::CreateBackendProgram(const RenderProgramDesc& desc)
{
	RenderProgramDesc patched = desc;

	// The layout of FModelVertexBuffer (hw_modelvertexbuffer.cpp): binding 0 is the frame at the vertex buffer offset,
	// binding 1 the second frame.
	patched.vertexAttributes.Clear();
	auto add = [&](int binding, int location, int format, size_t offset)
	{
		FVertexBufferAttribute attribute;
		attribute.binding = binding;
		attribute.location = location;
		attribute.format = format;
		attribute.offset = (int)offset;
		patched.vertexAttributes.Push(attribute);
	};
	add(0, VATTR_VERTEX, VFmt_Float3, offsetof(FModelVertex, x));
	add(0, VATTR_TEXCOORD, VFmt_Float2, offsetof(FModelVertex, u));
	add(0, VATTR_NORMAL, VFmt_Packed_A2R10G10B10, offsetof(FModelVertex, packedNormal));
	add(0, VATTR_BONESELECTOR, VFmt_Byte4_UInt, offsetof(FModelVertex, boneselector[0]));
	add(0, VATTR_BONEWEIGHT, VFmt_Byte4, offsetof(FModelVertex, boneweight[0]));
	add(1, VATTR_VERTEX2, VFmt_Float3, offsetof(FModelVertex, x));
	add(1, VATTR_NORMAL2, VFmt_Packed_A2R10G10B10, offsetof(FModelVertex, packedNormal));

	patched.vertexStride[0] = sizeof(FModelVertex);
	patched.vertexStride[1] = sizeof(FModelVertex);
	patched.numBindingPoints = 2;
	patched.instanceDivisorBinding = -1;

	return RenderProgramBase::CreateBackendProgram(patched);
}

FString ModelProgram::VertexShader()
{
	FString text = R"GLSL(
        #version 330

        layout(location = ${LocVertex}) in vec4 aPosition;
        layout(location = ${LocTexCoord}) in vec2 aTexCoord;
        layout(location = ${LocVertex2}) in vec4 aVertex2;
        layout(location = ${LocNormal}) in vec4 aNormal;
        layout(location = ${LocNormal2}) in vec4 aNormal2;
        layout(location = ${LocBoneWeight}) in vec4 aBoneWeight;
        layout(location = ${LocBoneSelector}) in uvec4 aBoneSelector;

        out vec2 uvFrag;
        flat out float alphaFrag;
        flat out float lightLevelFrag;
        flat out vec4 uzLightFrag;
        flat out float modelFullbrightFrag;
        out vec3 worldPosFrag;
        out vec3 normalFrag;
        out float dist2D;
        out float dist3D;
        ${SectorColorMapVertexFragVariables}

        uniform mat4 mvp;
        uniform vec4 helionClipPlane;
        uniform mat4 mvpNoPitch;
        uniform mat4 modelMatrix;
        uniform mat4 normalModelMatrix;
        uniform float interpolationFactor;
        uniform int uBoneIndexBase;
        uniform float modelAlpha;
        uniform float modelLightLevel;
        uniform int lightBufferIndex;
        uniform int modelOptions;
        uniform int useSectorColor;
        uniform int useSectorFog;
        ${SectorColorMapVertexUniformVariables}

        ${BoneBuffer}

        struct BonesResult
        {
            vec3 Normal;
            vec4 Position;
        };

        vec3 GetAttrNormal()
        {
            return mix(aNormal.xyz, aNormal2.xyz, interpolationFactor);
        }

        void AddWeightedBone(uint boneIndex, float weight, inout vec4 position, inout vec3 normal)
        {
            if (weight != 0.0)
            {
                mat4 transform = bones[uBoneIndexBase + int(boneIndex)];
                mat3 rotation = mat3(transform);
                position += (transform * aPosition) * weight;
                normal += (rotation * aNormal.xyz) * weight;
            }
        }

        BonesResult ApplyBones()
        {
            BonesResult result;
            if (uBoneIndexBase >= 0 && aBoneWeight != vec4(0.0))
            {
                result.Position = vec4(0.0);
                result.Normal = vec3(0.0);

                // We use low precision input for our bone weights. Rescale so the sum still is 1.0
                float totalWeight = aBoneWeight.x + aBoneWeight.y + aBoneWeight.z + aBoneWeight.w;
                float weightMultiplier = 1.0 / totalWeight;
                vec4 boneWeight = aBoneWeight * weightMultiplier;

                AddWeightedBone(aBoneSelector.x, boneWeight.x, result.Position, result.Normal);
                AddWeightedBone(aBoneSelector.y, boneWeight.y, result.Position, result.Normal);
                AddWeightedBone(aBoneSelector.z, boneWeight.z, result.Position, result.Normal);
                AddWeightedBone(aBoneSelector.w, boneWeight.w, result.Position, result.Normal);

                result.Position.w = 1.0; // For numerical stability
            }
            else
            {
                result.Position = aPosition;
                result.Normal = GetAttrNormal();
            }
            return result;
        }

        void main()
        {
            BonesResult bones = ApplyBones();

            // UZDoom GL frame: x, height, y
            vec4 worldcoord = modelMatrix * mix(bones.Position, aVertex2, interpolationFactor);
            // Helion world frame: x, y, height
            vec4 helionPos = vec4(worldcoord.x, worldcoord.z, worldcoord.y, 1.0);

            vec4 clip = mvp * helionPos;

            uvFrag = aTexCoord;
            worldPosFrag = worldcoord.xyz;
            normalFrag = (normalModelMatrix * vec4(normalize(bones.Normal), 1.0)).xyz;
            alphaFrag = modelAlpha;
            lightLevelFrag = (modelOptions & ${OptionFullbright}) != 0 ? 255.0 : modelLightLevel;
            modelFullbrightFrag = float((modelOptions & ${OptionFullbright}) != 0);
            dist3D = clip.z;
            // the truecolor light stage takes the view space depth (LightLevel::VertexDist)
            dist2D = clip.w;

            int lightIndexInt = lightBufferIndex;
            ${SectorColorMapVertexFunction}

            if ((modelOptions & ${OptionBlackFog}) != 0)
                sectorFogColorFrag.rgb = vec3(0.0);

            // b < 0: the r channel is a palette colormap index, there is no colour to modify.
            if ((modelOptions & ${OptionFullbright}) != 0)
            {
                if (sectorColorMapIndexFrag.b >= 0.0)
                    sectorColorMapIndexFrag = vec3(1.0);
            }
            else if ((modelOptions & ${OptionNoColoredLight}) != 0 && sectorColorMapIndexFrag.b >= 0.0)
            {
                // PalEntry::Decolorize
                float gray = (3.0 + 2.0 * (sectorColorMapIndexFrag.r + sectorColorMapIndexFrag.g + sectorColorMapIndexFrag.b)) / 9.0;
                sectorColorMapIndexFrag = vec3(gray);
            }

            // The inputs of the truecolor light stage (LightLevel::UzdoomLightVariables): no relative light, plane colour slot.
            uzLightFrag = vec4(lightLevelFrag, 0.0, float(lightIndexInt), texelFetch(sectorColormapTexture, lightIndexInt).a);

            gl_Position = clip;
            gl_ClipDistance[0] = dot(helionClipPlane, helionPos);
            #ifdef VULKAN_COORDINATE_SYSTEM
            gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
            #endif
        }
    )GLSL";

	text = Replace(text, "${LocVertex}", FStringf("%d", VATTR_VERTEX));
	text = Replace(text, "${LocTexCoord}", FStringf("%d", VATTR_TEXCOORD));
	text = Replace(text, "${LocVertex2}", FStringf("%d", VATTR_VERTEX2));
	text = Replace(text, "${LocNormal}", FStringf("%d", VATTR_NORMAL));
	text = Replace(text, "${LocNormal2}", FStringf("%d", VATTR_NORMAL2));
	text = Replace(text, "${LocBoneWeight}", FStringf("%d", VATTR_BONEWEIGHT));
	text = Replace(text, "${LocBoneSelector}", FStringf("%d", VATTR_BONESELECTOR));
	text = Replace(text, "${OptionFullbright}", FStringf("%d", ModelOptions::Fullbright));
	text = Replace(text, "${OptionNoColoredLight}", FStringf("%d", ModelOptions::NoColoredLight));
	text = Replace(text, "${OptionBlackFog}", FStringf("%d", ModelOptions::BlackFog));
	text = Replace(text, "${BoneBuffer}", BoneBufferBlock());
	text = Replace(text, "${SectorColorMapVertexFragVariables}", SectorColorMap::VertexFragVariables());
	text = Replace(text, "${SectorColorMapVertexUniformVariables}", SectorColorMap::VertexUniformVariables());
	text = Replace(text, "${SectorColorMapVertexFunction}", SectorColorMap::VertexFunction("lightIndexInt", "sectorColorMapIndexFrag", "sectorFogColorFrag"));
	return text;
}

std::optional<FString> ModelProgram::FragmentShader()
{
	// Material programs are always truecolor.
	PaletteColorModeScope colorMode(false);

	FString text = R"GLSL(
            #version 330

            in vec2 uvFrag;
            flat in float alphaFrag;
            in vec3 worldPosFrag;
            in vec3 normalFrag;
            flat in float modelFullbrightFrag;
            // models have no lightmap (vLightmap.z < 0: none)
            const vec3 lightmapFrag = vec3(0.0, 0.0, -1.0);
            ${VertexDistVar3D}

            out vec4 fragColor;
            ${PassOutputs}

            uniform int hasInvulnerability;
            uniform vec3 colorMix;
            uniform int useSectorFog;

            ${LightLevelFragVariables}
            ${SectorColorMapFragVariables}
            ${UzdoomPrelude}

            void main() {
                ${LightLevelFragFunction}
                ${FragColorFunction}
                ${UzdoomMainEnd}
            }
        )GLSL";

	// The light level of the model is a light level value, not a colormap index: the colormap texture of the palette mode is not read.
	// gl_emulateinvulncolormap reads the invulnerability colormap from it (FragFunction::InvulnerabilityFragColorInner).
	FString lightVariables = LightLevel::FragVariables(LightLevelOptions::Default);
	if (!ShaderVars::EmulateInvulnerabilityColorMap)
		lightVariables = Replace(lightVariables, "uniform samplerBuffer colormapTexture;", "");
	FString colorFunction = UseVertexNormal(FragFunction::FragColorFunction(FragColorFunctionOptions::Alpha | FragColorFunctionOptions::UzdoomMaterial,
		ColorMapFetchContext::Default, OitOptions::None));

	// Fullbright models (MDL / RF_FULLBRIGHT through ModelOptions::Fullbright) are fullbright in the truecolor light stage too.
	const char* fullbright = "float uzFullbright = float(lightLevelMix >= 1.0);";
	if (colorFunction.IndexOf(fullbright) < 0)
		I_FatalError("ModelProgram: the fullbright test of FragFunction changed");
	colorFunction = Replace(colorFunction, fullbright, "float uzFullbright = max(modelFullbrightFrag, float(lightLevelMix >= 1.0));");

	text = Replace(text, "${VertexDistVar3D}", FragFunction::VertexDistVar3D());
	text = Replace(text, "${PassOutputs}", UzdoomContract::PassOutputs(m_material.Pass));
	text = Replace(text, "${LightLevelFragVariables}", lightVariables);
	text = Replace(text, "${SectorColorMapFragVariables}", SectorColorMap::FragVariables());
	text = Replace(text, "${UzdoomPrelude}", UzdoomContract::MaterialPrelude(m_material));
	text = Replace(text, "${LightLevelFragFunction}", LightLevel::FragFunction());
	text = Replace(text, "${FragColorFunction}", colorFunction);
	text = Replace(text, "${UzdoomMainEnd}", UzdoomContract::PassMainEnd(m_material.Pass));
	return text;
}

}
