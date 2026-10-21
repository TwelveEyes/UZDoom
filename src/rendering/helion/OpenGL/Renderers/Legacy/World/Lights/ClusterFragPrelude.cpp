/*
** ClusterFragPrelude.cpp
**
** UZDoom-only (plan M6.1): see ClusterFragPrelude.h. The cluster lookup is the inverse of ClusterGrid::SliceOf /
** ClusterGrid::Bounds.
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

#include "OpenGL/Renderers/Legacy/World/Lights/ClusterFragPrelude.h"
#include "hw_lightbuffer.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

bool ClusterFragPrelude::UsesStorageBuffers()
{
	return screen->IsVulkan() || ((screen->hwcaps & RFL_COMPUTE_SHADER) && screen->allowSSBO());
}

int ClusterFragPrelude::UboLightCapacity()
{
	return (int)(screen->maxuniformblock / 16);
}

FString ClusterFragPrelude::Declarations(FString& defines)
{
	return Declarations(defines, UsesStorageBuffers(), UboLightCapacity());
}

FString ClusterFragPrelude::Declarations(FString& defines, bool storage, int capacity)
{
	FString text;
	if (storage)
	{
		defines << "#define SUPPORTS_SHADOWMAPS\n#define SHADER_STORAGE_LIGHTS\n";
		text << "readonly buffer LightBufferSSO\n{\n\tvec4 lights[];\n};\n";
		text << "readonly buffer ClusterOffsetBuffer\n{\n\tint clusterOffsets[];\n};\n";
	}
	else
	{
		defines.AppendFormat("#define NUM_UBO_LIGHTS %d\n", capacity);
		text.AppendFormat("uniform LightBufferUBO\n{\n\tvec4 lights[%d];\n};\n", capacity);
		text << "uniform isamplerBuffer clusterOffsetTexture;\n";
	}

	text << R"GLSL(
uniform ClusterParams
{
	vec4 uClRange;		// near, far, slice scale, slice bias
	vec4 uClViewport;	// x, y, width, height in gl_FragCoord space
	ivec4 uClGrid;		// tiles x, tiles y, slices, tile pixels
	ivec4 uClState;		// x = 1 when the offsets are valid for this view
};

// 1 = this draw gets no dynamic lights (additive surfaces without gl_lightadditivesurfaces, fog boundaries, ...)
uniform int uClusterLightsOff;

// The header index of the light record of the fragment's cluster in lights[] (-1 = no lights). A global filled by the first
// statement of main(), read by ProcessMaterialLight and the material shaders like the table shaders' uniform.
int uLightIndex;

int clusterLightIndex(vec4 fragCoord, float viewDepth)
{
	if (uClState.x == 0 || uClusterLightsOff != 0)
		return -1;

	vec2 p = fragCoord.xy - uClViewport.xy;
	int tx = clamp(int(p.x) / uClGrid.w, 0, uClGrid.x - 1);
	int ty = clamp(int(p.y) / uClGrid.w, 0, uClGrid.y - 1);
	int slice = clamp(int(floor(log(max(viewDepth, uClRange.x)) * uClRange.z + uClRange.w)), 0, uClGrid.z - 1);
	int cluster = tx + ty * uClGrid.x + slice * uClGrid.x * uClGrid.y;
)GLSL";
	if (storage)
		text << "\treturn clusterOffsets[cluster];\n}\n";
	else
		text << "\treturn texelFetch(clusterOffsetTexture, cluster).r;\n}\n";

	text << R"GLSL(
int clusterLightIndex(vec4 fragCoord)
{
	return clusterLightIndex(fragCoord, 1.0 / fragCoord.w);
}
)GLSL";
	return text;
}

FString ClusterFragPrelude::DrawListDeclarations(FString& defines)
{
	FString text;
	// gl_shader.cpp: the 1D shadow map needs storage buffers, independent of the type of the light buffer (a uniform
	// buffer on OpenGL, FLightBuffer / DFrameBuffer::useSSBO).
	if (UsesStorageBuffers())
		defines << "#define SUPPORTS_SHADOWMAPS\n";
	if (screen->mLights->GetBufferType())
	{
		defines << "#define SHADER_STORAGE_LIGHTS\n";
		text << "readonly buffer LightBufferSSO\n{\n\tvec4 lights[];\n};\n";
	}
	else
	{
		int capacity = (int)screen->mLights->GetBlockSize();
		defines.AppendFormat("#define NUM_UBO_LIGHTS %d\n", capacity);
		text.AppendFormat("uniform LightBufferUBO\n{\n\tvec4 lights[%d];\n};\n", capacity);
	}

	text << R"GLSL(
// The header index of the draw's light list in lights[] (-1 = no lights), set per draw.
uniform int uDrawLightIndex;

// Filled by the first statement of main() like the cluster lookup does it for the world programs.
int uLightIndex;

int clusterLightIndex(vec4 fragCoord, float viewDepth)
{
	return uDrawLightIndex;
}

int clusterLightIndex(vec4 fragCoord)
{
	return uDrawLightIndex;
}
)GLSL";
	return text;
}

FString ClusterFragPrelude::MainStart()
{
	return "\tuLightIndex = clusterLightIndex(gl_FragCoord);\n";
}

}
