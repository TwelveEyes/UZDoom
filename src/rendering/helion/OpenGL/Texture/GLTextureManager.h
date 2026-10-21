/*
** GLTextureManager.h
**
** Port of Helion.Render.OpenGL.Texture.GLTextureManager<GLTextureType>.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** UZDoom mapping of the Helion types this class is built on:
**   Resources.Texture / texture index  FGameTexture / FTextureID::GetIndex(); the Texture.RenderStore,
**                                      RenderStoreClamp, BrightmapRenderStore, BrightmapRenderStoreClamp
**                                      and SetGLTexture/ClearGLTexture members are TextureRenderStore
**                                      slots of the manager, indexed by texture index.
**   Graphics.Image                     TextureImage (OpenGL/Texture/TextureImage.h)
**   SpriteRotation                     SpriteRotation below, per (sprite texture, mirror, translation)
**   ResourceTracker<T>                 TextureTracker below
**   ArchiveCollection / IConfig        TexMan / CVars
**   GLInfo.MaxArrayLayers              DFrameBuffer::GetMaxTextureArrayLayers()
** The generic parameter GLTextureType is GLLegacyTexture (the only instantiation; the font family
** GLFontTexture, GetFont and NullFont are not ported, the 2D drawer is not part of the Helion port).
** Helion's world animation (swapping the texture of a Resources.Texture) is TexMan's Translation
** table: GetTexture(int)/GetTexture(FTextureID)/GetBrightmapTexture resolve it at lookup time like
** Helion's TextureManager.GetTexture (m_translations), GetNonAnimatedTexture does not. Geometry
** therefore stores unresolved handles, so static geometry animates without being rebuilt.
**
** Texture classification (UsesArray): a texture is rendered by the array programs (boundTexture,
** sampler2DArray, boundTextureIndex) unless it needs the per material path: normal/specular/
** metallic/roughness/AO/detail/glow layers, a custom shader (GLDEFS), warp, canvas textures.
** Animated textures and textures with a brightmap are never packed into a shared array (like
** Helion), they are a one layer array of their own.
**
** Colour modes: truecolor layers are BGRA8; palette mode (vid_colormode 1) layers hold the palette
** index in red and alpha 0 / 254, see TextureImage.h. The mode is read once when the manager is
** created, changing vid_colormode requires a new manager. Palette mode samples with the nearest
** filter and no anisotropy (GLLegacyTexture::SamplerFilter), otherwise the backend applies
** gl_texture_filter and gl_texture_filter_anisotropic at bind time, so there are no
** filter/anisotropy change handlers.
**
*/

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "tarray.h"
#include "zstring.h"
#include "textureid.h"
#include "Common/Shared/IImageDrawInfoProvider.h"
#include "Common/Textures/IRendererTextureManager.h"
#include "OpenGL/Texture/ArrayTextureData.h"
#include "OpenGL/Texture/IArrayTextureLookup.h"
#include "OpenGL/Texture/TextureImage.h"
#include "OpenGL/Texture/Legacy/GLLegacyTexture.h"

class FGameTexture;

namespace Helion::Render::OpenGL::Texture
{

using Legacy::GLLegacyTexture;

struct ArrayTextureImage
{
	const TextureImage* Image = nullptr;	// valid until the next getImage call; always the size of the array
	Geometry::Dimension OriginalDimension;	// pixel size of the image before it was fit into the array dimension
	Geometry::Dimension LogicalDimension;	// size in map units of the image
	// Properties of the image before it was fit into the array dimension (Helion reads them from the fit image).
	Geometry::Vec2I Offset;
	int TransparentPixelCount = 0;
	int BlankRowsFromTop = 0;
	int BlankRowsFromBottom = 0;
};
using GetImageFunc = std::function<ArrayTextureImage(int)>;

// Helion.Resources.SpriteRotation: the textures of one sprite rotation.
struct SpriteRotation
{
	FGameTexture* Texture = nullptr;
	bool Mirror = false;
	bool BrightmapNoFullbright = false;
	GLLegacyTexture* RenderStore = nullptr;
	GLLegacyTexture* BrightmapRenderStore = nullptr;
};

// Helion.Util.Container.ResourceTracker<T>: textures by (name, namespace), names are case insensitive.
class TextureTracker
{
public:
	void Insert(const FString& name, Resources::ResourceNamespace ns, GLLegacyTexture* texture);
	GLLegacyTexture* GetOnly(const FString& name, Resources::ResourceNamespace ns) const;
	// The texture of the priority namespace if there is one, otherwise the one of any namespace.
	GLLegacyTexture* Get(const FString& name, Resources::ResourceNamespace priorityNamespace) const;
	void Remove(const FString& name, Resources::ResourceNamespace ns);
	// Removes every entry whose texture satisfies the predicate (UZDoom-only, used when texture arrays are destroyed).
	void RemoveIf(const std::function<bool(GLLegacyTexture*)>& predicate);
	std::vector<GLLegacyTexture*> GetValues() const;
	void Clear() { m_textures.clear(); }

private:
	using Entry = std::pair<Resources::ResourceNamespace, GLLegacyTexture*>;
	// Lower cased name -> the textures of that name, one per namespace.
	std::unordered_map<std::string, std::vector<Entry>> m_textures;
};

// Texture.RenderStore & co.
struct TextureRenderStore
{
	GLLegacyTexture* RenderStore = nullptr;
	GLLegacyTexture* RenderStoreClamp = nullptr;
	GLLegacyTexture* BrightmapRenderStore = nullptr;
	GLLegacyTexture* BrightmapRenderStoreClamp = nullptr;
};

class GLTextureManager : public Common::Textures::IRendererTextureManager, public IArrayTextureLookup
{
public:
	/// <summary>
	/// The null texture, intended to be used when the actual texture
	/// cannot be found.
	/// </summary>
	GLLegacyTexture* NullTexture = nullptr;
	GLLegacyTexture* TransparentNullTexture = nullptr;

	/// <summary>
	/// A fully white texture that can be used for drawing shapes of a
	/// solid color.
	/// </summary>
	GLLegacyTexture* WhiteTexture = nullptr;

	GLLegacyTexture* BlackTexture = nullptr;

	/// <summary>
	/// The null sprite rotation for when a sprite cannot be found.
	/// </summary>
	SpriteRotation NullSpriteRotation;

	virtual Shared::IImageDrawInfoProvider* ImageDrawInfoProvider() = 0;

	GLTextureManager(const GLTextureManager&) = delete;
	GLTextureManager& operator=(const GLTextureManager&) = delete;
	~GLTextureManager() override;

	bool PaletteColorMode() const { return m_paletteColorMode; }

	void DestroyTextureArrays(TextureContext textureContext);

	bool TryGet(const FString& name, Common::Textures::IRenderableTextureHandle*& handle, std::optional<Resources::ResourceNamespace> specificNamespace = std::nullopt,
		int upscalingFactor = 1, bool brightmap = false) override;

	void RegisterTexture(const FString& name, Common::Textures::IRenderableTextureHandle* handle, Resources::ResourceNamespace resourceNamespace) override;

	TArray<FString> GetNames(Resources::ResourceNamespace specificNamespace) override;

	/// <summary>
	/// Checks if the texture manager contains the image.
	/// </summary>
	/// <param name="name">The name of the image.</param>
	/// <returns>True if it does, false if not.</returns>
	bool Contains(const FString& name);

	bool TryGetImage(const FString& name, TextureImage& image, std::optional<Resources::ResourceNamespace> specificNamespace = std::nullopt,
		int upscalingFactor = 1, bool brightmap = false) override;

	bool TryGetImage(const FString& name, Resources::ResourceNamespace priorityNamespace, TextureImage& image, Resources::ResourceNamespace& fetchedNamespace,
		int upscalingFactor = 1, bool brightmap = false);

	/// <summary>
	/// Gets the texture, with priority given to the namespace provided. If
	/// it cannot be found, the null texture handle is used instead.
	/// </summary>
	/// <param name="name">The texture name.</param>
	/// <param name="priorityNamespace">The namespace to search first.
	/// </param>
	/// <param name="texture">The populated texture. This will either be
	/// the texture you want, or it will be the null image texture.</param>
	/// <returns>True if the texture was found, false if it was not found
	/// and the out value is the null texture handle.</returns>
	bool TryGet(const FString& name, Resources::ResourceNamespace priorityNamespace, GLLegacyTexture*& texture, int upscalingFactor = 1, bool brightmap = false);

	// Helion TextureManager.GetTexture: index is the texture manager index (FTextureID::GetIndex()) of the
	// unresolved texture; its current animation frame (TexMan Translation table) is returned.
	GLLegacyTexture* GetTexture(int index, bool repeatY = true);
	GLLegacyTexture* GetTexture(FTextureID id, bool repeatY = true);
	// Helion TextureManager.GetNonAnimatedTexture: index is used as is.
	GLLegacyTexture* GetNonAnimatedTexture(int index, bool repeatY = true);
	// The index of the current animation frame of the texture (Helion TextureManager.GetTranslationIndex).
	static int GetTranslationIndex(int index);

	bool CreateTextureArray(std::span<FGameTexture*> textures, TextureContext textureContext, TextureFlags textureFlags, Geometry::Dimension dimension, bool addToTextureTracker) override;

	int GetWorldArrayTextureHandle(int textureHandle, bool repeatY) override;

	bool TryGetTexture(int textureIndex, TextureContext textureContext, FGameTexture*& texture);

	// Brightmap of the current animation frame of the texture (like GetTexture(int)).
	GLLegacyTexture* GetBrightmapTexture(int index, bool repeatY = true);
	GLLegacyTexture* GetNonAnimatedBrightmapTexture(int index, bool repeatY = true);

	// UZDoom-only: Texture.RenderStore / RenderStoreClamp of the texture, nullptr if it was not created yet.
	GLLegacyTexture* GetRenderStore(int index, bool repeatY);

	/// <summary>
	/// Get a sprite rotation.
	/// </summary>
	/// <param name="spriteTexture">The texture of the sprite frame/rotation (spritedef_t::GetSpriteFrame).</param>
	/// <param name="mirror">Whether the rotation is flipped.</param>
	/// <param name="translation">FTranslationID::index() of the actor. In palette mode the translation is a
	/// colormap of the colormap buffer, the textures of the rotation are the untranslated ones. In truecolor
	/// mode the translated image is created on demand and cached per (texture, translation).</param>
	/// <returns>The rotation, owned by the manager. NullSpriteRotation if the texture is not valid.</returns>
	const SpriteRotation* GetSpriteRotation(FTextureID spriteTexture, bool mirror, int translation);

	/// <summary>
	/// Gets the texture, with priority given to the sprite namespace. If
	/// it cannot be found, the null texture handle is returned.
	/// </summary>
	/// <param name="name">The flat texture name.</param>
	/// <param name="texture">The populated texture. This will either be
	/// the texture you want, or it will be the null image texture.</param>
	/// <returns>True if the texture was found, false if it was not found
	/// and the out value is the null texture handle.</returns>
	bool TryGetSprite(const FString& name, GLLegacyTexture*& texture);

	Common::Textures::IRenderableTextureHandle* CreateOrReplaceTexture(const FString& name, Resources::ResourceNamespace resourceNamespace, const TextureImage& image, bool repeatY = true) override;

	void RemoveTexture(const FString& name, Resources::ResourceNamespace resourceNamespace) override;

	void Dispose();

	static int CalculateMipmapLevels(Geometry::Dimension dimension);

	GLLegacyTexture* CreateBrightMapTexture(const TextureImage* image, const FString* name, Resources::ResourceNamespace resourceNamespace, bool repeatY = true);

	GLLegacyTexture* CreateTexture(const TextureImage* image, bool repeatY);

	GLLegacyTexture* CreateTexture(const TextureImage* image, const FString* name, Resources::ResourceNamespace resourceNamespace, bool repeatY = true);

	// UZDoom-only: the largest array a texture array may have (Helion GLInfo.MaxArrayLayers).
	static int MaxArrayLayers();

	// UZDoom-only. True if the game texture is animated by TexAnim (animation frames and the base picture).
	bool IsTextureAnimated(int index);
	// Call after the animation definitions changed (a new level/game).
	void ResetAnimatedTextures() { m_animatedTexturesValid = false; }

	// UZDoom-only. See the file comment: false = per material path (FRenderState::SetMaterial).
	static bool UsesArray(FGameTexture* texture);

protected:
	GLTextureManager();

	// Creates the null/white/black textures. Helion does this in the constructor through the abstract
	// GenerateTexture, which cannot be called from a base class constructor in C++: the derived class
	// constructor calls it.
	void CreateDefaultTextures();

	TextureTracker TextureTracker_;
	TextureTracker TextureTrackerClamp_;

	// Everything that was created, the manager owns the textures until it is disposed.
	std::vector<std::unique_ptr<GLLegacyTexture>> OwnedTextures;

	// Helion Texture.SetGLTexture / ClearGLTexture
	TextureRenderStore& RenderStoreFor(int index);
	void ClearGLTexture(int index);

	int NextTextureId() { return ++m_nextTextureId; }

	// Takes ownership of a texture (Helion relies on the garbage collector).
	GLLegacyTexture* Own(std::unique_ptr<GLLegacyTexture> texture);

	void DeleteTexture(GLLegacyTexture* texture, const FString& name, Resources::ResourceNamespace resourceNamespace);

	// Creates a texture owned by OwnedTextures. logicalDimension is the size in map units (0 = the image size).
	virtual GLLegacyTexture* GenerateTexture(const TextureImage& image, const FString& name, Resources::ResourceNamespace resourceNamespace, TextureFlags flags = TextureFlags::Default) = 0;
	virtual std::vector<GLLegacyTexture*> GenerateTextureArray(const GetImageFunc& getImage, int imageLength, Geometry::Dimension dimension, Resources::ResourceNamespace resourceNamespace,
		TextureFlags flags, TextureContext textureContext, GLLegacyTexture*& arrayTexture) = 0;

public:
	virtual void ReUpload(GLLegacyTexture* texture, const TextureImage& image) = 0;
	// UZDoom-only: the namespace a game texture lives in, by use type.
	static Resources::ResourceNamespace NamespaceOf(FGameTexture* texture);
	// UZDoom-only: a texture of its own (never a shared array layer, never the texture walls use) for the texture index,
	// like Helion's SkySphereTexture.CreateTexture. clamp = ClampX|ClampY (sky box faces). Owned by the manager.
	GLLegacyTexture* GetStandaloneTexture(int index, bool clamp = false);

protected:
	// Creates the one image of a game texture layer, see TextureImage::FromGameTexture.
	TextureImage LoadImage(FGameTexture* texture, int translation, bool brightmap, bool forceUpscale = false);
	// The texture of a game texture with the namespace, flags and sizes of the game texture. Not tracked by name.
	GLLegacyTexture* CreateGameTexture(FGameTexture* texture, bool repeatY, int translation, bool forceUpscale = false);

private:
	bool m_paletteColorMode;
	bool m_disposed = false;
	int m_nextTextureId = 0;
	std::unordered_map<int, GLLegacyTexture*> m_standaloneTextures;
	TArray<TextureRenderStore> m_renderStores;
	std::vector<std::unique_ptr<ArrayTextureData>> m_arrayTextures;
	std::unordered_map<uint64_t, std::unique_ptr<SpriteRotation>> m_spriteRotations;
	std::unordered_map<uint64_t, GLLegacyTexture*> m_translatedTextures;
	std::vector<bool> m_animatedTextures;
	bool m_animatedTexturesValid = false;
	FGameTexture* FindGameTexture(const FString& name, Resources::ResourceNamespace priorityNamespace, Resources::ResourceNamespace& fetchedNamespace);

	TextureTracker& GetTextureTracker(bool repeatY) { return repeatY ? TextureTracker_ : TextureTrackerClamp_; }
	void ReleaseUnmanagedResources();
};

}
