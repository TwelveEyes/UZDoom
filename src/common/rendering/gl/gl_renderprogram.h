/*
** gl_renderprogram.h
**
** OpenGL implementation of the generic render program API (hw_renderprogram.h)
**
**---------------------------------------------------------------------------
**
** Copyright 2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include "gl_system.h"
#include "hw_renderprogram.h"
#include "hw_ihwtexture.h"
#include "gl_buffers.h"

namespace OpenGLRenderer
{

// Table shaders use texture units 16 (shadow map) and 17 (light map) for persistent state;
// program binding N lives on unit N for N < 16 and N + 2 above.
inline int ProgramGLTextureUnit(int binding)
{
	return binding < 16 ? binding : binding + 2;
}

class GLRenderProgram : public IRenderProgram
{
public:
	GLRenderProgram(const RenderProgramDesc& desc);
	~GLRenderProgram();

	bool IsValid() const override { return mProgram != 0; }
	const RenderProgramDesc& Desc() const override { return mDesc; }
	GLuint Handle() const { return mProgram; }

private:
	void Link(GLuint vertex, GLuint fragment, GLuint compute);
	void SetupBindings();

	RenderProgramDesc mDesc;
	GLuint mProgram = 0;
};

// A buffer exposed as samplerBuffer / usamplerBuffer / isamplerBuffer.
class GLTexelBuffer : public IDataBuffer, public GLBuffer
{
public:
	GLTexelBuffer(int format);
	~GLTexelBuffer();

	void SetData(size_t size, const void* data, BufferUsageType usage) override;
	void Resize(size_t newsize) override;
	void BindRange(FRenderState* state, size_t start, size_t length) override {}

	unsigned int TextureHandle() const { return mTexture; }

private:
	void AttachTexture();

	unsigned int mTexture = 0;
	int mInternalFormat = 0;
};

// GL_TEXTURE_2D_ARRAY texture (CreateTextureArray)
class GLTextureArray : public IHardwareTexture
{
public:
	GLTextureArray(int width, int height, int layers, int numchannels, bool integer, bool mipmap, const char* name);
	~GLTextureArray();

	void AllocateBuffer(int w, int h, int texelsize) override {}
	uint8_t* MapBuffer() override { return nullptr; }
	unsigned int CreateTexture(unsigned char* buffer, int w, int h, int texunit, bool mipmap, const char* name) override { return mTexture; }
	void UploadLayer(int layer, const unsigned char* buffer, int w, int h) override;
	void GenerateMipmaps() override;

	unsigned int TextureHandle() const { return mTexture; }

private:
	unsigned int mTexture = 0;
	int mWidth, mHeight, mLayers, mChannels;
	bool mInteger, mMipmap;
};

// Offscreen colour attachments (+ optionally the scene depth/stencil or an own depth texture).
class GLRenderTarget : public IRenderTarget
{
public:
	GLRenderTarget(const RenderTargetDesc& desc);
	~GLRenderTarget();

	int Width() const override { return mWidth; }
	int Height() const override { return mHeight; }
	const RenderTargetDesc& Desc() const override { return mDesc; }

	// Recreates the attachments when the scene size changed (desc size 0) or the scene depth was recreated.
	// returns true when the attachments were recreated
	bool Validate();
	unsigned int FBO() const { return mFBO; }
	unsigned int Texture(int i) const { return mTextures[i]; }
	unsigned int DepthTexture() const { return mOwnDepth; }
	int NumAttachments() const { return (int)mDesc.colorFormats.Size(); }
	bool IsIntegerAttachment(int i) const;

private:
	void Create();
	void Destroy();

	RenderTargetDesc mDesc;
	int mWidth = 0, mHeight = 0;
	unsigned int mFBO = 0;
	unsigned int mTextures[8] = {};
	unsigned int mOwnDepth = 0;
	unsigned int mSharedDepth = 0;	// the scene depth handle this FBO was built against
	bool mWarnedMultisample = false;
};

}
