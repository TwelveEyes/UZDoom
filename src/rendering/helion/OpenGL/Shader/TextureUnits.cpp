/*
** TextureUnits.cpp
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
*/

#include <cassert>
#include "TextureUnits.h"

namespace Helion::Render::OpenGL::Shader
{

static TextureUnits::Entry s_units[MaxTextureUnits];
static unsigned s_nextVersion = 1;

static TextureUnits::Entry& Slot(TextureUnit unit)
{
	int index = ToInt(unit);
	assert(index >= 0 && index < MaxTextureUnits);
	return s_units[index];
}

void TextureUnits::BindTexture(TextureUnit unit, IHardwareTexture* texture, int clampmode, int filter)
{
	Entry& entry = Slot(unit);
	entry = Entry();
	entry.kind = Kind::Texture;
	entry.texture = texture;
	entry.clampmode = clampmode;
	entry.filter = filter;
	entry.version = s_nextVersion++;
}

void TextureUnits::BindTexelBuffer(TextureUnit unit, IDataBuffer* buffer)
{
	Entry& entry = Slot(unit);
	entry = Entry();
	entry.kind = Kind::TexelBuffer;
	entry.buffer = buffer;
	entry.version = s_nextVersion++;
}

void TextureUnits::BindRenderTarget(TextureUnit unit, IRenderTarget* target, int attachment)
{
	Entry& entry = Slot(unit);
	entry = Entry();
	entry.kind = Kind::RenderTarget;
	entry.target = target;
	entry.attachment = attachment;
	entry.version = s_nextVersion++;
}

void TextureUnits::BindScene(TextureUnit unit, bool depth)
{
	Entry& entry = Slot(unit);
	entry = Entry();
	entry.kind = depth ? Kind::SceneDepth : Kind::SceneColor;
	entry.version = s_nextVersion++;
}

void TextureUnits::Unbind(TextureUnit unit)
{
	Entry& entry = Slot(unit);
	entry = Entry();
	entry.version = s_nextVersion++;
}

const TextureUnits::Entry& TextureUnits::Get(TextureUnit unit)
{
	return Slot(unit);
}

}
