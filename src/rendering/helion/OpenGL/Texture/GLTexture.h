/*
** GLTexture.h
**
** Port of Helion.Render.OpenGL.Texture.GLTexture.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** GL.GenTexture/DeleteTexture are replaced by an IHardwareTexture created through
** DFrameBuffer (CreateHardwareTexture / CreateTextureArray). Binding happens
** through Shader/TextureUnits.h (Bind(TextureUnit) in GLLegacyTexture) instead of
** GL.BindTexture. TextureId is a manager-unique number, not an API object name.
** The IRenderableTextureHandle Dimension() accessor is the C# Dimension property;
** the field of the same name in Helion is m_dimension here.
**
*/

#pragma once

#include "zstring.h"
#include "hw_ihwtexture.h"
#include "Resources/ResourceNamespace.h"
#include "Common/Textures/IRenderableTextureHandle.h"

namespace Helion::Render::OpenGL::Texture
{

// Declared in GLTextureManager.cs upstream; here to keep GLLegacyTexture independent of the manager.
// The values equal CLAMP_NONE, CLAMP_X, CLAMP_Y, CLAMP_XY of the backend (textures.h).
enum class TextureFlags
{
	Default,
	ClampX = 1,
	ClampY = 2,
};

constexpr TextureFlags operator|(TextureFlags a, TextureFlags b) { return (TextureFlags)((int)a | (int)b); }
constexpr TextureFlags operator&(TextureFlags a, TextureFlags b) { return (TextureFlags)((int)a & (int)b); }

enum class TextureContext
{
	Default,
	WorldArray,
	WorldSprites,
	Hud
};

// OpenTK TextureTarget (only the two targets the texture managers use).
enum class TextureTarget
{
	Texture2D,
	Texture2DArray
};

class GLTexture : public Common::Textures::IRenderableTextureHandle
{
public:
	int Index() const override { return TextureId; }
	Geometry::Box2I Area() const override { return Geometry::Box2I(Geometry::Vec2I(0, 0), Geometry::Vec2I(m_dimension.Width, m_dimension.Height)); }
	Geometry::Box2F UV() const override { return Geometry::Box2F(Geometry::Vec2F(0, 0), Geometry::Vec2F(1, 1)); }
	Geometry::Dimension Dimension() const override { return m_dimension; }
	Geometry::Vec2I Offset() const override { return m_offset; }

	const int TextureId;
	FString Name;
	const Geometry::Vec2F UVInverse;
	const Resources::ResourceNamespace Namespace;
	const TextureTarget Target;
	const int TransparentPixelCount;
	const TextureContext Context;

	int Width() const { return m_dimension.Width; }
	int Height() const { return m_dimension.Height; }

	int BlankRowsFromTop;
	int BlankRowsFromBottom;

	GLTexture(const GLTexture&) = delete;
	GLTexture& operator=(const GLTexture&) = delete;
	~GLTexture() override;

	// The backend object this texture samples from. Array sub textures return their parent's.
	virtual IHardwareTexture* HardwareTexture() const { return m_hwTexture; }

	void Dispose();

	// UZDoom-only: textures are created before their backend object (see LegacyGLTextureManager::UploadAndSetParameters).
	void SetHardwareTexture(IHardwareTexture* hwTexture) { m_hwTexture = hwTexture; }

protected:
	GLTexture(int textureId, const FString& name, Geometry::Dimension dimension, Geometry::Vec2I offset, Resources::ResourceNamespace ns,
		TextureTarget target, int transparentPixelCount, int blankRowsFromTop, int blankRowsFromBottom, IHardwareTexture* hwTexture,
		bool ownsTexture = true, TextureContext textureContext = TextureContext::Default);

private:
	Geometry::Dimension m_dimension;
	Geometry::Vec2I m_offset;
	IHardwareTexture* m_hwTexture;
	bool m_ownsTexture;
	bool m_disposed = false;

	void ReleaseUnmanagedResources();
};

}
