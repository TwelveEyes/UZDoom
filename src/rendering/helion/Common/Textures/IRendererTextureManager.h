/*
** IRendererTextureManager.h
**
** Port of Helion.Render.Common.Textures.IRendererTextureManager.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion.Graphics.Image is OpenGL/Texture/TextureImage.h, Resources.Texture is FGameTexture*.
** BrightmapDefinition does not exist (brightmaps are layers of FGameTexture): the brightmap argument
** is a bool selecting the brightmap layer of the named texture. upscalingFactor > 1 means "apply the
** hqresize upscaler of the backend (gl_texture_hqresize*)", the factor itself is a CVar there.
**
*/

#pragma once

#include <optional>
#include <span>
#include "tarray.h"
#include "zstring.h"
#include "Util/Geometry.h"
#include "Resources/ResourceNamespace.h"
#include "OpenGL/Texture/GLTexture.h"
#include "OpenGL/Texture/TextureImage.h"
#include "Common/Textures/IRenderableTextureHandle.h"

class FGameTexture;

namespace Helion::Render::Common::Textures
{

/// <summary>
/// A texture manager for the renderer.
/// </summary>
class IRendererTextureManager
{
public:
	virtual ~IRendererTextureManager() = default;

	/// <summary>
	/// Checks if the image exists and is renderable with.
	/// </summary>
	/// <param name="name">The case insensitive name of the image.</param>
	/// <param name="specificNamespace">If null, will search all namespaces,
	/// otherwise will search only in the provided one.</param>
	/// <returns>True if such an image exists, false otherwise.</returns>
	bool HasImage(const FString& name, std::optional<Resources::ResourceNamespace> specificNamespace = std::nullopt)
	{
		IRenderableTextureHandle* handle;
		return TryGet(name, handle, specificNamespace);
	}

	virtual bool TryGetImage(const FString& name, OpenGL::Texture::TextureImage& image, std::optional<Resources::ResourceNamespace> specificNamespace = std::nullopt,
		int upscalingFactor = 1, bool brightmap = false) = 0;

	/// <summary>
	/// Tries to get a handle for an image.
	/// </summary>
	/// <param name="name">The image name.</param>
	/// <param name="handle">The handle, which is not null if it returns true,
	/// or null if it returns false.</param>
	/// <param name="specificNamespace">If null, will search all namespaces,
	/// otherwise will search only in the provided one.</param>
	/// <param name="upscalingFactor">Amount to upscale the texture if retrieving it
	/// for the first time.  If 1, no upscaling is performed.</param>
	/// <returns>True if found, false if not.</returns>
	virtual bool TryGet(const FString& name, IRenderableTextureHandle*& handle, std::optional<Resources::ResourceNamespace> specificNamespace = std::nullopt,
		int upscalingFactor = 1, bool brightmap = false) = 0;

	/// <summary>
	/// Get a list of texture names
	/// </summary>
	/// <param name="specificNamespace">Namespace to filter to</param>
	/// <returns>A list of texture names</returns>
	virtual TArray<FString> GetNames(Resources::ResourceNamespace specificNamespace) = 0;

	/// <summary>
	/// Stores a new image in the texture manager.  It is the consumer's responsibility to remove and dispose this texture when it is no longer needed.
	/// </summary>
	/// <param name="name">Name to assign the texture</param>
	/// <param name="resourceNamespace">Resource namespace to store the texture in</param>
	/// <param name="image">Image data</param>
	/// <param name="repeatY">Whether the image should repeat vertically</param>
	/// <returns>A handle to the created texture</returns>
	virtual IRenderableTextureHandle* CreateOrReplaceTexture(const FString& name, Resources::ResourceNamespace resourceNamespace, const OpenGL::Texture::TextureImage& image, bool repeatY = true) = 0;

	/// <summary>
	/// Remove a texture from the texture manager
	/// </summary>
	/// <param name="name">Name of the texture</param>
	/// <param name="resourceNamespace">Namespace for the texture</param>
	virtual void RemoveTexture(const FString& name, Resources::ResourceNamespace resourceNamespace) = 0;

	virtual void RegisterTexture(const FString& name, IRenderableTextureHandle* handle, Resources::ResourceNamespace resourceNamespace) = 0;

	virtual bool CreateTextureArray(std::span<FGameTexture*> textures, OpenGL::Texture::TextureContext textureContext, OpenGL::Texture::TextureFlags textureFlags,
		Geometry::Dimension dimension, bool addToTextureTracker) = 0;
};

}
