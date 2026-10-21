/*
** TextureUnits.h
**
** UZDoom-only. The texture unit table that replaces GL.ActiveTexture + BindTexture.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion binds a texture to a unit (GL.ActiveTexture(unit); texture.Bind()) and then
** points the sampler uniform of the current program at that unit. The render
** program API binds resources per program binding index instead, so the units
** are a table here: Bind* records what the unit holds, ProgramUniforms::Set(unit,
** location) records which unit a sampler reads, and RenderProgram::FlushState
** forwards the pair to FRenderState::SetProgramTexture/TexelBuffer/... before a draw.
**
*/

#pragma once

#include "OpenGL/Util/TextureUnit.h"
#include "hwrenderer/data/hw_renderprogram.h"

namespace Helion::Render::OpenGL::Shader
{

class TextureUnits
{
public:
	enum class Kind
	{
		None,
		Texture,		// IHardwareTexture (2D or 2D array)
		TexelBuffer,	// samplerBuffer / usamplerBuffer
		RenderTarget,	// attachment of an IRenderTarget
		SceneColor,		// FRenderState::CopySceneColor() copy
		SceneDepth,
	};

	struct Entry
	{
		Kind kind = Kind::None;
		IHardwareTexture* texture = nullptr;
		IDataBuffer* buffer = nullptr;
		IRenderTarget* target = nullptr;
		int attachment = 0;
		int clampmode = 0;	// CLAMP_*
		int filter = 0;		// 0 = gl_texture_filter, 1 = nearest, 2 = linear
		unsigned version = 0;	// changes whenever the unit is rebound
	};

	static void BindTexture(TextureUnit unit, IHardwareTexture* texture, int clampmode = 0, int filter = 0);
	static void BindTexelBuffer(TextureUnit unit, IDataBuffer* buffer);
	static void BindRenderTarget(TextureUnit unit, IRenderTarget* target, int attachment);
	static void BindScene(TextureUnit unit, bool depth);
	static void Unbind(TextureUnit unit);

	static const Entry& Get(TextureUnit unit);
};

}
