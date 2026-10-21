/*
** IArrayTexture.h
**
** Port of Helion.Render.OpenGL.Texture.Legacy.IArrayTexture.
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

#pragma once

#include "zstring.h"
#include "Common/Textures/IRenderableTextureHandle.h"

namespace Helion::Render::OpenGL::Texture::Legacy
{

class IArrayTexture
{
public:
	virtual ~IArrayTexture() = default;

	virtual const FString& FetchTextureName() const = 0;
	virtual Common::Textures::IRenderableTextureHandle* Handle() const = 0;
	virtual void SetHandle(Common::Textures::IRenderableTextureHandle* handle) = 0;
	virtual int ResolvedHeight() const = 0;
	virtual void SetResolvedHeight(int height) = 0;
};

}
