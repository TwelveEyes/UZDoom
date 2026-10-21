/*
** VertexFunction.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Shader.VertexFunction.
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

#include "VertexFunction.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

FString VertexFunction::VertexDistVar3D()
{
	return "out float dist3D;";
}

FString VertexFunction::SetVertexDist3D(const FString& posVariable)
{
	// Depth is always z (plan D11, Helion ShaderVars.Depth).
	FString text;
	text << "dist3D = (mvp * " << posVariable << ").z;";
	return text;
}

FString VertexFunction::VertexGapVariables()
{
	return "flat out vec2 uvClampMinFrag; flat out vec2 uvClampMaxFrag;";
}

FString VertexFunction::VertexGapSet()
{
	return R"GLSL(
            const float MaxValue = 1e30;
            uvClampMinFrag = vec2(-MaxValue, -MaxValue);
            uvClampMaxFrag = vec2(MaxValue, MaxValue);

            if (vertexGapClampUV == 1) {
                ivec2 texSize = textureSize(boundTexture, 0).xy;
                const float VertexGapX = 0.1;
                float pixelSize = uvFrag.y * texSize.y;
                // Push y further since it's more likely to show t-junction issue with subsector flat splits
                // But don't push if on a fractional part of a pixel
                float VertexGapY = mix(0.9, 0, float(1 - abs(fract(pixelSize)) > 0.05));
                vec2 uvGap = vec2(VertexGapX / texSize.x, VertexGapY / texSize.y);

                // Currently don't clamp when uv coordinates are flipped (eg topLeft.u > bottomRight.u). This happens in UDMF when scale x/y is negative.
                bool topLeftFlag = topLeft == 1 && uvFlags == 0;
                bool bottomRightFlag = topLeft == 0 && uvFlags == 0;
                uvClampMinFrag.x = mix(uvClampMinFrag.x, uvFrag.x + uvGap.x, topLeftFlag);
                uvClampMinFrag.y = mix(uvClampMinFrag.y, uvFrag.y + uvGap.y, topLeftFlag);

                uvClampMaxFrag.x = mix(uvClampMaxFrag.x, uvFrag.x - uvGap.x, bottomRightFlag);
                uvClampMaxFrag.y = mix(uvClampMaxFrag.y, uvFrag.y - uvGap.y, bottomRightFlag);
            }
    )GLSL";
}

FString VertexFunction::VertexOptionsSet()
{
	return R"GLSL(
            int intOptions = floatBitsToInt(surfaceOptions);
            alphaFrag = (intOptions & 0xFF) / 255.0;
            float topLeft = float((intOptions >> 8) & 1);
            addAlphaFrag = float((intOptions >> 9) & 1);
            upperFrag = float((intOptions >> 10) & 1);
            lowerFrag =  float((intOptions >> 11) & 1);
            float overrideLightIndex = float((intOptions >> 12) & 1);
            boundTextureIndex = float((intOptions >> 13) & 0xFFFFF);)GLSL";
}

FString VertexFunction::ColorMapAndLightLevelSet()
{
	return R"GLSL(
            int lightLevelIndexAndLightLevel = floatBitsToInt(renderOptions);
            // UZDoom: the vertex light level is 9 bit two's complement (bits 0-7 + sign in bit 31): relative side / plane light can be negative.
            vertexLightLevelFrag = float(lightLevelIndexAndLightLevel & 0xFF) - 256.0 * float(lightLevelIndexAndLightLevel < 0);
            float lightLevelBufferIndex = float((lightLevelIndexAndLightLevel >> 10) & 0x1FFFFF);
            colorMapIndexFrag = lightLevelBufferIndex;
            lightLevelBufferIndex = mix(lightLevelBufferIndex, overrideLightIndex - 1, float(overrideLightIndex > 0));
            uvFlags = float((lightLevelIndexAndLightLevel >> 8) & 0x3);)GLSL";
}

FString VertexFunction::LightLevelAddAndMapIdSet()
{
	return R"GLSL(
            int lightLevelAndMapId = floatBitsToInt(lightLevelAdd);
            float lightLevelAddValue = float((lightLevelAndMapId >> 1) & 0xFF);
            mapIdFrag = float((lightLevelAndMapId >> 9) & 0xFFFFFF);
            lightLevelAddValue = mix(lightLevelAddValue, -lightLevelAddValue, float(lightLevelAndMapId & 1));)GLSL";
}

}
