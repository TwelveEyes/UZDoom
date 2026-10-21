/*
** SkySphereShader.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkySphereShader.
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

#include "SkySphereShader.h"
#include "SkySphereVertex.h"
#include "OpenGL/Renderers/Legacy/World/Shader/FragFunction.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderText.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderVars.h"
#include "OpenGL/Vertex/Attributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

using namespace Shader;

void SkySphereShader::ProgramCreated()
{
	m_boundTextureLocation = Uniforms.GetLocation("boundTexture");
	m_colormapTextureLocation = Uniforms.GetLocation("colormapTexture");
	m_mvpLocation = Uniforms.GetLocation("mvp");
	m_hasInvulnerabilityLocation = Uniforms.GetLocation("hasInvulnerability");
	m_flipULocation = Uniforms.GetLocation("flipU");
	m_paletteIndexLocation = Uniforms.GetLocation("paletteIndex");
	m_colorMapIndexLocation = Uniforms.GetLocation("colormapIndex");
	m_domeTextureLocation = Uniforms.GetLocation("domeTexture");
	m_domeModelLocation = Uniforms.GetLocation("domeModel");
	m_topColorLocation = Uniforms.GetLocation("topColor");
	m_bottomColorLocation = Uniforms.GetLocation("bottomColor");
	m_colorMixLocation = Uniforms.GetLocation("colorMix");
	m_gammaCorrectionLocation = Uniforms.GetLocation("gammaCorrection");
}

const TArray<Vertex::VaoAttribute>* SkySphereShader::VertexLayout() const
{
	return &Vertex::Attributes::ReadStructAttributes<SkySphereVertex>();
}

FString SkySphereShader::SphereVertexShader()
{
	return R"GLSL(
        #version 330

        layout(location = 0) in vec3 pos;
        layout(location = 1) in vec2 uv;

        out vec2 uvFrag;
        out vec3 dirFrag;

        uniform mat4 mvp;
        uniform int flipU;

        void main() {
            uvFrag = uv;
            if (flipU == 1)
                uvFrag.x = -uvFrag.x;
            // UZDoom: the view direction (sphere coordinates) of the vertex. SkySphereRenderer::CalculateMvp scales the
            // unit sphere to r = 0.5 and puts the eye at y = 0.1.
            dirFrag = pos * 0.5 - vec3(0.0, 0.1, 0.0);
            gl_Position = mvp * vec4(pos, 1.0);
            #ifdef VULKAN_COORDINATE_SYSTEM
            gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
            #endif
        }
    )GLSL";
}

FString SkySphereShader::VertexShader()
{
	return SphereVertexShader();
}

FString SkySphereShader::FetchTopBottomColors()
{
	if (ShaderVars::PaletteColorMode)
	{
		return R"GLSL(
int topTexIndex = lightLevelOffset + (useColormap * colormapSize) + (usePalette * paletteSize + int(topColor.r * 255.0));
vec4 topFetchColor = vec4(texelFetch(colormapTexture, topTexIndex).rgb, 1);
int bottomTexIndex = lightLevelOffset + (useColormap * colormapSize) + (usePalette * paletteSize + int(bottomColor.r * 255.0));
vec4 bottomFetchColor = vec4(texelFetch(colormapTexture, bottomTexIndex).rgb, 1);)GLSL";
	}

	return R"GLSL(
vec4 topFetchColor = topColor;
vec4 bottomFetchColor = bottomColor;
)GLSL";
}

// UZDoom: the sky sphere draws the layers with the texture mapping of the classic sky dome (FSkyVertexBuffer,
// the classic sky dome) instead of Helion's MidTexel mapping, so every sky texture height, the texture sky offset, the
// forced tiled skies and the sky transfer y offset place the texture like before. For the view direction of the
// fragment, domeCoord finds the point of the dome (the 4 rows of the upper and the 4 rows of the lower hemisphere,
// with the model matrix of SetupMatrices and the pixel stretch of the view matrix) the direction goes through and
// returns its texture coordinates after the texture matrix. fade is the vertex alpha there: 0 at the top and the
// bottom ring (the caps show through), 1 elsewhere.
FString SkySphereShader::DomeFunctions()
{
	return R"GLSL(
        // x: texture u scale (negative = mirrored), y: texture v scale, z: u offset (turns), w: v offset (texture heights)
        uniform vec4 domeTexture;
        // x: model y scale, y: model y translation, z: pixel stretch
        uniform vec3 domeModel;

        vec2 domeRingPoint(int k) {
            int r = k <= 4 ? k : 8 - k;
            float a = radians(15.0 * float(4 - r));
            float height = 10000.0 * sin(a);
            float y = (k <= 4 ? height : -height) + (r != 4 ? 300.0 : 0.0) - 1.0;
            return vec2(10000.0 * cos(a), (y * domeModel.x + domeModel.y) * domeModel.z);
        }

        vec2 domeCoord(vec3 dir, out float fade) {
            vec2 d = vec2(length(dir.xz), dir.y);
            float v = d.y > 0.0 ? -1.0 : 3.0;
            vec2 p = domeRingPoint(0);
            float side = p.y * d.x - p.x * d.y;
            if (side > 0.0) {
                for (int k = 1; k <= 8; k++) {
                    vec2 q = domeRingPoint(k);
                    float qside = q.y * d.x - q.x * d.y;
                    if (qside <= 0.0) {
                        v = (float(k - 1) + side / (side - qside)) / 4.0;
                        break;
                    }
                    side = qside;
                }
            }
            else {
                v = -1.0;
            }
            fade = clamp(min(v, 2.0 - v) * 4.0, 0.0, 1.0);
            // The world angle of the direction; the sphere is in <X, Z, -Y>.
            float u = degrees(atan(-dir.z, dir.x)) / 360.0;
            return vec2(domeTexture.x * (u + domeTexture.z), domeTexture.y * (v + domeTexture.w));
        }
    )GLSL";
}

std::optional<FString> SkySphereShader::FragmentShader()
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

        uniform vec4 topColor;
        uniform vec4 bottomColor;

        ${DomeFunctions}

        void main() {
            float fade;
            vec2 texCoord = domeCoord(dirFrag, fade);
            fragColor = texture(boundTexture, vec3(texCoord, 0));

            ${ColorMapFetch}
            ${FetchTopBottomColors}
            // The caps (top / bottom colour) under the faded top and bottom rows of the dome.
            vec4 capColor = dirFrag.y > 0.0 ? topFetchColor : bottomFetchColor;
            fragColor = vec4(mix(capColor.rgb, fragColor.rgb, fade), 1);
            fragColor.xyz *= min(colorMix, 1);
            ${InvulnerabilityFragColor}
            ${GammaCorrection}
        }
    )GLSL";

	text = Replace(text, "${DomeFunctions}", DomeFunctions());
	text = Replace(text, "${FetchTopBottomColors}", FetchTopBottomColors());
	text = Replace(text, "${InvulnerabilityFragColor}", FragFunction::InvulnerabilityFragColor());
	text = Replace(text, "${ColorMapFetch}", FragFunction::ColorMapFetch(false, ColorMapFetchContext::Default));
	text = Replace(text, "${GammaCorrection}", FragFunction::GammaCorrection());
	return text;
}

}
