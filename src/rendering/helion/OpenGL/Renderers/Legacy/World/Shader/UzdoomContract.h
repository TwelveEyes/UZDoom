/*
** UzdoomContract.h
**
** UZDoom-only. The mod-visible shader contract (D5) for the Helion world programs.
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

#include "zstring.h"
#include "tarray.h"
#include "hwrenderer/data/hw_renderstate.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

// Which material shader a program draws with. The material shader table is the one the table shaders use:
// indices 0..N-1 are defaultshaders[] (Default, Warp 1, Warp 2, Specular, PBR, Paletted, No Texture, fuzz variants),
// N.. are usershaders[] (GLDEFS HardwareShader / material shaders), N = MaterialShaderCount() - usershaders.Size().
struct MaterialProgramOptions
{
	static constexpr int NoMaterial = -1;

	int MaterialShaderIndex = NoMaterial;
	EPassType Pass = NORMAL_PASS;
	// tex is a sampler2DArray and getTexel samples the layer boundTextureIndex (the varying of the Helion programs).
	bool ArrayTexture = false;
	// lights[] / uLightIndex are the per draw light list of the frame's FLightBuffer (models, ModelLightList) instead of
	// the view's clustered world lights.
	bool DrawLightList = false;

	bool IsMaterial() const { return MaterialShaderIndex != NoMaterial; }
};

// Generates the pieces of the fragment shader that make the existing GLDEFS material/hardware shaders (ZScript Shader
// API, ProcessMaterial, SetupMaterial, ProcessLight, ProcessMaterialLight, getTexel, ...) compile and behave like they do
// in the table shaders: the uniform blocks and samplers, the contract globals, the contract functions copied from main.fp
// (shaders/glsl/helion_contract.fp), the material lump chosen exactly like FShader::Load chooses it and the helper that
// runs the contract's light stage with the Helion light level.
//
// All declarations are plain GLSL text that RenderProgram lifts into the program description (ProgramSource.h).
class UzdoomContract
{
public:
	// Number of material shaders: defaultshaders[] (without the terminator) plus usershaders[].
	static int MaterialShaderCount();

	// The display name of a material shader (used in the program label).
	static FString MaterialShaderName(int index);

	// Truecolor native programs with the UzdoomLightMode option: the viewpoint block, the light attribute uniform,
	// pixelpos / vWorldNormal and R_DoomLightingEquation (shaders/glsl/helion_lightmode.fp). dynamicLights adds the clustered
	// lights (ClusterFragPrelude), the ShadowMap sampler and HelionDynamicLight (FragColorFunctionOptions::DynamicLights).
	static FString LightModePrelude(bool dynamicLights = false);
	// The sector buffer declarations of the truecolor light stage (level constants, HelionWallGlow); follows helion_lightmode.fp.
	static FString LightStageDeclarations();

	// Complete fragment declarations + functions up to (excluding) main() for a per material program.
	static FString MaterialPrelude(const MaterialProgramOptions& options);

	// The extra fragment outputs and the writes at the end of main() of the G-buffer pass.
	static FString PassOutputs(EPassType pass);
	static FString PassMainEnd(EPassType pass);

private:
	static FString ViewpointBlock();
	static FString StreamBlock();
	static FString LooseUniforms();
	static FString ShadowDeclarations(const FString& defines);
	static FString Samplers(bool arrayTexture);
	static FString Globals();
	static FString MaterialLumpText(int index, FString& defines);
	static FString HelionLightFunction();
};

}
