/*
** GLLegacyTexture.cpp
**
** Port of Helion.Render.OpenGL.Texture.Legacy.GLLegacyTexture.
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

#include "textures.h"
#include "gametexture.h"
#include "v_video.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "GLLegacyTexture.h"
#include "OpenGL/Shader/TextureUnits.h"
#include "OpenGL/Shader/ProgramUniforms.h"
#include "BindTextures.h"

namespace Helion::Render::OpenGL::Texture::Legacy
{

using namespace Geometry;

int GLLegacyTexture::SamplerFilter = 0;

GLLegacyTexture::GLLegacyTexture(int textureId, const FString& name, Geometry::Dimension dimension, Vec2I offset, Resources::ResourceNamespace ns,
	TextureTarget target, int transparentPixelCount, int blankRowsFromTop, int blankRowsFromBottom, IHardwareTexture* hwTexture,
	bool ownsTexture, TextureContext textureContext)
	// The target argument is ignored like in Helion: every legacy texture is a Texture2DArray.
	: GLTexture(textureId, name, dimension, offset, ns, TextureTarget::Texture2DArray, transparentPixelCount, blankRowsFromTop, blankRowsFromBottom,
		hwTexture, ownsTexture, textureContext)
{
}

IHardwareTexture* GLLegacyTexture::HardwareTexture() const
{
	return ParentArrayTexture != nullptr ? ParentArrayTexture->HardwareTexture() : GLTexture::HardwareTexture();
}

void GLLegacyTexture::Bind(TextureUnit unit) const
{
	// TextureFlags::ClampX/ClampY equal CLAMP_X/CLAMP_Y; the backend sampler also needs the filter mode.
	Shader::TextureUnits::BindTexture(unit, HardwareTexture(), (int)Flags & (int)CLAMP_XY, SamplerFilter);

	if (unit == BindTextures::BoundTexture)
		SetWarpUniform();
}

// UZDoom-only: the SHADER_Warp1/2 texture warp (ANIMDEFS warp/warp2) of the world programs. Warped textures are never
// layers of a shared array (CanTextureArray), so the texture bound for a draw decides it. Programs without the uniform
// (GetLocation -1) ignore it. warpParams = (warp type 0/1/2, timer of func_warp: seconds * shader speed). The warp type is
// FGameTexture::isWarped(), which FMaterial turns into SHADER_Warp1/2 (the shader index of the game texture stays 0); the
// timer counts from FRenderState::firstFrame like the table shaders.
void GLLegacyTexture::SetWarpUniform() const
{
	Shader::ProgramUniforms* uniforms = Shader::ProgramUniforms::Current();
	if (uniforms == nullptr)
		return;

	static const FString WarpParamsName = "warpParams";
	int location = uniforms->GetLocation(WarpParamsName);
	if (location < 0)
		return;

	Geometry::Vec2F warpParams(0, 0);
	if (Source != nullptr && Source->isWarped())
	{
		FRenderState* state = screen->RenderState();
		warpParams = Geometry::Vec2F((float)Source->isWarped(), (float)((double)(screen->FrameTime - state->firstFrame) * (double)Source->GetShaderSpeed() / 1000.0));
	}
	Shader::ProgramUniforms::Set(warpParams, location);
}

void GLLegacyTexture::Unbind(TextureUnit unit)
{
	Shader::TextureUnits::Unbind(unit);
}

FString GLLegacyTexture::ToString() const
{
	FString result;
	result.Format("%d:%s [%d x %d]", TextureId, Name.GetChars(), Dimension().Width, Dimension().Height);
	return result;
}

}
