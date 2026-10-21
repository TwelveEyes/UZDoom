/*
** TextureImage.h
**
** UZDoom-only. The pixel container that stands in for Helion.Graphics.Image on the texture upload path.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion uploads Helion.Graphics.Image objects. UZDoom keeps its pixels in FTexture,
** so TextureImage is the BGRA8 copy that is handed to IHardwareTexture::UploadLayer /
** CreateTexture. In palette colour mode (vid_colormode 1) the red channel holds the
** palette index and alpha is 0 (transparent) or Image.AlphaFlag (254, "fetch from the
** colormap buffer"), as Image.GetGlTexturePixels(indexedSupported) produces in Helion.
** In truecolor mode alpha 254 is a reserved value (full bright flag); it is bumped to
** 255 like Image.FromArgbBytes does.
**
*/

#pragma once

#include <vector>
#include <cstdint>
#include "Util/Geometry.h"
#include "Resources/ResourceNamespace.h"

class FGameTexture;

namespace Helion::Render::OpenGL::Texture
{

struct TextureImage
{
	// Image.AlphaFlag
	static constexpr uint8_t AlphaFlag = 254;

	Geometry::Dimension Dimension;
	// UZDoom-only: the size in map units (FGameTexture display size) the world geometry maps the image with.
	// It differs from Dimension for scaled and hires textures. (0, 0) = same as Dimension.
	Geometry::Dimension LogicalDimension;
	Geometry::Vec2I Offset;
	Resources::ResourceNamespace Namespace = Resources::ResourceNamespace::Global;
	std::vector<uint8_t> Pixels;	// BGRA, row 0 = top
	int BlankRowsFromTop = 0;
	int BlankRowsFromBottom = 0;

	TextureImage() = default;
	TextureImage(Geometry::Dimension dimension, Geometry::Vec2I offset = Geometry::Vec2I(0, 0), Resources::ResourceNamespace ns = Resources::ResourceNamespace::Global);

	int Width() const { return Dimension.Width; }
	int Height() const { return Dimension.Height; }
	bool Empty() const { return Pixels.empty(); }
	Geometry::Dimension GetLogicalDimension() const { return LogicalDimension.Width > 0 && LogicalDimension.Height > 0 ? LogicalDimension : Dimension; }

	void ClearPixels();
	// Copies the pixels of another image into the top left corner (Image.CopyPixelsFrom).
	void CopyPixelsFrom(const TextureImage& other);
	void SetPixel(int x, int y, uint32_t argb);
	uint32_t GetPixel(int x, int y) const;
	void Fill(uint32_t argb);

	int TransparentPixelCount() const;
	// Recalculates BlankRowsFromTop/BlankRowsFromBottom from the alpha channel.
	void UpdateBlankRows();

	static const TextureImage& NullImage();
	static const TextureImage& WhiteImage();
	static const TextureImage& TransparentImage();
	static TextureImage CreateBlackImage(bool paletteColorMode);

	// Reads the pixels of a game texture (UZDoom-only).
	//   translation   FTranslationID::index() applied to the pixels; ignored in palette mode where the
	//                 translation is a colormap of the colormap buffer.
	//   brightmap     read the brightmap layer of the game texture instead of the base layer.
	//   upscale       apply the hqresize upscaler (CTF_Upscale), see PredictUpscaledDimension.
	// Returns an empty image if the layer has no pixels.
	static TextureImage FromGameTexture(FGameTexture* texture, int translation, bool brightmap, bool paletteColorMode, bool upscale);

	// The size of the pixel buffer FromGameTexture will return for the base layer.
	static Geometry::Dimension PredictUpscaledDimension(FGameTexture* texture, bool upscale, bool paletteColorMode);
};

}
