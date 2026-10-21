/*
** GLTexture.cpp
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
*/

#include "GLTexture.h"

namespace Helion::Render::OpenGL::Texture
{

using namespace Geometry;

GLTexture::GLTexture(int textureId, const FString& name, Geometry::Dimension dimension, Vec2I offset, Resources::ResourceNamespace ns,
	TextureTarget target, int transparentPixelCount, int blankRowsFromTop, int blankRowsFromBottom, IHardwareTexture* hwTexture,
	bool ownsTexture, TextureContext textureContext)
	: TextureId(textureId),
	Name(name),
	UVInverse(1.0f / (float)dimension.Width, 1.0f / (float)dimension.Height),
	Namespace(ns),
	Target(target),
	TransparentPixelCount(transparentPixelCount),
	Context(textureContext),
	BlankRowsFromTop(blankRowsFromTop),
	BlankRowsFromBottom(blankRowsFromBottom),
	m_dimension(dimension),
	m_offset(offset),
	m_hwTexture(hwTexture),
	m_ownsTexture(ownsTexture)
{
}

GLTexture::~GLTexture()
{
	ReleaseUnmanagedResources();
}

void GLTexture::Dispose()
{
	ReleaseUnmanagedResources();
}

void GLTexture::ReleaseUnmanagedResources()
{
	if (m_disposed || !m_ownsTexture)
		return;

	delete m_hwTexture;
	m_hwTexture = nullptr;

	m_disposed = true;
}

}
