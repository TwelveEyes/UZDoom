/*
** UzdoomContract.cpp
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

#include "UzdoomContract.h"
#include "LumpText.h"
#include "ShaderText.h"
#include "OpenGL/Renderers/Legacy/World/Data/SectorLightBuffers.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterFragPrelude.h"
#include "Util/Constants.h"
#include "hwrenderer/data/hw_shaderpatcher.h"
#include "hwrenderer/data/hw_viewpointuniforms.h"
#include "hwrenderer/data/shaderuniforms.h"
#include "v_video.h"
#include "cmdlib.h"
#include "engineerrors.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

namespace
{

int DefaultShaderCount()
{
	int count = 0;
	while (defaultshaders[count].ShaderName != nullptr)
		count++;
	return count;
}

}

int UzdoomContract::MaterialShaderCount()
{
	return DefaultShaderCount() + (int)usershaders.Size();
}

FString UzdoomContract::MaterialShaderName(int index)
{
	int defaultCount = DefaultShaderCount();
	if (index < defaultCount)
		return defaultshaders[index].ShaderName;

	return ExtractFileBase(usershaders[index - defaultCount].shader.GetChars());
}

// The uniform blocks of the table shaders. The text of the std140 structs is what the table shaders use
// (ShaderInputsOutputs::GenerateStruct), so the member names and the layout are the ones mods and the backends know.
FString UzdoomContract::ViewpointBlock()
{
	FString text = "uniform ViewpointUBO";
	text << ShaderInputsOutputs::GenerateStruct<HWViewpointUniforms>() << ";\n";
	return text;
}

FString UzdoomContract::StreamBlock()
{
	FString text = "uniform StreamData";
	text << ShaderInputsOutputs::GenerateStruct<StreamData>() << ";\n";
	return text;
}

// The loose uniforms of the table shaders (gl_shader.cpp FShader::Load) that are not part of StreamData / ViewpointUBO.
// The vertex-only ones (matrices, bones) are not needed: the Helion vertex shaders have their own inputs.
FString UzdoomContract::LooseUniforms()
{
	return R"GLSL(
uniform int uTextureMode;
uniform vec2 uClipSplit;
uniform float uAlphaThreshold;

// Lighting + Fog
uniform vec4 uLightAttr;
#define uLightLevel uLightAttr.a
#define uFogDensity uLightAttr.b
#define uLightFactor uLightAttr.g
#define uLightDist uLightAttr.r
uniform int uFogEnabled;

// UZDoom-only: camera textures are stored upside down (FGameTexture negates their vertical scale)
uniform int materialFlipY;

// bone animation
uniform int uBoneIndexBase;

// Blinn glossiness and specular level
uniform vec2 uSpecularMaterial;
)GLSL";
}

FString UzdoomContract::Samplers(bool arrayTexture)
{
	FString text;
	text << (arrayTexture ? "uniform sampler2DArray tex;\n" : "uniform sampler2D tex;\n");
	text << R"GLSL(uniform sampler2D ShadowMap;
uniform sampler2DArray LightMap;
uniform sampler2D texture2;
uniform sampler2D texture3;
uniform sampler2D texture4;
uniform sampler2D texture5;
uniform sampler2D texture6;
uniform sampler2D texture7;
uniform sampler2D texture8;
uniform sampler2D texture9;
uniform sampler2D texture10;
uniform sampler2D texture11;
uniform sampler2D texture12;

// material types
#if defined(SPECULAR)
#define normaltexture texture2
#define speculartexture texture3
#define brighttexture texture4
#define detailtexture texture5
#define glowtexture texture6
#elif defined(PBR)
#define normaltexture texture2
#define metallictexture texture3
#define roughnesstexture texture4
#define aotexture texture5
#define brighttexture texture6
#define detailtexture texture7
#define glowtexture texture8
#else
#define brighttexture texture2
#define detailtexture texture3
#define glowtexture texture4
#endif
)GLSL";
	return text;
}

// In the table shaders these are the varyings of the vertex shader. The Helion programs compute them in main().
FString UzdoomContract::Globals()
{
	return R"GLSL(
vec4 vTexCoord;
vec4 vColor;
vec4 pixelpos;
vec3 glowdist;
vec3 gradientdist;
vec4 vWorldNormal;
vec4 vEyeNormal;
vec3 vLightmap;
)GLSL";
}

FString UzdoomContract::LightModePrelude(bool dynamicLights)
{
	FString text;
	text << "\n" << ViewpointBlock();
	text << R"GLSL(
uniform vec4 uLightAttr;
#define uLightLevel uLightAttr.a
#define uFogDensity uLightAttr.b
#define uLightFactor uLightAttr.g
#define uLightDist uLightAttr.r
vec4 pixelpos;
vec4 vWorldNormal;
)GLSL";
	text << LumpText::Require("shaders/glsl/helion_lightmode.fp") << "\n";
	text << LightStageDeclarations();

	if (dynamicLights)
	{
		// The clustered lights (plan D25), the attenuation functions of main.fp and HelionDynamicLight.
		FString defines;
		FString lights = Lights::ClusterFragPrelude::Declarations(defines);
		text << "\n" << defines << ShadowDeclarations(defines) << lights << "uniform sampler2D ShadowMap;\n";
		text << LumpText::Require("shaders/glsl/helion_shadow.fp") << "\n";
		text << LumpText::Require("shaders/glsl/helion_dynlight.fp") << "\n";
	}
	return text;
}

// The shadow source of shadowAttenuation (helion_shadow.fp): the ray queries of the Vulkan backend (vk_raytrace; the
// program description enables them with RenderProgramDesc::rayQuery when it finds the define) or the 1D shadow map.
FString UzdoomContract::ShadowDeclarations(const FString& defines)
{
	FString text;
	if (screen->RaytracingEnabled() && defines.IndexOf("SUPPORTS_SHADOWMAPS") >= 0)
		text << "#define SUPPORTS_RAYTRACING\n";
	return text;
}

// The sector buffer the truecolor light stage reads in the fragment stage besides the per vertex values of uzLightFrag:
// the level constants and the wall glow (SectorLightBuffers glow buffer). Needs helion_lightmode.fp and pixelpos.
FString UzdoomContract::LightStageDeclarations()
{
	FString text;
	text.AppendFormat("\nconst int HelionGlowLevelTexels = %d;\nconst int HelionSectorIndexStart = %d;\nconst int HelionWallOffset = %d;\nconst int HelionGlowTexels = %d;\n",
		Data::SectorLightBuffers::GlowLevelTexels, ::Helion::Util::Constants::LightBuffer::SectorIndexStart,
		::Helion::Util::Constants::LightBuffer::WallOffset, Data::SectorLightBuffers::GlowTexels);
	static_assert(::Helion::Util::Constants::LightBuffer::BufferSize == 3, "HelionWallGlow assumes three slots per sector");
	text << R"GLSL(
uniform samplerBuffer sectorGlowTexture;

// x = MAXDIST of SetShaderLight, y = brightfog, z = r_extralight, w = gl_weaponlight (SectorLightBuffers::GetLevelTexels)
vec4 HelionLevelLight()
{
	return texelFetch(sectorGlowTexture, 0);
}

// getLightColor's glowing walls (glowdist of main.vp per fragment). slot is the colour / fog buffer slot of the surface: only
// wall slots glow, like HWWall::HWF_GLOW.
vec3 HelionWallGlow(float slot, float desaturation)
{
	int s = int(slot) - HelionSectorIndexStart;
	if (s < 0 || (s % 3) != HelionWallOffset)
		return vec3(0.0);

	int base = HelionGlowLevelTexels + (s / 3) * HelionGlowTexels;
	vec4 topColor = texelFetch(sectorGlowTexture, base);
	vec4 bottomColor = texelFetch(sectorGlowTexture, base + 1);
	if (topColor.a <= 0.0 && bottomColor.a <= 0.0)
		return vec3(0.0);

	vec4 topPlane = texelFetch(sectorGlowTexture, base + 2);
	vec4 bottomPlane = texelFetch(sectorGlowTexture, base + 3);
	float topatpoint = (topPlane.w + topPlane.x * pixelpos.x + topPlane.y * pixelpos.z) * topPlane.z;
	float bottomatpoint = (bottomPlane.w + bottomPlane.x * pixelpos.x + bottomPlane.y * pixelpos.z) * bottomPlane.z;
	vec2 glowdist = vec2(topatpoint - pixelpos.y, pixelpos.y - bottomatpoint);

	vec3 glow = vec3(0.0);
	if (topColor.a > 0.0 && glowdist.x < topColor.a)
		glow += HelionDesaturate(topColor.rgb * (1.0 - glowdist.x / topColor.a), desaturation);
	if (bottomColor.a > 0.0 && glowdist.y < bottomColor.a)
		glow += HelionDesaturate(bottomColor.rgb * (1.0 - glowdist.y / bottomColor.a), desaturation);
	return glow;
}
)GLSL";
	return text;
}

// The material part of FShader::Load: the code of the material (func_defaultmat.fp or the user shader), the default
// light function and the light lump of the material type, chosen the same way (gl_shader.cpp). The defines the
// choice implies (NO_PROCESS_TEXEL, OLD_PROCESSLIGHT, LEGACY_USER_SHADER) are added to defines.
FString UzdoomContract::MaterialLumpText(int index, FString& defines)
{
	int defaultCount = DefaultShaderCount();
	FString procLump, lightLump, shaderDefines;
	if (index < defaultCount)
	{
		procLump = defaultshaders[index].gettexelfunc;
		lightLump = defaultshaders[index].lightfunc;
		shaderDefines = defaultshaders[index].Defines;
	}
	else
	{
		const UserShaderDesc& shader = usershaders[index - defaultCount];
		procLump = shader.shader;
		lightLump = defaultshaders[shader.shaderType].lightfunc;
		shaderDefines = FString(defaultshaders[shader.shaderType].Defines) + shader.defines;
	}

	defines << shaderDefines;

	FString text;
	if (procLump[0] != '#')
	{
		FString lumpFilename = procLump;
		FString ppData;
		TArray<FString> filenamesForError;
		bool coreFound;
		FString coreData = LumpText::Get(procLump.GetChars(), true, &coreFound);	// if it's a core shader, ignore overrides by user mods.
		if (!coreFound)
		{
			bool found;
			FString modData = LumpText::Get(procLump.GetChars(), false, &found);
			if (!found)
				I_Error("Unable to load '%s'", procLump.GetChars());

			ppData = LumpText::Include(modData, lumpFilename.GetChars(), filenamesForError);
		}
		else
		{
			// skip includes processing for code shaders
			ppData = coreData;
		}

		if (ppData.IndexOf("ProcessMaterial") < 0 && ppData.IndexOf("SetupMaterial") < 0)
		{
			// this looks like an old custom hardware shader.
			if (ppData.IndexOf("GetTexCoord") >= 0)
			{
				text << "\n" << LumpText::Require("shaders/glsl/func_defaultmat2.fp");
			}
			else
			{
				text << "\n" << LumpText::Require("shaders/glsl/func_defaultmat.fp");

				if (ppData.IndexOf("ProcessTexel") < 0)
				{
					// this looks like an even older custom hardware shader.
					// We need to replace the ProcessTexel call to make it work.
					defines << "#define NO_PROCESS_TEXEL\n";
				}
			}

			if (ppData.IndexOf("ProcessLight") >= 0)
			{
				// The ProcessLight signatured changed. Forward to the old one.
				defines << "#define OLD_PROCESSLIGHT\n";
			}
		}

		FString userText = RemoveLegacyUserUniforms(ppData);
		userText.Substitute("gl_TexCoord[0]", "vTexCoord");	// fix old custom shaders.
		text << userText;

		if (ppData.IndexOf("ProcessLight") < 0)
			text << "\n" << LumpText::Require("shaders/glsl/func_defaultlight.fp");

		// ProcessMaterial must be considered broken because it requires the user to fill in data they possibly cannot know all about.
		if (ppData.IndexOf("ProcessMaterial") >= 0 && ppData.IndexOf("SetupMaterial") < 0)
		{
			// This reactivates the old logic and disables all features that cannot be supported with that method.
			defines << "#define LEGACY_USER_SHADER\n";
		}
	}
	else
	{
		// The proc lump is not a lump name but the source itself (from generated shaders)
		text << procLump.GetChars() + 1;
	}

	if (lightLump.Len() > 0)
		text << "\n" << LumpText::Require(lightLump.GetChars()) << "\n";

	return text;
}

// getLightColor of main.fp with the light of the Helion programs instead of vColor / uLightLevel: light is the colour of
// HelionGetLight (light level, sector colour, black fog) plus the wall glow (HelionWallGlow, the glow planes of the sector
// buffers). Everything a mod can hook (brightmaps/glow from the material, ProcessLight, ProcessMaterialLight with the dynamic
// lights) runs as in the table shaders; the coloured fog is applied by the caller.
FString UzdoomContract::HelionLightFunction()
{
	return R"GLSL(

vec4 getHelionLightColor(Material material, vec3 light)
{
	vec4 color = vec4(light, vColor.a);
	color = min(color, 1.0);

#if !defined LEGACY_USER_SHADER && !defined NO_LAYERS
	//
	// apply glow
	//
	color.rgb = mix(color.rgb, material.Glow.rgb, material.Glow.a);

	//
	// apply brightmaps
	//
	color.rgb = min(color.rgb + material.Bright.rgb, 1.0);
#endif

	//
	// apply other light manipulation by custom shaders, default is a NOP.
	//
	color = ProcessLight(material, color);

	//
	// apply lightmaps
	//
	if (vLightmap.z >= 0.0)
	{
		color.rgb += texture(LightMap, vLightmap).rgb;
	}

	//
	// apply dynamic lights
	//
	return vec4(ProcessMaterialLight(material, color.rgb), material.Base.a * vColor.a);
}
)GLSL";
}

FString UzdoomContract::MaterialPrelude(const MaterialProgramOptions& options)
{
	FString defines;
	if (options.Pass == GBUFFER_PASS)
		defines << "#define GBUFFER_PASS\n";
	if (options.ArrayTexture)
		defines << "#define HELION_ARRAY_TEXTURE\n";
	if (usershaders.Size() > 0 || options.MaterialShaderIndex >= 0)
	{
		// noise functions are broken on macOS drivers, like in the table shaders
#ifdef __APPLE__
		defines << "#define noise1(unused) 0.0\n#define noise2(unused) vec2(0)\n#define noise3(unused) vec3(0)\n#define noise4(unused) vec4(0)\n";
#endif
	}

	// The clustered lights (plan D25) replace the per draw uLightIndex uniform and the lights[] block of FLightBuffer;
	// models keep a per draw list (ModelLightList) for the per actor light flags.
	FString lightBlock = options.DrawLightList ? Lights::ClusterFragPrelude::DrawListDeclarations(defines) : Lights::ClusterFragPrelude::Declarations(defines);
	defines << ShadowDeclarations(defines);
	FString materialText = MaterialLumpText(options.MaterialShaderIndex, defines);

	FString text;
	text << "\n" << defines;
	text << StreamBlock() << ViewpointBlock() << lightBlock << LooseUniforms() << Samplers(options.ArrayTexture) << Globals();
	text << "\n" << LumpText::Require("shaders/glsl/helion_lightmode.fp") << "\n";
	text << LightStageDeclarations();
	text << LumpText::Require("shaders/glsl/helion_contract.fp") << "\n";
	text << LumpText::Require("shaders/glsl/helion_shadow.fp") << "\n";
	text << materialText;
	text << HelionLightFunction();
	return text;
}

FString UzdoomContract::PassOutputs(EPassType pass)
{
	if (pass != GBUFFER_PASS)
		return "";

	return "out vec4 FragFog;\nout vec4 FragNormal;\n";
}

FString UzdoomContract::PassMainEnd(EPassType pass)
{
	if (pass != GBUFFER_PASS)
		return "";

	return "FragFog = vec4(AmbientOcclusionColor(), 1.0);\nFragNormal = vec4(vEyeNormal.xyz * 0.5 + 0.5, 1.0);";
}

}
