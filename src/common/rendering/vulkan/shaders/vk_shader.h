/*
** vk_shader.h
**
** Vulkan backend
**
**---------------------------------------------------------------------------
**
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Copyright 2016-2020 Magnus Norddahl
**
** SPDX-License-Identifier: Zlib
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include <cstddef>
#include <memory>

#include "vectors.h"
#include "matrix.h"
#include "name.h"
#include "hw_renderstate.h"
#include "zvulkan/vulkanbuilders.h"
#include <list>
#include "shaderuniforms.h"
#include "hwrenderer/data/hw_levelmeshdata.h"

#define SHADER_MIN_REQUIRED_TEXTURE_LAYERS 11

class VulkanRenderDevice;
class VulkanDevice;
class VulkanShader;
class VkPPShader;
class PPShader;

struct MatricesUBO
{
	VSMatrix ModelMatrix;
	VSMatrix NormalModelMatrix;
	VSMatrix TextureMatrix;
};

#define MAX_STREAM_DATA ((int)(65536 / sizeof(StreamData)))

struct StreamUBO
{
	StreamData data[MAX_STREAM_DATA];
};

struct PushConstants
{
	int uTextureMode;
	float uAlphaThreshold;
	FVector2 uClipSplit;

	// Lighting + Fog
	float uLightLevel;
	float uFogDensity;
	float uLightFactor;
	float uLightDist;
	int uFogEnabled;

	// dynamic lights
	int uLightIndex;

	// Blinn glossiness and specular level
	FVector2 uSpecularMaterial;

	// bone animation
	int uBoneIndexBase;

	int uDataIndex;
	int uLevelMeshFlags;	// levelmesh pipelines only (LMVF_*); 0 for classic draws
	float uLevelMeshTime;	// levelmesh pipelines only: (FrameTime - firstFrame) / 1000, the material timer at speed 1
	int uLevelMeshView;		// levelmesh mark pass only: word offset of the view's region in LMBIND_EXPOSURE (ticket 18)

	// Ticket 19: the dynamic light list of the draw (lm_lights.glsl).
	int uLightOwner;		// -1 none; >= 0 owner index; < -1: -(2 + float index in lights[]) of an inline list
	int uLightConsumer;		// LMLC_*
	int uLightActor;		// LevelMesh_ActorId of the drawn actor (actor consumers)
	FVector4 uLightOrigin;	// LMLC_POINT: where the vertex stage evaluates the list (GL order x, z, y)
};
static_assert(offsetof(PushConstants, uLightOrigin) == 80 && sizeof(PushConstants) == 96, "PushConstants must match the GLSL block (vk_shader.cpp)");

class VkShaderProgram
{
public:
	std::unique_ptr<VulkanShader> vert;
	std::unique_ptr<VulkanShader> frag;
};

class VkShaderManager
{
public:
	VkShaderManager(VulkanRenderDevice* fb);
	~VkShaderManager();

	void Deinit();

	VkShaderProgram *GetEffect(int effect, EPassType passType);
	VkShaderProgram *Get(unsigned int eff, bool alphateston, EPassType passType);
	bool CompileNextShader();

	VkPPShader* GetVkShader(PPShader* shader);

	// Levelmesh: the vertex shader is shared by all programs (vert is null in
	// the returned program; use GetLevelMeshVert).
	VkShaderProgram *GetLevelMesh(bool alphatest, EPassType passType, int program, bool dither);
	// The mark pass fragment stage (ticket 18) of a level whose leaf bits
	// take 'leafWords' words (compiled in: the line bits follow them).
	std::unique_ptr<VulkanShader> LoadLevelMeshMarkShader(bool alphatest, unsigned leafWords, bool subgroups);
	VulkanShader *GetLevelMeshVert() { return mLevelMeshVert.get(); }
	std::unique_ptr<VulkanShader> LoadComputeShader(FString shadername, const char *lump, const char *defines);

	void AddVkPPShader(VkPPShader* shader);
	void RemoveVkPPShader(VkPPShader* shader);

private:
	std::unique_ptr<VulkanShader> LoadVertShader(FString shadername, const char *vert_lump, const char *defines, AllShaderIndex type);
	std::unique_ptr<VulkanShader> LoadFragShader(FString shadername, const char *frag_lump, const char *material_lump, const char *light_lump, const char *defines, bool alphatest, bool gbufferpass, AllShaderIndex type, bool levelmesh = false, const char *extensions = nullptr);
	std::unique_ptr<VulkanShader> LoadLevelMeshVertShader(FString shadername, const char *defines);

	FString GetTargetGlslVersion();
	FString LoadPublicShaderLump(const char *lumpname);
	FString LoadPrivateShaderLump(const char *lumpname);

	static ShaderIncludeResult OnInclude(FString headerName, FString includerName, size_t depth);

	VulkanRenderDevice* fb = nullptr;

	std::vector<VkShaderProgram> mMaterialShaders[MAX_PASS_TYPES];
	std::vector<VkShaderProgram> mMaterialShadersNAT[MAX_PASS_TYPES];
	std::vector<VkShaderProgram> mEffectShaders[MAX_PASS_TYPES];
	VkShaderProgram mLevelMeshShaders[LMP_COUNT][2][MAX_PASS_TYPES][2];	// program, alpha test, pass, dither
	std::unique_ptr<VulkanShader> mLevelMeshVert;
	uint8_t compilePass = 0, compileState = 0;
	int compileIndex = 0;

	std::list<VkPPShader*> PPShaders;
	friend class VkPPShader;
};
