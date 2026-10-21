/*
** ModelProgram.h
**
** UZDoom-only (plan M4.3): the program models and voxels are drawn with. Helion has no model renderer, this is the
** FModelVertex pipeline of main.vp (aPosition / aVertex2 frame interpolation, aNormal / aNormal2, bone matrices of the
** BoneBuffer) on the fragment stage of the Helion world programs: the light level / colormap / sector colour / sector fog
** path of StaticShader (LightLevel, SectorColorMap, FragFunction) with the mod shader contract (UzdoomContract material
** programs), so GLDEFS material and hardware shaders work on skins.
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

#pragma once

#include "OpenGL/Renderers/Legacy/World/RenderProgramBase.h"
#include "OpenGL/Renderers/Legacy/World/Shader/UzdoomContract.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

// Bits of ModelProgram::Options.
struct ModelOptions
{
	static constexpr int Fullbright = 1;		// light level 255 and a white sector light colour (fog is kept)
	static constexpr int NoColoredLight = 2;	// LEVEL3_NOCOLOREDSPRITELIGHTING: decolorize the sector light colour
	static constexpr int BlackFog = 4;			// additive styles: the fog colour is black (additivefog of HWSprite::DrawSprite)
};

// Create with RenderProgram::Create<ModelProgram>(name, material) (or HelionProgramRegistry). The material shader of the
// program is the one of the skin texture (FMaterial::GetShaderIndex()); MaterialProgramOptions without a material shader
// selects the default material shader (index 0).
class ModelProgram : public RenderProgramBase
{
public:
	explicit ModelProgram(const FString& name, const Shader::MaterialProgramOptions& material = Shader::MaterialProgramOptions());

	const Shader::MaterialProgramOptions& Material() const { return m_material; }

	// Object space -> UZDoom GL world space (x, height, y) and the matrix that transforms its normals (VSMatrix::computeNormalMatrix).
	void ModelMatrix(const ::Helion::Geometry::mat4& matrix);
	void NormalModelMatrix(const ::Helion::Geometry::mat4& matrix);
	// FRenderState::SetInterpolationFactor: blend of the vertex (aPosition) and the second frame vertex (aVertex2).
	void InterpolationFactor(float factor);
	// uBoneIndexBase of the shader; -1 = the model has no bones. Use BindBones() to get the value for a boneStartIndex.
	void BoneIndexBase(int index);
	// Alpha of the render style (HWSprite::trans), multiplied into the texture alpha.
	void ModelAlpha(float alpha);
	// The light level (0..255) of the model (HWSprite::lightlevel), 255 for fullbright is done by ModelOptions::Fullbright.
	void ModelLightLevel(float lightLevel);
	// Index of the light buffer slot of the sector (sector colour and fog of the Helion sector buffers).
	void LightBufferIndex(int index);
	// ModelOptions bits.
	void Options(int options);
	// FRenderState::SetSpecular (Blinn glossiness and specular level of the material).
	void SpecularMaterial(float glossiness, float specularLevel);
	// hw_GetDynModelLight: the light list of the draw, an index of screen->mLights (ModelLightList::Upload; -1 = no lights).
	// Binds the light buffer (the block range of the list in uniform block mode) and sets uDrawLightIndex.
	void DrawLights(int index);

	// Binds the bone buffer (screen->mBones) for the draws that follow and returns the value for BoneIndexBase
	// (uniform buffer fallback: the index relative to the bound range). boneStartIndex < 0 returns -1.
	int BindBones(int boneStartIndex);

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	// The model vertex buffer has the second frame in the second binding point: 0 = frame 1 at the vertex buffer offset,
	// 1 = frame 2 (the render state's instance buffer slot, set to the same buffer with the offset of the second frame), no divisor.
	IRenderProgram* CreateBackendProgram(const RenderProgramDesc& desc) override;
	void ProgramCreated() override;

private:
	Shader::MaterialProgramOptions m_material;

	int m_modelMatrixLocation = -1;
	int m_normalModelMatrixLocation = -1;
	int m_interpolationFactorLocation = -1;
	int m_boneIndexBaseLocation = -1;
	int m_modelAlphaLocation = -1;
	int m_modelLightLevelLocation = -1;
	int m_lightBufferIndexLocation = -1;
	int m_optionsLocation = -1;
	int m_specularMaterialLocation = -1;
	int m_drawLightIndexLocation = -1;
};

}
