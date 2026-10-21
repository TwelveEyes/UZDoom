/*
** ClusterFragPrelude.h
**
** UZDoom-only (plan M6.1): the GLSL text FragFunction / UzdoomContract emit for clustered dynamic lights. It replaces
** the table shaders' per surface `uniform int uLightIndex;` (set by FRenderState::SetLightIndex for every draw) with a
** global that the first statement of main() fills from the fragment's cluster, and the `lights[]` declaration
** (UzdoomContract::LightBufferBlock) with the one of the cluster buffers. Mod shaders keep reading
** `lights[uLightIndex]` exactly as before (func_defaultlight.fp / material_*.fp / main.fp ProcessMaterialLight).
**
** The declarations are plain GLSL that RenderProgram lifts into the program description (ProgramSource.h), so the
** bindings below appear in RenderProgram::Desc() under these names:
**
**   uniform block   ClusterParams        loose block members uClRange, uClViewport, uClGrid, uClState; set per view with
**                                        ClusterLightBuffer::Bind (Uniforms.SetBlock("ClusterParams", ...))
**   storage buffer  LightBufferSSO       lights[] (storage mode)      \  the names RenderProgramBase::LightBuffer
**   uniform block   LightBufferUBO       lights[N] (uniform mode)     /  already looks up
**   storage buffer  ClusterOffsetBuffer  int clusterOffsets[] (storage mode)
**   sampler         clusterOffsetTexture isamplerBuffer (uniform mode)
**   loose uniform   uClusterLightsOff    1 = this draw gets no dynamic lights (uLightIndex = -1)
**
** Storage mode is DFrameBuffer::IsVulkan() or GL with compute (UsesStorageBuffers); otherwise the lights are a
** uniform block of UboLightCapacity() vec4 and the offsets a texel buffer.
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

class ClusterFragPrelude
{
public:
	static constexpr const char* ParamsBlock = "ClusterParams";
	static constexpr const char* LightBufferStorage = "LightBufferSSO";
	static constexpr const char* LightBufferUniform = "LightBufferUBO";
	static constexpr const char* OffsetBufferStorage = "ClusterOffsetBuffer";
	static constexpr const char* OffsetTexture = "clusterOffsetTexture";
	static constexpr const char* DisableUniform = "uClusterLightsOff";

	// Storage buffers (SSBO lights[] and offsets) or uniform block + texel buffer.
	static bool UsesStorageBuffers();

	// Number of vec4 of the lights[] uniform block (uniform mode): DFrameBuffer::maxuniformblock / 16.
	static int UboLightCapacity();

	// The complete declaration text, to be emitted where UzdoomContract emits LooseUniforms' uLightIndex and
	// LightBufferBlock: the lights[] block, ClusterParams, the offsets, `int uLightIndex;` (a global, not a uniform),
	// uClusterLightsOff and the lookup functions
	//   int clusterLightIndex(vec4 fragCoord, float viewDepth)   viewDepth = distance along the view axis (-eye z)
	//   int clusterLightIndex(vec4 fragCoord)                    viewDepth = 1.0 / fragCoord.w (perspective passes)
	// defines receives the SHADER_STORAGE_LIGHTS / SUPPORTS_SHADOWMAPS or NUM_UBO_LIGHTS lines the contract's prelude
	// needs. The HUD weapon (orthographic) must use the two argument form with the world distance of the weapon.
	static FString Declarations(FString& defines);
	// The same for an explicit mode (tools, tests): storage = UsesStorageBuffers(), uboCapacity = UboLightCapacity().
	static FString Declarations(FString& defines, bool storage, int uboCapacity);

	// Models and HUD models (ModelLightList): lights[] is the frame's FLightBuffer (screen->mLights; a storage block when
	// that is a storage buffer, else a uniform block of its block size, like gl_shader.cpp) and both lookup functions
	// return the per draw uniform uDrawLightIndex (ModelProgram::DrawLights; -1 = no lights). No ClusterParams, offsets or
	// uClusterLightsOff.
	static FString DrawListDeclarations(FString& defines);
	static constexpr const char* DrawIndexUniform = "uDrawLightIndex";

	// The statement that must be the first of main(): uLightIndex = ...;
	static FString MainStart();
};

}
