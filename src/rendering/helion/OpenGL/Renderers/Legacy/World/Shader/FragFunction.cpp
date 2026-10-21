/*
** FragFunction.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Shader.FragFunction.
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

#include "FragFunction.h"
#include "ShaderVars.h"
#include "ShaderText.h"
#include "OpenGL/Renderers/Legacy/World/ShaderUniforms.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterFragPrelude.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

FString FragFunction::VertexDistVar3D()
{
	return "in float dist3D;";
}

FString FragFunction::OitFragVariables(OitOptions options)
{
	switch (options)
	{
	case OitOptions::OitTransparentPass:
		return R"GLSL(
                  layout (location = 0) out vec4 accum;
                  layout (location = 1) out vec2 accumCount;
                  layout (location = 2) out float outFuzz;
                )GLSL";
	case OitOptions::OitCompositePass:
		return R"GLSL(
                    uniform sampler2D accum;
                    uniform sampler2D accumCount;

                    // get the max value between three values
                    float max3(vec3 v)
                    {
	                    return max(max(v.x, v.y), v.z);
                    }
                    )GLSL";
	case OitOptions::OitFuzzRefractionPass:
		return R"GLSL(
                    uniform sampler2D accum;
                    uniform sampler2D accumCount;
                    uniform sampler2D fuzzTexture;
                    uniform sampler2D opaqueTexture;

                    float max3(vec3 v)
                    {
	                    return max(max(v.x, v.y), v.z);
                    }
                    )GLSL";
	default:
		return "";
	}
}

FString FragFunction::FuzzFunction()
{
	return R"GLSL(
        // These two functions are found here:
        // https://gist.github.com/patriciogonzalezvivo/670c22f3966e662d2f83
        float rand(vec2 n) {
            return fract(sin(dot(n, vec2(12.9898, 4.1414))) * 43758.5453);
        }

        float noise(vec2 p) {
            vec2 ip = floor(p);
            vec2 u = fract(p);
            u = u * u * (3.0 - 2.0 * u);

            float res = mix(
	            mix(rand(ip), rand(ip + vec2(1.0, 0.0)), u.x),
	            mix(rand(ip + vec2(0.0, 1.0)), rand(ip + vec2(1.0, 1.0)), u.x), u.y);
            return res * res;
        })GLSL";
}

FString FragFunction::AlphaFlag(bool lightLevel)
{
	if (ShaderVars::PaletteColorMode)
		return "";

	FString text = R"GLSL(
            // Check for the reserved alpha value to indicate a full bright pixel.
            float fullBrightFlag = float(fragColor.w == 0.9960784313725490196078431372549);
            )GLSL";
	text << (lightLevel ? "lightLevel = mix(lightLevel, 1, fullBrightFlag);\n" : "");
	text << "fragColor.w = mix(fragColor.w, 1, fullBrightFlag);\n";
	return text;
}

FString FragFunction::BrightMapLightColorIndexFetch(const FString& uvVar)
{
	FString text;
	text << "\n            vec3 brightColor = texture(brightmapTexture, vec3(" << uvVar << ", boundTextureIndex)).rgb;\n"
		<< "            float hasBrightColor = float(brightColor.r != 0 && brightColor.r == brightColor.g && brightColor.g == brightColor.b);\n"
		<< "            lightColorIndex = int(mix(lightColorIndex, min(int((1 - brightColor.r) * 31), lightColorIndex), hasBrightColor));\n        ";
	return text;
}

FString FragFunction::ColorMapFetch(bool lightLevel, ColorMapFetchContext ctx)
{
	if (!ShaderVars::PaletteColorMode)
		return "";

	FString indexAdd;
	if (lightLevel)
	{
		// sectorColorMapIndexFrag is overriding the colormapIndex uniform
		// Only use brightmap if grayscale to increase the light level
		indexAdd = R"GLSL(
            ${BrigthmapFetch}

            int useColormap = int(mix(colormapIndex, sectorColorMapIndexFrag.r, float(sectorColorMapIndexFrag.r > 0)));
            ${EntityColorMapFrag}
            int usePalette = paletteIndex;
            int lightLevelOffset = (lightColorIndex * 256);
            lightLevelOffset = int(mix(lightLevelOffset, 32 * 256, float(hasInvulnerability)));)GLSL";
		// if useColormap is not default(0) then override with the uniform colormap. This overrides translations with boom colormaps etc.
		indexAdd = Replace(indexAdd, "${EntityColorMapFrag}", ctx == ColorMapFetchContext::Entity ?
			"useColormap = int(mix(useColormap, colorMapTranslationFrag, float(useColormap == 0)));" : "");
		indexAdd = Replace(indexAdd, "${BrigthmapFetch}", BrightMapLightColorIndexFetch("texUV"));
	}
	else
	{
		indexAdd = R"GLSL(
            int useColormap = colormapIndex${HudClearColorMap};
            int usePalette = paletteIndex;
            ${LightOffset}
            lightLevelOffset = int(mix(lightLevelOffset, 32 * 256, float(hasInvulnerability${HudDrawColorMapFrag})));
            ${HudClearPalette})GLSL";
		indexAdd = Replace(indexAdd, "${HudClearColorMap}", ctx == ColorMapFetchContext::Hud && ShaderVars::PaletteColorMode ? "* int(drawColorMapFrag)" : "");
		indexAdd = Replace(indexAdd, "${HudDrawColorMapFrag}", ctx == ColorMapFetchContext::Hud ? "* drawColorMapFrag" : "");
		indexAdd = Replace(indexAdd, "${HudClearPalette}", ctx == ColorMapFetchContext::Hud && ShaderVars::PaletteColorMode ?
			"usePalette = int(mix(0, usePalette, float(drawPaletteFrag)));" : "");

		FString lightOffset = "int lightLevelOffset = 0;";
		if (ctx == ColorMapFetchContext::Hud)
		{
			lightOffset = R"GLSL(
                int lightColorIndex = int(hudColorMapIndexFrag);
                ${BrigthmapFetch}
                int lightLevelOffset = lightColorIndex * 256;)GLSL";
			lightOffset = Replace(lightOffset, "${BrigthmapFetch}", BrightMapLightColorIndexFetch("uvFrag.st"));
		}
		indexAdd = Replace(indexAdd, "${LightOffset}", lightOffset);
	}

	// Use the alpha flag to indicate we need to fetch from the colormap buffer since we don't need it for fullbright.
	FString text = R"GLSL(
                const int paletteSize = 256 * 34;
                const int colormapSize = paletteSize * 14;
                float colormapFetchFlag = float(fragColor.w == 0.9960784313725490196078431372549);
                fragColor.w = mix(fragColor.w, 1, colormapFetchFlag);
                ${IndexAdd}
                int texIndex = lightLevelOffset + (useColormap * colormapSize) + (usePalette * paletteSize + int(fragColor.r * 255.0));
                vec3 fetchColor = texelFetch(colormapTexture, texIndex).rgb;
                fragColor.rgb = mix(fragColor.rgb, fetchColor, vec3(colormapFetchFlag, colormapFetchFlag, colormapFetchFlag));
                )GLSL";
	return Replace(text, "${IndexAdd}", indexAdd);
}

FString FragFunction::GetTextureMappingClamp(FragColorFunctionOptions options)
{
	if (!HasOption(options, FragColorFunctionOptions::VertexGapClampUV))
		return "vec2 texUV = uvFrag;";

	return "vec2 texUV = clamp(uvFrag, uvClampMinFrag, uvClampMaxFrag);";
}

// UZDoom-only: the truecolor light stage of the array programs, which replaces Helion's light level, brightmap, sector colour and
// fog steps: HelionGetLight (gl_lightmode, sector light colour, fog density rules) + wall glow + brightmap + coloured fog, in the
// order of main.fp getLightColor / main(). Defines fogFactor (amount of fog) for the fog barrier / fuzz steps that follow.
FString FragFunction::UzdoomLight(FragColorFunctionOptions options)
{
	FString text = R"GLSL(
            pixelpos = vec4(worldPosFrag, dist2D);
            vec3 faceNormal = normalize(cross(dFdx(pixelpos.xyz), dFdy(pixelpos.xyz)));
            if (dot(faceNormal, uCameraPos.xyz - pixelpos.xyz) < 0.0)
                faceNormal = -faceNormal;
            vWorldNormal = vec4(faceNormal, 1.0);
)GLSL";
	text << AlphaFlag(false);
	text << R"GLSL(
            float uzFullbright = max(fullBrightFlag, float(lightLevelMix >= 1.0));
            fragColor.rgb = HelionDesaturate(fragColor.rgb, uzLightFrag.w);
            vec4 uzLevel = HelionLevelLight();
            HelionLight uzLight = HelionGetLight(vec2(uzLightFrag.x, uzLightFrag.y + float(extraLight) * uzLevel.w), sectorColorMapIndexFrag, sectorFogColorFrag, uzFullbright, uzLevel);
            vec3 uzColor = min(uzLight.color + HelionWallGlow(uzLightFrag.z, uzLightFrag.w) * (1.0 - uzFullbright), vec3(1.0));
)GLSL";
	if (HasOption(options, FragColorFunctionOptions::Brightmaps))
	{
		text << R"GLSL(
            if (useBrightmaps == 1)
                uzColor = min(uzColor + texture(brightmapTexture, vec3(texUV, boundTextureIndex)).rgb, vec3(1.0));
)GLSL";
	}
	if (HasOption(options, FragColorFunctionOptions::DynamicLights))
	{
		text << R"GLSL(
            fragColor.rgb = HelionApplyFog(uzLight, HelionDynamicLight(fragColor.rgb, uzColor, uzLightFrag.w));
)GLSL";
	}
	else
	{
		text << R"GLSL(
            fragColor.rgb = HelionApplyFog(uzLight, fragColor.rgb * uzColor);
)GLSL";
	}
	text << R"GLSL(
            float fogFactor = float(uzLight.fogEnabled != 0) * (1.0 - uzLight.fogFactor);
)GLSL";
	return text;
}

FString FragFunction::MaterialColor(const FString& declareFragColor, const FString& textureMappingClamp)
{
	// The contract globals (vTexCoord...) are what the mod shaders read; they are plain globals here because the
	// values come from the Helion varyings, not from a vertex shader written for the contract.
	FString text;
	// The cluster lookup of the dynamic lights (ClusterFragPrelude::MainStart): the first statement of the contract part of main().
	text << "\n" << Lights::ClusterFragPrelude::MainStart();
	text << "\n        " << textureMappingClamp << R"GLSL(
        if (materialFlipY != 0)
            texUV.y = -texUV.y;
        vTexCoord = vec4(texUV, 0.0, 1.0);
        vColor = vec4(1.0);
        pixelpos = vec4(worldPosFrag, dist2D);
        glowdist = vec3(1.0e6, 1.0e6, 0.0);
        gradientdist = vec3(1.0e6, 1.0e6, 0.0);
        vLightmap = lightmapFrag;
        vec3 faceNormal = normalize(cross(dFdx(pixelpos.xyz), dFdy(pixelpos.xyz)));
        if (dot(faceNormal, uCameraPos.xyz - pixelpos.xyz) < 0.0)
            faceNormal = -faceNormal;
        vWorldNormal = vec4(faceNormal, 1.0);
        vEyeNormal = vec4(normalize((NormalViewMatrix * vec4(faceNormal, 1.0)).xyz), 1.0);

#ifndef LEGACY_USER_SHADER
        Material material;
        material.Base = vec4(0.0);
        material.Bright = vec4(0.0);
        material.Glow = vec4(0.0);
        material.Normal = vec3(0.0);
        material.Specular = vec3(0.0);
        material.Glossiness = 0.0;
        material.SpecularLevel = 0.0;
        material.Metallic = 0.0;
        material.Roughness = 0.0;
        material.AO = 0.0;
        SetupMaterial(material);
#else
        Material material = ProcessMaterial();
#endif
#ifndef NO_ALPHATEST
        if (material.Base.a <= uAlphaThreshold)
            discard;
#endif
        )GLSL";
	text << declareFragColor << " = material.Base;";
	return text;
}

FString FragFunction::MaterialLightStage()
{
	return R"GLSL(
            float uzFullbright = float(lightLevelMix >= 1.0);
            vec4 uzLevel = HelionLevelLight();
            HelionLight uzLight = HelionGetLight(vec2(uzLightFrag.x, uzLightFrag.y + float(extraLight) * uzLevel.w), sectorColorMapIndexFrag, sectorFogColorFrag, uzFullbright, uzLevel);
            vec3 uzColor = uzLight.color + HelionWallGlow(uzLightFrag.z, uzLightFrag.w) * (1.0 - uzFullbright);
            fragColor.rgb = HelionApplyFog(uzLight, getHelionLightColor(material, uzColor).rgb);
            float fogFactor = float(uzLight.fogEnabled != 0) * (1.0 - uzLight.fogFactor);
)GLSL";
}

FString FragFunction::DitherTransparency()
{
	// main.fp: pixelpos.x / pixelpos.y are the world x and the height (worldPosFrag in the same frame); the brightness is the
	// value (rgb2hsv z) of the colour. The pattern index is taken modulo 8 on the positive side (the classic index goes
	// negative left of / below the origin). The kept pixels are halved in colour only: the classic opaque pass ignores the
	// alpha main.fp halves too, while the world programs here blend with it.
	return R"GLSL(
            if (ditherFrag > 0.5)
            {
                const float DITHER_THRESHOLDS[64] = float[64](
                    1.0 / 65.0, 33.0 / 65.0, 9.0 / 65.0, 41.0 / 65.0, 3.0 / 65.0, 35.0 / 65.0, 11.0 / 65.0, 43.0 / 65.0,
                    49.0 / 65.0, 17.0 / 65.0, 57.0 / 65.0, 25.0 / 65.0, 51.0 / 65.0, 19.0 / 65.0, 59.0 / 65.0, 27.0 / 65.0,
                    13.0 / 65.0, 45.0 / 65.0, 5.0 / 65.0, 37.0 / 65.0, 15.0 / 65.0, 47.0 / 65.0, 7.0 / 65.0, 39.0 / 65.0,
                    61.0 / 65.0, 29.0 / 65.0, 53.0 / 65.0, 21.0 / 65.0, 63.0 / 65.0, 31.0 / 65.0, 55.0 / 65.0, 23.0 / 65.0,
                    4.0 / 65.0, 36.0 / 65.0, 12.0 / 65.0, 44.0 / 65.0, 2.0 / 65.0, 34.0 / 65.0, 10.0 / 65.0, 42.0 / 65.0,
                    52.0 / 65.0, 20.0 / 65.0, 60.0 / 65.0, 28.0 / 65.0, 50.0 / 65.0, 18.0 / 65.0, 58.0 / 65.0, 26.0 / 65.0,
                    16.0 / 65.0, 48.0 / 65.0, 8.0 / 65.0, 40.0 / 65.0, 14.0 / 65.0, 46.0 / 65.0, 6.0 / 65.0, 38.0 / 65.0,
                    64.0 / 65.0, 32.0 / 65.0, 56.0 / 65.0, 24.0 / 65.0, 62.0 / 65.0, 30.0 / 65.0, 54.0 / 65.0, 22.0 / 65.0);
                int ditherIndex = (((int(worldPosFrag.x) % 8) + 8) % 8) * 8 + (((int(worldPosFrag.y) % 8) + 8) % 8);
                float ditherBrightness = clamp(1.5 * max(fragColor.r, max(fragColor.g, fragColor.b)), 0.1, 1.0);
                if (DITHER_THRESHOLDS[ditherIndex] < ditherBrightness)
                    discard;
                fragColor.rgb *= 0.5;
            }
)GLSL";
}

FString FragFunction::FragColorFunction(FragColorFunctionOptions options, ColorMapFetchContext ctx, OitOptions oitOptions, const FString& postProcess)
{
	bool material = HasOption(options, FragColorFunctionOptions::UzdoomMaterial);
	// UZDoom: the truecolor light stage (UzdoomLight / MaterialLightStage) replaces the light, sector colour and fog steps.
	bool uzdoom = material || (HasOption(options, FragColorFunctionOptions::UzdoomLightMode) && !ShaderVars::PaletteColorMode);
	FString declareFragColor = oitOptions == OitOptions::OitTransparentPass ? "vec4 fragColor" : "fragColor";
	FString textureMappingClamp = GetTextureMappingClamp(options);

	FString fragColor;
	if (material)
	{
		fragColor = MaterialColor(declareFragColor, textureMappingClamp);
	}
	else
	{
		fragColor << "\n        " << textureMappingClamp << "\n        if (warpParams.x > 0.5)\n            texUV = HelionWarpTexCoord(texUV);";
		fragColor << "\n        " << declareFragColor << " = texture(boundTexture, vec3(texUV, boundTextureIndex));";
	}

	FString text = fragColor;

	if (!material)
	{
		if (HasOption(options, FragColorFunctionOptions::Colormap))
		{
			text << ColorMapFetch(true, ctx);
			// UZDoom-only (plan D25): the clustered dynamic lights in the palette colour mode. Helion has no dynamic lights;
			// this is the classic modulated / subtractive / additive math (base * clamp(light + dynlight, 0, 1.4) + additive)
			// with the light level colour of the colormap fetch as base * light and the unlit row of the colormap as base.
			if (ShaderVars::PaletteColorMode && HasOption(options, FragColorFunctionOptions::DynamicLights))
			{
				text << R"GLSL(
            pixelpos = vec4(worldPosFrag, dist2D);
            vec3 palNormal = normalize(cross(dFdx(pixelpos.xyz), dFdy(pixelpos.xyz)));
            if (dot(palNormal, uCameraPos.xyz - pixelpos.xyz) < 0.0)
                palNormal = -palNormal;
            vWorldNormal = vec4(palNormal, 1.0);
            int palLightIndex = clusterLightIndex(gl_FragCoord, helionViewDepth > 0.0 ? helionViewDepth : 1.0 / gl_FragCoord.w);
            if (palLightIndex >= 0 && colormapFetchFlag > 0.5)
            {
                vec3 palBase = texelFetch(colormapTexture, texIndex - lightLevelOffset).rgb;
                ivec4 palRange = ivec4(lights[palLightIndex]) + ivec4(palLightIndex + 1);
                vec3 palDyn = vec3(0.0);
                for (int i = palRange.x; i < palRange.y; i += 4)
                    palDyn += HelionLightContribution(i, palNormal);
                for (int i = palRange.y; i < palRange.z; i += 4)
                    palDyn -= HelionLightContribution(i, palNormal);
                fragColor.rgb = clamp(fragColor.rgb + palBase * palDyn, vec3(0.0), max(fragColor.rgb, palBase * 1.4));
                vec3 palAdd = vec3(0.0);
                for (int i = palRange.z; i < palRange.w; i += 4)
                    palAdd += HelionLightContribution(i, palNormal);
                fragColor.rgb = clamp(fragColor.rgb + palAdd, 0.0, 1.0);
            }
)GLSL";
			}
		}

		if (uzdoom)
		{
			text << UzdoomLight(options);
		}
		else
		{
			text << AlphaFlag(true);
			text << GetBrightMapBlend(options);
		}
	}
	else
	{
		text << MaterialLightStage();
	}

	if (HasOption(options, FragColorFunctionOptions::AddAlpha))
	{
		text << "fragColor.w = fragColor.w * alphaFrag + addAlphaFrag;";
		text << GetClearAlpha(oitOptions);
	}

	if (HasOption(options, FragColorFunctionOptions::Alpha))
		text << "\nfragColor.w *= alphaFrag;";

	text << R"GLSL(
            if (fragColor.w <= 0.0)
                discard;

            fragColor.rgb *= min(colorMix, 1);
)GLSL";

	if (!uzdoom)
	{
		// If b channel is -1 then r channel is colormap index. if b channel is >= 0 then the rgb values are a true color mix.
		// This is to support Sector_SetColor to have true color mixes when still using palette color mode.
		text << "fragColor.rgb *= mix(vec3(1), min(sectorColorMapIndexFrag, 1), float(sectorColorMapIndexFrag.b >= 0));";
		text.AppendFormat(R"GLSL(
                float fogFactor = clamp(mix(1.0 - exp(-sectorFogColorFrag.a * dist3D), 0.5, float(useSectorFog == %d)), 0.0, 1.0);
                fragColor.rgb = mix(fragColor.rgb, sectorFogColorFrag.rgb, fogFactor);)GLSL", ShaderUniforms::ViewBlendFog);
	}

	// Fog barriers need to ignore texture color and just apply fog color + factor directly. Only relevant to the OIT transparent pass for level geometry.
	if (!(oitOptions != OitOptions::OitTransparentPass || ctx == ColorMapFetchContext::Entity))
		text << "fragColor.rgba = mix(fragColor.rgba, vec4(sectorFogColorFrag.rgb, fogFactor), fogBarrier);";

	text << InvulnerabilityFragColor();
	text << GammaCorrection();
	text << postProcess;
	text << Oit(oitOptions, options);
	return text;
}

FString FragFunction::GetBrightMapBlend(FragColorFunctionOptions options)
{
	if (ShaderVars::PaletteColorMode)
		return "\n";

	if (!HasOption(options, FragColorFunctionOptions::Brightmaps))
		return "fragColor.rgb *= lightLevel;";

	return R"GLSL(
            if (useBrightmaps == 1)
                fragColor.rgb *= min(vec3(1.0), texture(brightmapTexture, vec3(texUV, boundTextureIndex)).rgb + vec3(lightLevel));
            else
                fragColor.rgb *= lightLevel;)GLSL";
}

FString FragFunction::GetClearAlpha(OitOptions oitOptions)
{
	if (oitOptions != OitOptions::None)
		return "";

	// UZDoom: addAlphaFrag = 1 (opaque surfaces) makes the alpha 2.0. Helion's RGBA8 framebuffer clamps it to 1 before
	// blending; UZDoom's scene buffer is RGBA16F, which does not clamp, so SrcAlpha/OneMinusSrcAlpha would compute
	// 2 * src - dst. Clamp here to keep the fixed point semantics the blend state relies on.
	return R"GLSL(// Don't write partially transparent pixels for two-sided middle to fix issues with texture filtering.
                fragColor.w = mix(mix(0.0, 1.0, float(fragColor.a > 0.5)), fragColor.w, addAlphaFrag);
                fragColor.w = clamp(fragColor.w, 0.0, 1.0);)GLSL";
}

FString FragFunction::FuzzDist(FuzzRefractionOptions options)
{
	return options == FuzzRefractionOptions::World ? "fuzzDist" : "1";
}

FString FragFunction::FuzzRefractionFunction(FuzzRefractionOptions options)
{
	FString text;
	text << R"GLSL(
                ivec2 coords = ivec2(gl_FragCoord.x, gl_FragCoord.y);

                float fuzzDistStep = ceil((fuzzDiv/(max(1, )GLSL" << FuzzDist(options) << R"GLSL( / 96))));
                vec2 blockCoordinate = floor(coords / fuzzDistStep);
                float fuzzAlpha = clamp(noise(blockCoordinate * fuzzFrac), 0.2, 0.8);
                float offsetX = mix(-1, 1, float(fuzzAlpha > 0.3)) * int(fuzzDistStep * 4);
                float offsetY = mix(1, -1, float(fuzzAlpha < 0.4)) * int(fuzzDistStep * 4);
                float flipX = mix(1, -1, float(fuzzAlpha > 0.5));
                float flipY = mix(1, -1, float(fuzzAlpha < 0.35));
                float clearOffset = mix(1, 0, float(fuzzAlpha < 0.25));

                ${FuzzRefractCoords}

                ${FuzzRefractTexture}

                vec3 color = texelFetch(opaqueTexture, refractCoords, 0).rgb;
                fragColor = vec4(color, 1);

                ${FuzzRefractFragColor}
            )GLSL";

	// Don't pull pixels where fuzz wasn't written and don't refract past threshold
	text = Replace(text, "${FuzzRefractTexture}", options == FuzzRefractionOptions::World ?
		R"GLSL(float fuzz = texelFetch(fuzzTexture, refractCoords, 0).r;
                refractCoords = ivec2(mix(coords, refractCoords, fuzz));
                refractCoords = ivec2(mix(coords, refractCoords, float(dist2D < 800.0)));
                )GLSL"
		:
		"");

	text = Replace(text, "${FuzzRefractFragColor}", options == FuzzRefractionOptions::World ?
		R"GLSL(
                if (renderFuzzRefractionColor > 0) {
                    if (fuzzAlpha < 0.4)
                        discard;

                    vec4 fuzzColor = vec4(mix(color, ${FuzzBlackColor}, fuzzAlpha), 1);
                    vec2 counter = texelFetch(accumCount, refractCoords, 0).rg;
                    float alphaComponent = counter.r;
                    float countComponent = counter.g;

                    vec4 accumulation = texelFetch(accum, refractCoords, 0);

                    // Modified alpha so the fuzz effect pixels don't overly pop
                    float weight = max(exp(-dist3D * 0.0007), 0.16);
                    vec4 weightColor = vec4(fuzzColor.rgb * fuzzAlpha, fuzzAlpha * 0.9) * weight;

                    accumulation += weightColor;
                    alphaComponent += fuzzAlpha;
                    countComponent += 1;

                    if (isinf(max3(abs(accumulation.rgb))))
                      accumulation.rgb = vec3(accumulation.a);

                    vec3 average_color = accumulation.rgb / max(accumulation.a, 0.00001f);
                    fragColor = vec4(average_color, alphaComponent / countComponent);
                    // The original fragColor from the texture is mixed with the fog but is overwritten in this statement so it has to be remixed.
                    fragColor.rgb = mix(fragColor.rgb, sectorFogColorFrag.rgb, fogFactor);
                    // Increase alpha as the fog becomes more dense. Otherwise the fog color pops out too much when blending with other background colors.
                    fragColor.a *= (1 - (fogFactor * 0.5));
                })GLSL"
		:
		R"GLSL(
                fragColor = mix(
                    vec4(${FuzzBlackColor}, fuzzAlpha),
                    vec4(mix(color, ${FuzzBlackColor}, fuzzAlpha * 0.6), 1),
                    float(fuzzRefraction)
                );)GLSL");

	// Fetch black color from current palette. This takes the pre-blended black color with red/yellow/green palettes.
	text = Replace(text, "${FuzzBlackColor}", ShaderVars::PaletteColorMode ?
		"texelFetch(colormapTexture, usePalette * paletteSize).rgb" :
		"vec3(0, 0, 0)");

	text = Replace(text, "${FuzzRefractCoords}", options == FuzzRefractionOptions::World ?
		R"GLSL(      ivec2 refractCoords = ivec2(
                    clamp(coords.x + (offsetX*clearOffset*flipX), 0, screenBounds.x),
                    clamp(coords.y + (offsetY*clearOffset*flipY), 0, screenBounds.y));)GLSL"
		:
		R"GLSL(      ivec2 refractCoords = ivec2(
                    clamp((coords.x + (offsetX*clearOffset*flipX)) * fuzzSampleFactor.x + fuzzSampleOffset.x, 0, screenBounds.x),
                    clamp((coords.y + (offsetY*clearOffset*flipY)) * fuzzSampleFactor.y + fuzzSampleOffset.y, 0, screenBounds.y));)GLSL");

	return text;
}

FString FragFunction::Oit(OitOptions options, FragColorFunctionOptions fragColorOptions)
{
	if (options == OitOptions::None)
		return "";

	if (options == OitOptions::OitTransparentPass)
	{
		// Capping to 0.16 = ~2600 map units. Prevents far objects from being completely dominated by near objects.
		FString text = "float weight = max(exp(-dist3D * 0.0007), 0.16);";
		if (HasOption(fragColorOptions, FragColorFunctionOptions::Fuzz))
		{
			text << R"GLSL(
                outFuzz = fuzzFrag;
                float weightClear = mix(1, 0, fuzzFrag - renderFuzz);
                )GLSL";
		}
		else
		{
			text << "const float weightClear = 1;";
		}
		text << R"GLSL(
                accum = vec4(min(fragColor.rgb, vec3(colorClamp)) * fragColor.a, fragColor.a) * weight * weightClear;
                accumCount = vec2(fragColor.a * weightClear, 1 * weightClear);)GLSL";
		return text;
	}

	if (options == OitOptions::OitFuzzRefractionPass)
		return FuzzRefractionFunction(FuzzRefractionOptions::World);

	return R"GLSL(
            ivec2 coords = ivec2(gl_FragCoord.xy);

            // r is accumulated alpha, g is accumulation count
            vec2 counter = texelFetch(accumCount, coords, 0).rg;
            float alphaComponent = counter.r;
            float countComponent = counter.g;

            if (countComponent == 0)
                discard;

	        vec4 accumulation = texelFetch(accum, coords, 0);
	        if (isinf(max3(abs(accumulation.rgb))))
		        accumulation.rgb = vec3(accumulation.a);

	        // prevent floating point precision bug
	        vec3 average_color = accumulation.rgb / max(accumulation.a, 0.00001f);

	        fragColor = vec4(average_color, alphaComponent / countComponent);)GLSL";
}

FString FragFunction::GammaCorrection()
{
	// UZDoom: gamma is applied by the post-processing present pass (vid_gamma), the gammaCorrection uniform is always 1.0.
	// Helion's pow(fragColor.rgb, vec3(1.0/gammaCorrection)) is therefore a no-op, and on radeonsi/ACO (Mesa 26.2, RDNA3)
	// the separately compiled world fragment shaders miscompile it together with the alpha discard: whole 8x8 pixel waves
	// then get one saturated colour channel (the "green/red speckles"). Emit nothing.
	return "";
}

FString FragFunction::InvulnerabilityFragColor()
{
	if (ShaderVars::PaletteColorMode)
		return "";

	FString text = R"GLSL(
        if (hasInvulnerability != 0)
        {
            {InvulnerabilityFragColorInner}
        })GLSL";
	return Replace(text, "{InvulnerabilityFragColorInner}", InvulnerabilityFragColorInner());
}

FString FragFunction::InvulnerabilityFragColorInner()
{
	if (ShaderVars::PaletteColorMode)
		return "";

	FString text = R"GLSL(
        float gray = fragColor.x * 0.299 + fragColor.y * 0.587 + fragColor.z * 0.144;
        gray = 1 - gray;
        )GLSL";

	if (!ShaderVars::EmulateInvulnerabilityColorMap)
	{
		text << R"GLSL(
        fragColor.xyz = vec3(gray, gray, gray);
        )GLSL";
	}
	else
	{
		text << R"GLSL(
        // Map brightness to white + white + gray ramp (32x) + low grays (3x) + black
        // in invuln color map, to emulate palette mode's invuln effect.
        // These grays are not completely evenly spaced (most are 8 apart,
        // some are 4), but it's even enough to work.
        // Since some colormaps customize non-gray colors instead of just
        // mapping to brightness, the degree to which the original effect
        // is matched can vary by WAD.
        const float maxIndex = 38 - 1;
        int indexA = int(gray * maxIndex);
        int indexB = indexA + 1;
        float localPos = (gray - float(indexA) / maxIndex) * maxIndex;

        // [ 0,  1] => [ 4,   4]
        // [ 2, 33] => [80, 111]
        // [34, 36] => [ 5,   7]
        // [37, 37] => [ 0,   0]
        if (indexA <= 1) { indexA = 4; }
        else if (indexA <= 33) { indexA += 78; }
        else if (indexA <= 36) { indexA -= 29; }
        else { indexA = 0; }
        if (indexB <= 1) { indexB = 4; }
        else if (indexB <= 33) { indexB += 78; }
        else if (indexB <= 36) { indexB -= 29; }
        else { indexB = 0; }

        const int colorMapOffset = 256 * 32; // invuln colormap start
        vec3 blendA = texelFetch(colormapTexture, colorMapOffset + indexA).rgb;
        vec3 blendB = texelFetch(colormapTexture, colorMapOffset + indexB).rgb;
        fragColor.rgb = mix(blendA, blendB, localPos);
        )GLSL";
	}

	return text;
}

FString FragFunction::VertexGapVariables()
{
	return "flat in vec2 uvClampMinFrag; flat in vec2 uvClampMaxFrag;";
}

}
