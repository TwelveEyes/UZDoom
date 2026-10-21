/*
** BindTextures.h
**
** Port of Helion.Render.BindTextures.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The texture units a Helion program samples its resources from. A unit is a
** slot of Shader/TextureUnits.h (there is no glActiveTexture); ProgramUniforms
** points the sampler uniform of the program at the unit and RenderProgram
** forwards the slot to FRenderState::SetProgram* at draw time.
**
*/

#pragma once

#include "OpenGL/Util/TextureUnit.h"

namespace Helion::Render
{

struct BindTextures
{
	static constexpr OpenGL::TextureUnit BoundTexture = OpenGL::TextureUnit::Texture0;
	static constexpr OpenGL::TextureUnit SectorLight = OpenGL::TextureUnit::Texture1;
	static constexpr OpenGL::TextureUnit Colormap = OpenGL::TextureUnit::Texture2;
	static constexpr OpenGL::TextureUnit SectorColormap = OpenGL::TextureUnit::Texture3;
	static constexpr OpenGL::TextureUnit AccumTexture = OpenGL::TextureUnit::Texture4;
	static constexpr OpenGL::TextureUnit AccumCountTexture = OpenGL::TextureUnit::Texture5;
	static constexpr OpenGL::TextureUnit FuzzTexture = OpenGL::TextureUnit::Texture6;
	static constexpr OpenGL::TextureUnit OpaqueTexture = OpenGL::TextureUnit::Texture7;
	static constexpr OpenGL::TextureUnit WallClipTexture = OpenGL::TextureUnit::Texture8;
	static constexpr OpenGL::TextureUnit MapLineData = OpenGL::TextureUnit::Texture9;
	static constexpr OpenGL::TextureUnit PlaneClipTexture = OpenGL::TextureUnit::Texture10;
	static constexpr OpenGL::TextureUnit LineHeights = OpenGL::TextureUnit::Texture11;
	static constexpr OpenGL::TextureUnit BrightmapTexture = OpenGL::TextureUnit::Texture12;
	static constexpr OpenGL::TextureUnit SectorFog = OpenGL::TextureUnit::Texture13;
	// UZDoom-only: sector glow texel buffer (gl_glow, GLDEFS glow, UDMF glow colours).
	static constexpr OpenGL::TextureUnit SectorGlow = OpenGL::TextureUnit::Texture14;
	// UZDoom-only: texture2 .. texture12 of the material programs (FMaterial layers 1 .. 11) are Texture16 .. Texture26.
	static constexpr OpenGL::TextureUnit MaterialLayer1 = OpenGL::TextureUnit::Texture16;
};

}
