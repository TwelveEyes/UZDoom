/*
** GLTextureManager.cpp
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
** Departures from Helion that are not a type mapping:
**  - TryGet(name) finds the FGameTexture with TexMan.CheckForTexture (the namespace picks the use type
**    first, then any use type) and builds the GL texture with the game texture's sizes and offsets.
**  - CreateGameTexture/GetTexture(index) do not use the name tracker: texture names are not unique in
**    UZDoom (a patch and a wall texture can share a name), game textures are cached by texture index.
**  - Texture.RenderStore is TextureRenderStore[index]; animated textures are the TexMan translation of the
**    texture index and are resolved by GetTexture(int)/GetTexture(FTextureID) (Helion m_translations).
**
*/

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include "c_cvars.h"
#include "printf.h"
#include "textures.h"
#include "gametexture.h"
#include "texturemanager.h"
#include "HelionCVars.h"
#include "animations.h"
#include "v_video.h"
#include "GLTextureManager.h"

namespace Helion::Render::OpenGL::Texture
{

using namespace Geometry;
using Resources::ResourceNamespace;

// Helion.Util.Constants.NoTexture
static const char* NoTexture = "-";

static FString LowerName(const FString& name)
{
	FString lower = name;
	lower.ToLower();
	return lower;
}

//==========================================================================
//
// TextureTracker (Helion.Util.Container.ResourceTracker<T>)
//
//==========================================================================

void TextureTracker::Insert(const FString& name, ResourceNamespace ns, GLLegacyTexture* texture)
{
	auto& entries = m_textures[LowerName(name).GetChars()];
	for (auto& entry : entries)
	{
		if (entry.first == ns)
		{
			entry.second = texture;
			return;
		}
	}
	entries.push_back(Entry(ns, texture));
}

GLLegacyTexture* TextureTracker::GetOnly(const FString& name, ResourceNamespace ns) const
{
	auto it = m_textures.find(LowerName(name).GetChars());
	if (it == m_textures.end())
		return nullptr;

	for (auto& entry : it->second)
	{
		if (entry.first == ns)
			return entry.second;
	}
	return nullptr;
}

GLLegacyTexture* TextureTracker::Get(const FString& name, ResourceNamespace priorityNamespace) const
{
	GLLegacyTexture* texture = GetOnly(name, priorityNamespace);
	if (texture != nullptr)
		return texture;

	auto it = m_textures.find(LowerName(name).GetChars());
	if (it == m_textures.end() || it->second.empty())
		return nullptr;

	return it->second.front().second;
}

void TextureTracker::Remove(const FString& name, ResourceNamespace ns)
{
	auto it = m_textures.find(LowerName(name).GetChars());
	if (it == m_textures.end())
		return;

	auto& entries = it->second;
	entries.erase(std::remove_if(entries.begin(), entries.end(), [ns](const Entry& entry) { return entry.first == ns; }), entries.end());
	if (entries.empty())
		m_textures.erase(it);
}

void TextureTracker::RemoveIf(const std::function<bool(GLLegacyTexture*)>& predicate)
{
	for (auto it = m_textures.begin(); it != m_textures.end();)
	{
		auto& entries = it->second;
		entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& entry) { return predicate(entry.second); }), entries.end());
		if (entries.empty())
			it = m_textures.erase(it);
		else
			++it;
	}
}

std::vector<GLLegacyTexture*> TextureTracker::GetValues() const
{
	std::vector<GLLegacyTexture*> values;
	for (auto& pair : m_textures)
	{
		for (auto& entry : pair.second)
			values.push_back(entry.second);
	}
	return values;
}

//==========================================================================
//
// GLTextureManager
//
//==========================================================================

GLTextureManager::GLTextureManager()
	: m_paletteColorMode(vid_colormode == 1)
{
}

GLTextureManager::~GLTextureManager()
{
	Dispose();
}

void GLTextureManager::CreateDefaultTextures()
{
	// Config.Render.NullTexture (a debug option) is false: missing textures are transparent.
	const bool nullTextureConfig = false;
	NullTexture = nullTextureConfig ? GenerateTexture(TextureImage::NullImage(), "NULL", ResourceNamespace::Global) : GenerateTexture(TextureImage::TransparentImage(), "NULL", ResourceNamespace::Global);
	TransparentNullTexture = nullTextureConfig ? GenerateTexture(TextureImage::TransparentImage(), "NULL", ResourceNamespace::Global) : NullTexture;
	WhiteTexture = GenerateTexture(TextureImage::WhiteImage(), "NULLWHITE", ResourceNamespace::Global);
	BlackTexture = GenerateTexture(TextureImage::CreateBlackImage(m_paletteColorMode), "NULLBLACK", ResourceNamespace::Global);

	// CreateNullSpriteRotation
	NullSpriteRotation = SpriteRotation();
	NullSpriteRotation.RenderStore = NullTexture;
	NullSpriteRotation.BrightmapRenderStore = TransparentNullTexture;
}

GLLegacyTexture* GLTextureManager::Own(std::unique_ptr<GLLegacyTexture> texture)
{
	GLLegacyTexture* raw = texture.get();
	OwnedTextures.push_back(std::move(texture));
	return raw;
}

TextureRenderStore& GLTextureManager::RenderStoreFor(int index)
{
	if ((unsigned)index >= m_renderStores.Size())
		m_renderStores.Resize((unsigned)index + 1);
	return m_renderStores[index];
}

void GLTextureManager::ClearGLTexture(int index)
{
	if ((unsigned)index >= m_renderStores.Size())
		return;

	m_renderStores[index].RenderStore = nullptr;
	m_renderStores[index].RenderStoreClamp = nullptr;
}

GLLegacyTexture* GLTextureManager::GetRenderStore(int index, bool repeatY)
{
	if ((unsigned)index >= m_renderStores.Size())
		return nullptr;

	return repeatY ? m_renderStores[index].RenderStore : m_renderStores[index].RenderStoreClamp;
}

ResourceNamespace GLTextureManager::NamespaceOf(FGameTexture* texture)
{
	switch (texture->GetUseType())
	{
	case ETextureType::Flat:
		return ResourceNamespace::Flats;
	case ETextureType::Sprite:
	case ETextureType::SkinSprite:
		return ResourceNamespace::Sprites;
	case ETextureType::Wall:
	case ETextureType::WallPatch:
	case ETextureType::Override:
		return ResourceNamespace::Textures;
	case ETextureType::MiscPatch:
	case ETextureType::Decal:
	case ETextureType::SkinGraphic:
	case ETextureType::Autopage:
		return ResourceNamespace::Graphics;
	case ETextureType::FontChar:
		return ResourceNamespace::Fonts;
	default:
		return ResourceNamespace::Global;
	}
}

void GLTextureManager::DestroyTextureArrays(TextureContext textureContext)
{
	if ((unsigned)textureContext >= m_arrayTextures.size() || !m_arrayTextures[(int)textureContext])
		return;

	m_arrayTextures[(int)textureContext]->Destroy([this](FGameTexture* texture) { ClearGLTexture(texture->GetID().GetIndex()); });
	m_arrayTextures[(int)textureContext].reset();

	// The parent array textures and their sub textures are dead now, drop every reference to them.
	auto isArrayOfContext = [textureContext](GLLegacyTexture* texture)
	{
		return (texture->IsParentArray && texture->Context == textureContext) ||
			(texture->ParentArrayTexture != nullptr && texture->ParentArrayTexture->Context == textureContext);
	};

	TextureTracker_.RemoveIf(isArrayOfContext);
	TextureTrackerClamp_.RemoveIf(isArrayOfContext);

	if (textureContext == TextureContext::WorldSprites)
		m_spriteRotations.clear();

	// Decide first and destroy afterwards: a sub texture reads the parent it points to.
	std::vector<std::unique_ptr<GLLegacyTexture>> removed;
	std::vector<std::unique_ptr<GLLegacyTexture>> kept;
	for (auto& texture : OwnedTextures)
	{
		if (isArrayOfContext(texture.get()))
			removed.push_back(std::move(texture));
		else
			kept.push_back(std::move(texture));
	}
	OwnedTextures = std::move(kept);

	for (unsigned i = 0; i < m_renderStores.Size(); i++)
	{
		TextureRenderStore& store = m_renderStores[i];
		for (GLLegacyTexture** slot : { &store.RenderStore, &store.RenderStoreClamp, &store.BrightmapRenderStore, &store.BrightmapRenderStoreClamp })
		{
			if (*slot != nullptr && isArrayOfContext(*slot))
				*slot = nullptr;
		}
	}
}

bool GLTextureManager::TryGet(const FString& name, Common::Textures::IRenderableTextureHandle*& handle, std::optional<ResourceNamespace> specificNamespace, int upscalingFactor, bool brightmap)
{
	GLLegacyTexture* texture;
	if (TryGet(name, specificNamespace.value_or(ResourceNamespace::Undefined), texture, upscalingFactor, brightmap))
	{
		handle = texture;
		return true;
	}

	handle = nullptr;
	return false;
}

void GLTextureManager::RegisterTexture(const FString& name, Common::Textures::IRenderableTextureHandle* handle, ResourceNamespace resourceNamespace)
{
	if (auto textureType = dynamic_cast<GLLegacyTexture*>(handle))
		TextureTracker_.Insert(name, resourceNamespace, textureType);
}

TArray<FString> GLTextureManager::GetNames(ResourceNamespace specificNamespace)
{
	TArray<FString> names;
	for (int i = 0; i < TexMan.NumTextures(); i++)
	{
		FGameTexture* texture = TexMan.GameByIndex(i);
		if (texture != nullptr && texture->isValid() && NamespaceOf(texture) == specificNamespace)
			names.Push(texture->GetName());
	}
	return names;
}

bool GLTextureManager::Contains(const FString& name)
{
	GLLegacyTexture* texture;
	return TryGet(name, ResourceNamespace::Global, texture);
}

FGameTexture* GLTextureManager::FindGameTexture(const FString& name, ResourceNamespace priorityNamespace, ResourceNamespace& fetchedNamespace)
{
	fetchedNamespace = priorityNamespace;
	FTextureID id;

	// The reason we do this check before checking other namespaces is
	// that we can end up missing the texture for the namespace in some
	// pathological scenarios. Suppose we draw some texture that shares
	// a name with some flat. Then suppose we try to draw the flat. If
	// we check the GL texture cache first, we will find the texture
	// and miss the flat and then never know that there is a specific
	// flat that should have been used.
	ETextureType type = ETextureType::Any;
	switch (priorityNamespace)
	{
	case ResourceNamespace::Flats: type = ETextureType::Flat; break;
	case ResourceNamespace::Sprites: type = ETextureType::Sprite; break;
	case ResourceNamespace::Textures: type = ETextureType::Wall; break;
	case ResourceNamespace::Graphics: type = ETextureType::MiscPatch; break;
	default: break;
	}

	if (type != ETextureType::Any)
		id = TexMan.CheckForTexture(name.GetChars(), type, 0);

	if (!id.isValid())
	{
		// Note that because we are getting any texture, we don't want to
		// use the provided namespace since if we ask for a flat, but get a
		// texture, and then index it as a flat... things probably go bad.
		id = TexMan.CheckForTexture(name.GetChars(), ETextureType::Any, FTextureManager::TEXMAN_TryAny);
	}

	FGameTexture* texture = id.isValid() ? TexMan.GetGameTexture(id) : nullptr;
	if (texture == nullptr || !texture->isValid())
		return nullptr;

	if (priorityNamespace == ResourceNamespace::Undefined || NamespaceOf(texture) != priorityNamespace)
		fetchedNamespace = NamespaceOf(texture);
	return texture;
}

TextureImage GLTextureManager::LoadImage(FGameTexture* texture, int translation, bool brightmap, bool forceUpscale)
{
	bool upscale = false;
	if (!m_paletteColorMode)
	{
		bool sprite = texture->GetUseType() == ETextureType::Sprite || texture->GetUseType() == ETextureType::SkinSprite;
		upscale = forceUpscale || (!brightmap && shouldUpscale(texture, sprite ? UF_Sprite : UF_Texture) != 0);
	}
	return TextureImage::FromGameTexture(texture, translation, brightmap, m_paletteColorMode, upscale);
}

bool GLTextureManager::TryGetImage(const FString& name, TextureImage& image, std::optional<ResourceNamespace> specificNamespace, int upscalingFactor, bool brightmap)
{
	ResourceNamespace fetchedNamespace;
	return TryGetImage(name, specificNamespace.value_or(ResourceNamespace::Undefined), image, fetchedNamespace, upscalingFactor, brightmap);
}

bool GLTextureManager::TryGetImage(const FString& name, ResourceNamespace priorityNamespace, TextureImage& image, ResourceNamespace& fetchedNamespace, int upscalingFactor, bool brightmap)
{
	FGameTexture* texture = FindGameTexture(name, priorityNamespace, fetchedNamespace);
	if (texture == nullptr)
		return false;

	image = LoadImage(texture, 0, brightmap, upscalingFactor > 1);
	return !image.Empty();
}

bool GLTextureManager::TryGet(const FString& name, ResourceNamespace priorityNamespace, GLLegacyTexture*& texture, int upscalingFactor, bool brightmap)
{
	texture = NullTexture;
	if (name.Compare(NoTexture) == 0)
		return false;

	GLLegacyTexture* textureForNamespace = TextureTracker_.GetOnly(name, priorityNamespace);
	if (textureForNamespace != nullptr)
	{
		texture = textureForNamespace;
		return true;
	}

	ResourceNamespace fetchedNamespace;
	FGameTexture* gameTexture = FindGameTexture(name, priorityNamespace, fetchedNamespace);
	if (gameTexture != nullptr)
	{
		bool repeatY = true;
		if (fetchedNamespace == ResourceNamespace::Sprites || fetchedNamespace == ResourceNamespace::Brightmaps)
			repeatY = false;

		TextureImage image = LoadImage(gameTexture, 0, brightmap, upscalingFactor > 1);
		if (!image.Empty())
		{
			texture = brightmap ? CreateBrightMapTexture(&image, &name, fetchedNamespace, repeatY) : CreateTexture(&image, &name, fetchedNamespace, repeatY);
			if (texture != nullptr && texture->Source == nullptr)
				texture->Source = gameTexture;
			return true;
		}
	}

	// Now that nothing in the desired namespace was found, we will
	// accept anything.
	GLLegacyTexture* anyTexture = TextureTracker_.Get(name, priorityNamespace);
	if (anyTexture != nullptr)
	{
		texture = anyTexture;
		return true;
	}

	return false;
}

GLLegacyTexture* GLTextureManager::CreateGameTexture(FGameTexture* texture, bool repeatY, int translation, bool forceUpscale)
{
	ResourceNamespace ns = NamespaceOf(texture);
	repeatY = ns != ResourceNamespace::Sprites && ns != ResourceNamespace::Brightmaps && repeatY;

	TextureImage image = LoadImage(texture, translation, false, forceUpscale);
	if (image.Empty())
		return NullTexture;

	TextureFlags flags = repeatY ? TextureFlags::Default : TextureFlags::ClampY;
	FString name;
	name.Format("%d:%s", texture->GetID().GetIndex(), texture->GetName().GetChars());
	GLLegacyTexture* glTexture = GenerateTexture(image, name, ns, flags);
	glTexture->Source = texture;
	return glTexture;
}

int GLTextureManager::GetTranslationIndex(int index)
{
	if (index < 0 || index >= TexMan.NumTextures())
		return index;

	FGameTexture* texture = TexMan.GetGameTexture(FSetTextureID(index), true);
	return texture != nullptr ? texture->GetID().GetIndex() : index;
}

GLLegacyTexture* GLTextureManager::GetTexture(int index, bool repeatY)
{
	return GetNonAnimatedTexture(GetTranslationIndex(index), repeatY);
}

GLLegacyTexture* GLTextureManager::GetNonAnimatedTexture(int index, bool repeatY)
{
	if (index < 0)
		return NullTexture;

	FGameTexture* texture = TexMan.GameByIndex(index);
	if (texture == nullptr || !texture->isValid())
		return NullTexture;

	TextureRenderStore& store = RenderStoreFor(index);
	GLLegacyTexture* renderTexture = repeatY ? store.RenderStore : store.RenderStoreClamp;

	if (renderTexture == nullptr)
	{
		renderTexture = CreateGameTexture(texture, repeatY, 0);
		// Texture.SetGLTexture
		TextureRenderStore& updated = RenderStoreFor(index);
		if (repeatY)
			updated.RenderStore = renderTexture;
		else
			updated.RenderStoreClamp = renderTexture;
	}

	return renderTexture;
}

GLLegacyTexture* GLTextureManager::GetTexture(FTextureID id, bool repeatY)
{
	return GetTexture(id.GetIndex(), repeatY);
}

GLLegacyTexture* GLTextureManager::GetStandaloneTexture(int index, bool clamp)
{
	int key = clamp ? ~index : index;
	auto it = m_standaloneTextures.find(key);
	if (it != m_standaloneTextures.end())
		return it->second;

	FGameTexture* gameTexture = index >= 0 ? TexMan.GameByIndex(index) : nullptr;
	GLLegacyTexture* texture = NullTexture;
	if (gameTexture != nullptr && gameTexture->isValid())
	{
		texture = CreateGameTexture(gameTexture, true, 0);
		if (clamp && texture != NullTexture)
			texture->Flags = TextureFlags::ClampX | TextureFlags::ClampY;
	}
	m_standaloneTextures[key] = texture;
	return texture;
}

bool GLTextureManager::IsTextureAnimated(int index)
{
	if (!m_animatedTexturesValid)
	{
		m_animatedTextures.assign((size_t)TexMan.NumTextures(), false);
		for (const FAnimDef& anim : TexAnim.GetAnimations())
		{
			int base = anim.BasePic.GetIndex();
			if ((unsigned)base < m_animatedTextures.size())
				m_animatedTextures[base] = true;

			// Range animations (ANIMATED, ANIMDEFS "range") store one Frames entry (the speed); their frames are BasePic + 0..NumFrames-1.
			for (int i = 0; i < anim.NumFrames; i++)
			{
				int frame = anim.bDiscrete ? anim.Frames[i].FramePic.GetIndex() : base + i;
				if ((unsigned)frame < m_animatedTextures.size())
					m_animatedTextures[frame] = true;
			}
		}
		m_animatedTexturesValid = true;
	}

	return (unsigned)index < m_animatedTextures.size() && m_animatedTextures[index];
}

bool GLTextureManager::UsesArray(FGameTexture* texture)
{
	if (texture == nullptr || !texture->isValid())
		return false;

	// Canvas textures are rendered by the game and not backed by an image.
	if (texture->isHardwareCanvas() || texture->isSoftwareCanvas())
		return false;

	// Default and the warp shaders (a boundTexture program variant) only. The others are
	// layer shaders (specular, PBR), fuzz/paletted/no texture materials or GLDEFS custom shaders.
	if (texture->GetShaderIndex() > SHADER_Warp2)
		return false;

	if (texture->GetNormalmap() || texture->GetSpecularmap() || texture->GetMetallic() || texture->GetRoughness() ||
		texture->GetAmbientOcclusion() || texture->GetDetailmap() || texture->GetGlowmap())
		return false;

	return true;
}

int GLTextureManager::MaxArrayLayers()
{
	return screen->GetMaxTextureArrayLayers();
}

bool GLTextureManager::CreateTextureArray(std::span<FGameTexture*> textures, TextureContext textureContext, TextureFlags textureFlags, Geometry::Dimension dimension, bool addToTextureTracker)
{
	if (textures.empty())
		return false;

	assert((int)textures.size() <= MaxArrayLayers() && "Texture array overflow");
	if ((int)textures.size() > MaxArrayLayers())
		return false;

	// Helion loads every image up front. The images are loaded one after the other when the array is
	// uploaded to keep a single image in memory: GenerateTextureArray requests each image exactly once.
	TextureImage loaded;
	TextureImage fitImage;

	GLLegacyTexture* arrayTexture = nullptr;
	std::vector<GLLegacyTexture*> arraySubTextures = GenerateTextureArray([&](int imageIndex)
	{
		FGameTexture* texture = textures[imageIndex];
		loaded = LoadImage(texture, 0, false);
		assert(!loaded.Empty() && "Texture must have an image");
		if (loaded.Empty())
			loaded = TextureImage::TransparentImage();

		ArrayTextureImage result;
		result.OriginalDimension = loaded.Dimension;
		result.LogicalDimension = loaded.GetLogicalDimension();
		result.Offset = loaded.Offset;
		result.TransparentPixelCount = loaded.TransparentPixelCount();
		result.BlankRowsFromTop = loaded.BlankRowsFromTop;
		result.BlankRowsFromBottom = loaded.BlankRowsFromBottom;

		if (loaded.Dimension.Width == dimension.Width && loaded.Dimension.Height == dimension.Height)
		{
			result.Image = &loaded;
			return result;
		}

		fitImage = TextureImage(dimension, loaded.Offset, loaded.Namespace);
		fitImage.CopyPixelsFrom(loaded);
		result.Image = &fitImage;
		return result;
	}, (int)textures.size(), dimension, ResourceNamespace::Textures, textureFlags, textureContext, arrayTexture);

	for (size_t i = 0; i < textures.size(); i++)
	{
		GLLegacyTexture* glTexture = arraySubTextures[i];
		FGameTexture* texture = textures[i];
		glTexture->Source = texture;
		glTexture->Name.Format("%s %d:%s", glTexture->Name.GetChars(), texture->GetID().GetIndex(), texture->GetName().GetChars());

		// Texture.SetGLTexture
		TextureRenderStore& store = RenderStoreFor(texture->GetID().GetIndex());
		if ((textureFlags & TextureFlags::ClampY) == TextureFlags::Default)
			store.RenderStore = glTexture;
		else
			store.RenderStoreClamp = glTexture;

		// TODO make less stupid
		if (addToTextureTracker)
			TextureTracker_.Insert(texture->GetName(), ResourceNamespace::Undefined, glTexture);
	}

	if ((unsigned)textureContext >= m_arrayTextures.size())
		m_arrayTextures.resize((size_t)textureContext + 1);

	if (!m_arrayTextures[(int)textureContext])
		m_arrayTextures[(int)textureContext] = std::make_unique<ArrayTextureData>(textureContext);

	m_arrayTextures[(int)textureContext]->Add(arrayTexture, arraySubTextures, textures, textureFlags);
	return true;
}

int GLTextureManager::GetWorldArrayTextureHandle(int textureHandle, bool repeatY)
{
	if ((unsigned)TextureContext::WorldArray < m_arrayTextures.size() && m_arrayTextures[(int)TextureContext::WorldArray])
		return m_arrayTextures[(int)TextureContext::WorldArray]->GetArrayTextureHandle(textureHandle, repeatY ? TextureFlags::Default : TextureFlags::ClampY);

	return textureHandle;
}

bool GLTextureManager::TryGetTexture(int textureIndex, TextureContext textureContext, FGameTexture*& texture)
{
	if ((unsigned)textureContext >= m_arrayTextures.size() || !m_arrayTextures[(int)textureContext])
	{
		texture = nullptr;
		return false;
	}

	return m_arrayTextures[(int)textureContext]->TryGetTexture(textureIndex, texture);
}

GLLegacyTexture* GLTextureManager::GetBrightmapTexture(int index, bool repeatY)
{
	return GetNonAnimatedBrightmapTexture(GetTranslationIndex(index), repeatY);
}

GLLegacyTexture* GLTextureManager::GetNonAnimatedBrightmapTexture(int index, bool repeatY)
{
	FGameTexture* texture = index >= 0 ? TexMan.GameByIndex(index) : nullptr;
	if (texture == nullptr || !texture->isValid())
		return nullptr;

	TextureRenderStore& store = RenderStoreFor(index);
	GLLegacyTexture* renderTexture = repeatY ? store.BrightmapRenderStore : store.BrightmapRenderStoreClamp;

	if (renderTexture == nullptr && texture->GetBrightmap() != nullptr)
	{
		TextureImage image = LoadImage(texture, 0, true);
		if (image.Empty())
			return nullptr;

		FString name;
		name.Format("[BRIGHTMAP] %d:%s", index, texture->GetName().GetChars());
		// Deviation: Helion creates every brightmap in the Brightmaps namespace, which clamps X and Y (GetFlagsForNamespace),
		// so a brightmap of a wall or flat that repeats shows its edge pixels outside the first repetition. The brightmap of
		// a world texture wraps like the texture (repeatY picks Default or ClampY); sprite brightmaps stay clamped.
		bool sprite = texture->GetUseType() == ETextureType::Sprite || texture->GetUseType() == ETextureType::SkinSprite;
		renderTexture = CreateBrightMapTexture(&image, &name, sprite ? ResourceNamespace::Brightmaps : ResourceNamespace::Global, repeatY);
		// Texture.SetBrightmapGLTexture
		TextureRenderStore& updated = RenderStoreFor(index);
		if (repeatY)
			updated.BrightmapRenderStore = renderTexture;
		else
			updated.BrightmapRenderStoreClamp = renderTexture;
	}

	return renderTexture;
}

const SpriteRotation* GLTextureManager::GetSpriteRotation(FTextureID spriteTexture, bool mirror, int translation)
{
	FGameTexture* texture = spriteTexture.isValid() ? TexMan.GetGameTexture(spriteTexture) : nullptr;
	if (texture == nullptr || !texture->isValid())
		return &NullSpriteRotation;

	// In palette mode the translation is a colormap of the colormap buffer: the rotation always uses the
	// untranslated textures. For true color the image is recreated through the translation, like Helion
	// recreates it with a different palette (!StoreImageIndices && colorMapIndex > 0).
	if (m_paletteColorMode || translation < 0)
		translation = 0;

	int index = texture->GetID().GetIndex();
	uint64_t key = ((uint64_t)(uint32_t)index << 33) | ((uint64_t)(mirror ? 1 : 0) << 32) | (uint32_t)translation;
	auto it = m_spriteRotations.find(key);
	if (it != m_spriteRotations.end())
		return it->second.get();

	auto rotation = std::make_unique<SpriteRotation>();
	rotation->Texture = texture;
	rotation->Mirror = mirror;
	rotation->BrightmapNoFullbright = texture->isFullbrightDisabled();

	if (translation != 0)
	{
		uint64_t translatedKey = ((uint64_t)(uint32_t)index << 32) | (uint32_t)translation;
		auto translated = m_translatedTextures.find(translatedKey);
		if (translated == m_translatedTextures.end())
			translated = m_translatedTextures.emplace(translatedKey, CreateGameTexture(texture, false, translation)).first;
		rotation->RenderStore = translated->second;
	}
	else
	{
		// The sprite arrays (BuildSprites) registered the clamped store; textures that are not in an array get their own texture.
		GLLegacyTexture* store = GetRenderStore(index, false);
		if (store == nullptr)
			store = GetRenderStore(index, true);
		rotation->RenderStore = store != nullptr ? store : GetNonAnimatedTexture(index, false);
	}

	GLLegacyTexture* brightmap = GetNonAnimatedBrightmapTexture(index, false);
	rotation->BrightmapRenderStore = brightmap != nullptr ? brightmap : TransparentNullTexture;

	SpriteRotation* result = rotation.get();
	m_spriteRotations.emplace(key, std::move(rotation));
	return result;
}

bool GLTextureManager::TryGetSprite(const FString& name, GLLegacyTexture*& texture)
{
	return TryGet(name, ResourceNamespace::Sprites, texture);
}

Common::Textures::IRenderableTextureHandle* GLTextureManager::CreateOrReplaceTexture(const FString& name, ResourceNamespace resourceNamespace, const TextureImage& image, bool repeatY)
{
	GLLegacyTexture* existingTexture = TextureTracker_.GetOnly(name, resourceNamespace);
	if (existingTexture == nullptr)
	{
		GLLegacyTexture* newTexture = CreateTexture(&image, repeatY);
		TextureTracker_.Insert(name, resourceNamespace, newTexture);
		return newTexture;
	}

	ReUpload(existingTexture, image);
	return existingTexture;
}

void GLTextureManager::RemoveTexture(const FString& name, ResourceNamespace resourceNamespace)
{
	TextureTracker_.Remove(name, resourceNamespace);
}

int GLTextureManager::CalculateMipmapLevels(Geometry::Dimension dimension)
{
	int smallerAxis = std::min(dimension.Width, dimension.Height);
	return (int)std::floor(std::log2((double)smallerAxis));
}

GLLegacyTexture* GLTextureManager::CreateBrightMapTexture(const TextureImage* image, const FString* name, ResourceNamespace resourceNamespace, bool repeatY)
{
	GLLegacyTexture* texture = CreateTexture(image, name, resourceNamespace, repeatY);
	// Ensure that the brightmap texture is the null transparent texture. The debug option creates red/black checker texture for NullTexture.
	if (texture == NullTexture)
		return TransparentNullTexture;
	return texture;
}

GLLegacyTexture* GLTextureManager::CreateTexture(const TextureImage* image, bool repeatY)
{
	return CreateTexture(image, nullptr, ResourceNamespace::Global, repeatY);
}

GLLegacyTexture* GLTextureManager::CreateTexture(const TextureImage* image, const FString* name, ResourceNamespace resourceNamespace, bool repeatY)
{
	repeatY = resourceNamespace != ResourceNamespace::Sprites && resourceNamespace != ResourceNamespace::Brightmaps && repeatY;
	TextureTracker& textureTracker = GetTextureTracker(repeatY);
	GLLegacyTexture* texture;
	if (name != nullptr)
	{
		texture = textureTracker.GetOnly(*name, resourceNamespace);
		if (texture != nullptr)
			return texture;
	}

	if (image == nullptr || image->Empty())
		return NullTexture;

	TextureFlags flags = repeatY ? TextureFlags::Default : TextureFlags::ClampY;
	texture = GenerateTexture(*image, name != nullptr ? *name : FString(), resourceNamespace, flags);
	if (name != nullptr)
		textureTracker.Insert(*name, resourceNamespace, texture);

	return texture;
}

void GLTextureManager::DeleteTexture(GLLegacyTexture* texture, const FString& name, ResourceNamespace resourceNamespace)
{
	TextureTracker_.Remove(name, resourceNamespace);
	TextureTrackerClamp_.Remove(name, resourceNamespace);
	texture->Dispose();
}

void GLTextureManager::ReleaseUnmanagedResources()
{
	if (m_disposed)
		return;

	// Every texture the manager created is owned by OwnedTextures: the arrays first (their sub textures
	// do not own anything), then the rest.
	for (auto& arrayData : m_arrayTextures)
	{
		if (arrayData)
			arrayData->Destroy([](FGameTexture*) {});
	}
	m_arrayTextures.clear();

	TextureTracker_.Clear();
	TextureTrackerClamp_.Clear();
	m_spriteRotations.clear();
	m_translatedTextures.clear();
	m_standaloneTextures.clear();
	m_renderStores.Clear();
	OwnedTextures.clear();
	NullTexture = nullptr;
	TransparentNullTexture = nullptr;
	WhiteTexture = nullptr;
	BlackTexture = nullptr;

	m_disposed = true;
}

void GLTextureManager::Dispose()
{
	ReleaseUnmanagedResources();
}

}
