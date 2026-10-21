/*
** IImageDrawInfoProvider.h
**
** Port of Helion.Render.OpenGL.Shared.IImageDrawInfoProvider.
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
#include "Util/Geometry.h"
#include "Resources/ResourceNamespace.h"

namespace Helion::Render::OpenGL::Shared
{

/// <summary>
/// A helper class used with calculating the area that a string would take
/// up on the screen.
/// </summary>
class IImageDrawInfoProvider
{
public:
	virtual ~IImageDrawInfoProvider() = default;

	/// <summary>
	/// Checks if the image exists currently in the texture manager.
	/// </summary>
	/// <param name="image">The image name.</param>
	/// <returns>True if the image exists, false if not.</returns>
	virtual bool ImageExists(const FString& image) = 0;

	/// <summary>
	/// Gets the dimension of the image.
	/// </summary>
	/// <param name="image">The name of the image.</param>
	/// <param name="resourceNamespace">The namespace of the resource. This
	/// is optional.</param>
	/// <returns>The dimension of the image.</returns>
	virtual Geometry::Dimension GetImageDimension(const FString& image, Resources::ResourceNamespace resourceNamespace = Resources::ResourceNamespace::Global) = 0;
	virtual Geometry::Vec2I GetImageOffset(const FString& image, Resources::ResourceNamespace resourceNamespace = Resources::ResourceNamespace::Global) = 0;

	/// <summary>
	/// Gets the vertical height of the font, which is the tallest
	/// character that can be rendered from the bottom.
	/// </summary>
	/// <param name="font">The name of the font.</param>
	/// <returns>The font height in pixels.</returns>
	virtual int GetFontHeight(const FString& font) = 0;
};

}
