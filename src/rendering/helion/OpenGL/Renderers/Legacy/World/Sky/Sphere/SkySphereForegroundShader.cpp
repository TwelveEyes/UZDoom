/*
** SkySphereForegroundShader.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkySphereForegroundShader.
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

#include "SkySphereForegroundShader.h"
#include "OpenGL/Renderers/Legacy/World/Shader/FragFunction.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderText.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

using namespace Shader;

FString SkySphereForegroundShader::VertexShader()
{
	return SphereVertexShader();
}

std::optional<FString> SkySphereForegroundShader::FragmentShader()
{
	FString text = R"GLSL(
        #version 330

        in vec3 dirFrag;

        out vec4 fragColor;

        uniform sampler2DArray boundTexture;
        uniform samplerBuffer colormapTexture;
        uniform int hasInvulnerability;
        uniform int paletteIndex;
        uniform int colormapIndex;
        uniform vec3 colorMix;
        uniform float gammaCorrection;

        ${DomeFunctions}

        // UZDoom: the second layer of FSkyVertexBuffer (SKYMODE_SECONDLAYER): no caps, the transparent texels
        // (Alpha_Greater 0) and the faded top and bottom rows show the main layer, drawn translucent over it.
        void main() {
            float fade;
            vec2 texCoord = domeCoord(dirFrag, fade);
            fragColor = texture(boundTexture, vec3(texCoord, 0));

            if (fragColor.a == 0 || fade == 0)
                discard;

            float alpha = fade * fragColor.a;
            ${ColorMapFetch}
            fragColor.xyz *= min(colorMix, 1);
            ${InvulnerabilityFragColor}
            fragColor.a = alpha;
        }
    )GLSL";

	text = Replace(text, "${DomeFunctions}", SkySphereShader::DomeFunctions());
	text = Replace(text, "${InvulnerabilityFragColor}", FragFunction::InvulnerabilityFragColor());
	text = Replace(text, "${ColorMapFetch}", FragFunction::ColorMapFetch(false, ColorMapFetchContext::Default));
	return text;
}

}
