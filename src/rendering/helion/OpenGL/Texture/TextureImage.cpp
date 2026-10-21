/*
** TextureImage.cpp
**
** UZDoom-only. See TextureImage.h.
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

#include <algorithm>
#include <cmath>
#include <cstring>
#include "c_cvars.h"
#include "textures.h"
#include "gametexture.h"
#include "hw_texcontainer.h"
#include "palettecontainer.h"
#include "TextureImage.h"

EXTERN_CVAR(Int, gl_texture_hqresizemode)
EXTERN_CVAR(Int, gl_texture_hqresizemult)

namespace Helion::Render::OpenGL::Texture
{

using namespace Geometry;
using Resources::ResourceNamespace;

TextureImage::TextureImage(Geometry::Dimension dimension, Vec2I offset, ResourceNamespace ns)
	: Dimension(dimension), Offset(offset), Namespace(ns)
{
	Pixels.assign((size_t)std::max(0, dimension.Width) * (size_t)std::max(0, dimension.Height) * 4, 0);
}

void TextureImage::ClearPixels()
{
	std::fill(Pixels.begin(), Pixels.end(), (uint8_t)0);
}

void TextureImage::CopyPixelsFrom(const TextureImage& other)
{
	int width = std::min(Width(), other.Width());
	int height = std::min(Height(), other.Height());
	for (int y = 0; y < height; y++)
		memcpy(&Pixels[(size_t)y * Width() * 4], &other.Pixels[(size_t)y * other.Width() * 4], (size_t)width * 4);
}

void TextureImage::SetPixel(int x, int y, uint32_t argb)
{
	uint8_t* p = &Pixels[((size_t)y * Width() + x) * 4];
	p[0] = (uint8_t)(argb & 0xFF);
	p[1] = (uint8_t)((argb >> 8) & 0xFF);
	p[2] = (uint8_t)((argb >> 16) & 0xFF);
	p[3] = (uint8_t)(argb >> 24);
}

uint32_t TextureImage::GetPixel(int x, int y) const
{
	const uint8_t* p = &Pixels[((size_t)y * Width() + x) * 4];
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void TextureImage::Fill(uint32_t argb)
{
	for (int y = 0; y < Height(); y++)
		for (int x = 0; x < Width(); x++)
			SetPixel(x, y, argb);
}

int TextureImage::TransparentPixelCount() const
{
	int count = 0;
	for (size_t i = 3; i < Pixels.size(); i += 4)
	{
		if (Pixels[i] == 0)
			count++;
	}
	return count;
}

void TextureImage::UpdateBlankRows()
{
	BlankRowsFromTop = 0;
	BlankRowsFromBottom = 0;
	int width = Width(), height = Height();
	auto rowBlank = [&](int y)
	{
		const uint8_t* p = &Pixels[(size_t)y * width * 4];
		for (int x = 0; x < width; x++)
		{
			if (p[x * 4 + 3] != 0)
				return false;
		}
		return true;
	};

	if (Pixels.empty())
		return;

	int top = 0;
	while (top < height && rowBlank(top))
		top++;

	// Completely transparent image: all rows are blank from the top only.
	if (top == height)
	{
		BlankRowsFromTop = height;
		return;
	}

	int bottom = 0;
	while (bottom < height && rowBlank(height - 1 - bottom))
		bottom++;

	BlankRowsFromTop = top;
	BlankRowsFromBottom = bottom;
}

const TextureImage& TextureImage::NullImage()
{
	static const TextureImage image = []()
	{
		const int Dimension = 8;
		const int HalfDimension = Dimension / 2;
		TextureImage image(Geometry::Dimension(Dimension, Dimension));
		image.Fill(0xFF000000);	// Color.Black

		for (int y = 0; y < HalfDimension; y++)
			for (int x = 0; x < HalfDimension; x++)
				image.SetPixel(x, y, 0xFFFF0000);	// Color.Red

		for (int y = HalfDimension; y < Dimension; y++)
			for (int x = HalfDimension; x < Dimension; x++)
				image.SetPixel(x, y, 0xFFFF0000);

		return image;
	}();
	return image;
}

const TextureImage& TextureImage::WhiteImage()
{
	static const TextureImage image = []()
	{
		TextureImage image(Geometry::Dimension(1, 1));
		image.Fill(0xFFFFFFFF);
		return image;
	}();
	return image;
}

const TextureImage& TextureImage::TransparentImage()
{
	static const TextureImage image = []()
	{
		TextureImage image(Geometry::Dimension(1, 1));
		image.Fill(0x00000000);
		return image;
	}();
	return image;
}

TextureImage TextureImage::CreateBlackImage(bool paletteColorMode)
{
	// Helion: PaletteWithArgb image with palette index 0 (black); the indexed upload stores
	// the index in red with AlphaFlag as alpha, which is also an opaque black pixel.
	TextureImage image(Geometry::Dimension(1, 1));
	image.Fill(paletteColorMode ? ((uint32_t)AlphaFlag << 24) : 0xFF000000u);
	return image;
}

// The factor FTexture::CreateUpsampledTextureBuffer multiplies the size with (hqresize.cpp), 1 if it does not scale.
static int UpscaleFactor()
{
	int type = gl_texture_hqresizemode;
	int mult = gl_texture_hqresizemult;
	if (mult < 2 || mult > 6 || type < 1 || type > 6) return 1;
	if (type < 4 && mult > 4) mult = 4;
	return mult;
}

Geometry::Dimension TextureImage::PredictUpscaledDimension(FGameTexture* texture, bool upscale, bool paletteColorMode)
{
	FTexture* tex = texture->GetTexture();
	int w = tex->GetWidth(), h = tex->GetHeight();
	// Indexed (palette mode) buffers are never scaled; the upscaler only runs for image backed textures.
	if (upscale && !paletteColorMode && tex->GetImage())
	{
		int factor = UpscaleFactor();
		w *= factor;
		h *= factor;
	}
	return Geometry::Dimension(w, h);
}

TextureImage TextureImage::FromGameTexture(FGameTexture* texture, int translation, bool brightmap, bool paletteColorMode, bool upscale)
{
	TextureImage image;
	FTexture* tex = brightmap ? texture->GetBrightmap() : texture->GetTexture();
	if (tex == nullptr || tex->GetWidth() <= 0 || tex->GetHeight() <= 0)
		return image;

	image.Offset = Vec2I(texture->GetTexelLeftOffset(), texture->GetTexelTopOffset());
	image.LogicalDimension = Geometry::Dimension((int)floorf(texture->GetDisplayWidth() + 0.5f), (int)floorf(texture->GetDisplayHeight() + 0.5f));

	// Brightmaps are always colour images: the shader reads their rgb as a light level.
	if (paletteColorMode && !brightmap)
	{
		FTextureBuffer buffer = tex->CreateTexBuffer(0, CTF_Indexed);
		if (buffer.mBuffer == nullptr)
			return image;

		image.Dimension = Geometry::Dimension(buffer.mWidth, buffer.mHeight);
		image.Pixels.resize((size_t)buffer.mWidth * buffer.mHeight * 4);
		const uint8_t* src = buffer.mBuffer;
		uint8_t* dest = image.Pixels.data();
		size_t count = (size_t)buffer.mWidth * buffer.mHeight;
		// Image.GetGlTexturePixels: alpha = transparent ? 0 : AlphaFlag, index in the red channel.
		// Index 0 is the transparent palette entry of UZDoom's game palette (PaletteContainer::BaseColors[0] = 0).
		for (size_t i = 0; i < count; i++)
		{
			uint8_t index = src[i];
			dest[i * 4 + 0] = 0;
			dest[i * 4 + 1] = 0;
			dest[i * 4 + 2] = index;
			dest[i * 4 + 3] = index == 0 ? 0 : AlphaFlag;
		}
	}
	else
	{
		int flags = CTF_ProcessData | (upscale ? CTF_Upscale : 0);
		FTextureBuffer buffer = tex->CreateTexBuffer(brightmap ? 0 : translation, flags);
		if (buffer.mBuffer == nullptr)
			return image;

		image.Dimension = Geometry::Dimension(buffer.mWidth, buffer.mHeight);
		image.Pixels.assign(buffer.mBuffer, buffer.mBuffer + (size_t)buffer.mWidth * buffer.mHeight * 4);
		// Image.FromArgbBytes: alpha 254 is reserved for the full bright flag.
		for (size_t i = 3; i < image.Pixels.size(); i += 4)
		{
			if (image.Pixels[i] == AlphaFlag)
				image.Pixels[i] = AlphaFlag + 1;
		}
	}

	image.Namespace = ResourceNamespace::Textures;
	image.UpdateBlankRows();
	return image;
}

}
