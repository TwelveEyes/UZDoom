/*
** IArrayTextureLookup.h
**
** Port of Helion.Render.OpenGL.Texture.IArrayTextureLookup.
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

namespace Helion::Render::OpenGL::Texture
{

class IArrayTextureLookup
{
public:
	virtual ~IArrayTextureLookup() = default;

	// Returns unique base array texture handle given a texture handle from the TextureManager
	// If the texture does not have a parent array texture then the textureHandle parameter is returned
	virtual int GetWorldArrayTextureHandle(int textureHandle, bool repeatY) = 0;
};

}
