/*
** PlaneClip.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Shader.PlaneClip.
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

#include "PlaneClip.h"
#include "ShaderText.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

FString PlaneClip::WritePlaneFragFunction(PlaneClipFragOptions options)
{
	bool alphaSample = options == PlaneClipFragOptions::AlphaSample;
	FString text = R"GLSL(
            #version 330

            flat in float zPos;
            flat in float upperFrag;
            flat in float lowerFrag;
            flat in float boundTextureIndex;
            in float depthFrag;
            ${InVars}

            uniform sampler2DArray boundTexture;

            layout (location = 0) out vec3 outPlane;

            void main() {
                ${AlphaTexture}
                )GLSL";
	text << GetOutPlane(true) << "\n            }";

	text = Replace(text, "${InVars}", alphaSample ? "in vec2 uvFrag;" : "");
	text = Replace(text, "${AlphaTexture}", GetAlphaSample(alphaSample));
	return text;
}

FString PlaneClip::WriteWallFragFunction(WallClipFragOptions options)
{
	bool alphaSample = options == WallClipFragOptions::AlphaSample;
	bool discardNegativeMapId = options == WallClipFragOptions::DiscardNegativeMapId;
	FString text = R"GLSL(
            #version 330

            flat in float mapIdFrag;
            flat in float upperFrag;
            flat in float lowerFrag;
            flat in float boundTextureIndex;
            in float depthFrag;
            ${InVars}

            uniform sampler2DArray boundTexture;

            layout (location = 0) out vec4 outPlane;

            void main() {
                ${AlphaTexture}
                ${DiscardNegativeMapId}

                int lineId = int(mapIdFrag);
                int byte0 = lineId & 0xFF;
                int byte1 = (lineId >> 8) & 0xFF;
                // Pack lower and upper flags and overflow bytes after 65536 for line id.
                // This should allow for 256x256x64 = 4,194,304 line ids.
                int byte2 = ((lineId >> 16) & 0x3F) << 2 | int(lowerFrag + (upperFrag * 2));

                outPlane = vec4(byte0, byte1, byte2, depthFrag);
            })GLSL";

	text = Replace(text, "${InVars}", alphaSample ? "in vec2 uvFrag;" : "");
	text = Replace(text, "${AlphaTexture}", GetAlphaSample(alphaSample));
	text = Replace(text, "${DiscardNegativeMapId}", GetDiscardNegativeMapId(discardNegativeMapId));
	return text;
}

FString PlaneClip::GetTransparentDiscard(OitOptions options)
{
	if (options == OitOptions::None)
		return "";

	return R"GLSL(
            if (checkPlaneClip == 1) {
                ivec2 sampleCoords = ivec2(clamp(gl_FragCoord.xy / downScaleAmount, vec2(0.0), screenBounds / downScaleAmount));
                float wallClipDepth = texelFetch(wallClipTexture, sampleCoords, 0).a;
                float planeClipDepth = texelFetch(planeClipTexture, sampleCoords, 0).g;
                // This is for alpha walls and vanilla rendering
                // There is no depth buffer at this point so sample the plane clip texture to discard
                if (wallClipDepth <= depthFrag || planeClipDepth <= depthFrag)
                    discard;
            })GLSL";
}

FString PlaneClip::GetDiscardNegativeMapId(bool discard)
{
	if (!discard)
		return "";

	// mapIdFrag check is required for flood fill rendering that doesn't separate planes(floor/ceiling) from walls
	// Planes are written with -1 and need to be ignored
	return "if (mapIdFrag < 0) discard;";
}

FString PlaneClip::GetAlphaSample(bool alphaSample)
{
	if (!alphaSample)
		return "";

	return R"GLSL(float alpha = texture(boundTexture, vec3(uvFrag.xy, boundTextureIndex)).a;
                if (alpha <= 0) discard;)GLSL";
}

FString PlaneClip::GetOutPlane(bool planeClip)
{
	if (!planeClip)
		return FString();

	return "outPlane = vec3(zPos, depthFrag, lowerFrag + (upperFrag * 2));";
}

FString PlaneClip::GetOutTargets(bool planeClip)
{
	if (!planeClip)
		return "out vec4 fragColor;";

	return R"GLSL(
            layout (location = 0) out vec4 fragColor;
            layout (location = 1) out vec3 outPlane;
)GLSL";
}

}
