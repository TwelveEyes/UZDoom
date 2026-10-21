/*
** gl_renderprogram.cpp
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

#include "gl_system.h"
#include "gl_interface.h"
#include "gl_renderprogram.h"
#include "gl_renderer.h"
#include "gl_renderbuffers.h"
#include "gl_debug.h"
#include "gl_hwtexture.h"
#include "engineerrors.h"
#include "printf.h"
#include "v_video.h"
#include "textures.h"

namespace OpenGLRenderer
{

bool IsShaderCacheActive();
TArray<uint8_t> LoadCachedProgramBinary(const FString &vertex, const FString &fragment, uint32_t &binaryFormat, const char *name = nullptr);
void SaveCachedProgramBinary(const FString &vertex, const FString &fragment, const TArray<uint8_t> &binary, uint32_t binaryFormat, const char *name = nullptr);

//==========================================================================
//
// Format tables
//
//==========================================================================

struct GLTexelFormatInfo
{
	GLenum internalFormat;
	bool valid;
};

static GLenum TexelBufferFormat(int format)
{
	switch (format)
	{
	case VFmt_Float4: return GL_RGBA32F;
	case VFmt_Float2: return GL_RG32F;
	case VFmt_Float: return GL_R32F;
	case VFmt_Byte4: return GL_RGBA8;
	case VFmt_Byte4_UInt: return GL_RGBA8UI;
	case RPFmt_UInt: return GL_R32UI;
	case RPFmt_Int: return GL_R32I;
	case RPFmt_UInt2: return GL_RG32UI;
	case RPFmt_UInt4: return GL_RGBA32UI;
	default:
		I_FatalError("CreateTexelBuffer: unsupported format %d", format);
		return 0;
	}
}

struct RTFormatInfo
{
	GLenum internalFormat, format, type;
	bool integer;
};

static const RTFormatInfo &GetRTFormat(int fmt)
{
	static const RTFormatInfo infos[] =
	{
		{ GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false },		// RTF_RGBA8
		{ GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, false },		// RTF_RGBA16F
		{ GL_RG16F, GL_RG, GL_HALF_FLOAT, false },			// RTF_RG16F
		{ GL_R8, GL_RED, GL_UNSIGNED_BYTE, false },			// RTF_R8
		{ GL_R32F, GL_RED, GL_FLOAT, false },				// RTF_R32F
		{ GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, true },// RTF_R32UI
		{ GL_RGBA32F, GL_RGBA, GL_FLOAT, false },			// RTF_RGBA32F
	};
	return infos[clamp(fmt, 0, (int)RTF_RGBA32F)];
}

//==========================================================================
//
// GLRenderProgram
//
//==========================================================================

static FString GetShaderLog(GLuint handle, bool program)
{
	char buffer[10000];
	GLsizei length = 0;
	buffer[0] = 0;
	if (program) glGetProgramInfoLog(handle, sizeof(buffer), &length, buffer);
	else glGetShaderInfoLog(handle, sizeof(buffer), &length, buffer);
	return FString(buffer);
}

static GLuint CompileStage(GLenum type, const FString &source, const FString &progname)
{
	GLuint handle = glCreateShader(type);
	int lengths[1] = { (int)source.Len() };
	const char *sources[1] = { source.GetChars() };
	glShaderSource(handle, 1, sources, lengths);
	glCompileShader(handle);

	GLint status = 0;
	glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE)
	{
		const char *stage = type == GL_VERTEX_SHADER ? "vertex" : type == GL_FRAGMENT_SHADER ? "fragment" : "compute";
		I_FatalError("Render program %s: %s shader failed to compile:\n%s\n", progname.GetChars(), stage, GetShaderLog(handle, false).GetChars());
	}
	return handle;
}

GLRenderProgram::GLRenderProgram(const RenderProgramDesc &desc) : mDesc(desc)
{
	FString vs, fs;
	if (desc.isCompute)
	{
		if (!(gl.flags & RFL_COMPUTE_SHADER))
			I_FatalError("Render program %s: compute shaders require OpenGL 4.3", desc.name.GetChars());
		vs = GenerateProgramPrelude(desc, false, false, true) + desc.computeSource;
	}
	else
	{
		vs = GenerateProgramPrelude(desc, false, false, false) + desc.vertexSource;
		fs = GenerateProgramPrelude(desc, false, true, false) + desc.fragmentSource;
	}

	uint32_t binaryFormat = 0;
	TArray<uint8_t> binary;
	if (IsShaderCacheActive())
		binary = LoadCachedProgramBinary(vs, fs, binaryFormat, desc.name.GetChars());

	bool loadedFromBinary = false;
	if (binary.Size() > 0 && glProgramBinary)
	{
		mProgram = glCreateProgram();
		glProgramBinary(mProgram, binaryFormat, binary.Data(), binary.Size());
		GLint status = 0;
		glGetProgramiv(mProgram, GL_LINK_STATUS, &status);
		loadedFromBinary = (status == GL_TRUE);
		if (!loadedFromBinary)
		{
			glDeleteProgram(mProgram);
			mProgram = 0;
		}
	}

	if (!loadedFromBinary)
	{
		mProgram = glCreateProgram();
		GLuint shaders[2] = {};
		if (desc.isCompute)
		{
			shaders[0] = CompileStage(GL_COMPUTE_SHADER, vs, desc.name);
			glAttachShader(mProgram, shaders[0]);
		}
		else
		{
			shaders[0] = CompileStage(GL_VERTEX_SHADER, vs, desc.name);
			shaders[1] = CompileStage(GL_FRAGMENT_SHADER, fs, desc.name);
			glAttachShader(mProgram, shaders[0]);
			glAttachShader(mProgram, shaders[1]);

			// The locations are also given in the shader text; this is the fallback for drivers that ignore layout(location) on inputs.
			for (auto &io : desc.inputsOutputs)
			{
				if (io.position == RenderProgramIO::VInput && io.location >= 0)
					glBindAttribLocation(mProgram, io.location, io.name.GetChars());
			}
		}

		glLinkProgram(mProgram);
		GLint status = 0;
		glGetProgramiv(mProgram, GL_LINK_STATUS, &status);
		if (status == GL_FALSE)
		{
			I_FatalError("Render program %s: link failed:\n%s\n", desc.name.GetChars(), GetShaderLog(mProgram, true).GetChars());
		}

		for (int i = 0; i < 2; i++)
		{
			if (shaders[i])
			{
				glDetachShader(mProgram, shaders[i]);
				glDeleteShader(shaders[i]);
			}
		}

		if (glProgramBinary && IsShaderCacheActive())
		{
			int binaryLength = 0;
			glGetProgramiv(mProgram, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
			binary.Resize(binaryLength);
			glGetProgramBinary(mProgram, binary.Size(), &binaryLength, &binaryFormat, binary.Data());
			binary.Resize(binaryLength);
			SaveCachedProgramBinary(vs, fs, binary, binaryFormat, desc.name.GetChars());
		}
	}

	FGLDebug::LabelObject(GL_PROGRAM, mProgram, desc.name.GetChars());
	SetupBindings();
}

GLRenderProgram::~GLRenderProgram()
{
	if (mProgram != 0) glDeleteProgram(mProgram);
}

//==========================================================================
//
// GL 3.3 lacks layout(binding): assign sampler units and block binding
// points after linking.
//
//==========================================================================

void GLRenderProgram::SetupBindings()
{
	glUseProgram(mProgram);
	for (auto &b : mDesc.bindings)
	{
		switch (b.kind)
		{
		case RenderProgramBinding::UniformBlock:
		{
			FString type = b.blockTypeName.Len() ? b.blockTypeName : (FString(b.name) + "_t");
			GLuint index = glGetUniformBlockIndex(mProgram, type.GetChars());
			if (index != GL_INVALID_INDEX)
				glUniformBlockBinding(mProgram, index, ProgramGLBufferBindingBase + b.binding);
			break;
		}
		case RenderProgramBinding::StorageBuffer:
		case RenderProgramBinding::Image2D:
			break;	// layout(binding) in the shader (GL 4.3)
		default:
		{
			GLint loc = glGetUniformLocation(mProgram, b.name);
			if (loc >= 0) glUniform1i(loc, ProgramGLTextureUnit(b.binding));
			break;
		}
		}
	}
}

//==========================================================================
//
// GLTexelBuffer
//
//==========================================================================

GLTexelBuffer::GLTexelBuffer(int format) : GLBuffer(GL_TEXTURE_BUFFER)
{
	mInternalFormat = (int)TexelBufferFormat(format);
	glGenTextures(1, &mTexture);
}

GLTexelBuffer::~GLTexelBuffer()
{
	if (mTexture != 0) glDeleteTextures(1, &mTexture);
}

void GLTexelBuffer::AttachTexture()
{
	GLint binding = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &binding);
	glBindTexture(GL_TEXTURE_BUFFER, mTexture);
	glTexBuffer(GL_TEXTURE_BUFFER, mInternalFormat, mBufferId);
	glBindTexture(GL_TEXTURE_BUFFER, binding);
}

void GLTexelBuffer::SetData(size_t size, const void *data, BufferUsageType usage)
{
	GLBuffer::SetData(size, data, usage);
	AttachTexture();
}

void GLTexelBuffer::Resize(size_t newsize)
{
	GLBuffer::Resize(newsize);
	AttachTexture();
}

//==========================================================================
//
// GLTextureArray
//
//==========================================================================

GLTextureArray::GLTextureArray(int width, int height, int layers, int numchannels, bool integer, bool mipmap, const char *name)
	: mWidth(width), mHeight(height), mLayers(layers), mChannels(numchannels), mInteger(integer), mMipmap(mipmap)
{
	mWidth = min(mWidth, gl.max_texturesize);
	mHeight = min(mHeight, gl.max_texturesize);

	GLint binding = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &binding);

	glGenTextures(1, &mTexture);
	glBindTexture(GL_TEXTURE_2D_ARRAY, mTexture);
	FGLDebug::LabelObject(GL_TEXTURE, mTexture, name);

	static const GLenum normalFormats[] = { GL_R8, GL_RG8, GL_RGB8, GL_RGBA8 };
	static const GLenum integerFormats[] = { GL_R8UI, GL_RG8UI, GL_RGB8UI, GL_RGBA8UI };
	GLenum internalFormat = (integer ? integerFormats : normalFormats)[clamp(numchannels, 1, 4) - 1];

	int levels = 1;
	if (mipmap)
	{
		int m = max(mWidth, mHeight);
		while (m > 1) { m >>= 1; levels++; }
	}

	// Immutable storage so layers can be uploaded independently.
	glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels, internalFormat, mWidth, mHeight, layers);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, levels - 1);
	glBindTexture(GL_TEXTURE_2D_ARRAY, binding);
}

GLTextureArray::~GLTextureArray()
{
	if (mTexture != 0) glDeleteTextures(1, &mTexture);
}

void GLTextureArray::UploadLayer(int layer, const unsigned char *buffer, int w, int h)
{
	if (layer < 0 || layer >= mLayers || !buffer) return;
	w = min(w, mWidth);
	h = min(h, mHeight);

	GLint binding = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &binding);
	glBindTexture(GL_TEXTURE_2D_ARRAY, mTexture);

	static const GLenum normalSource[] = { GL_RED, GL_RG, GL_BGR, GL_BGRA };
	static const GLenum integerSource[] = { GL_RED_INTEGER, GL_RG_INTEGER, GL_BGR_INTEGER, GL_BGRA_INTEGER };
	GLenum source = (mInteger ? integerSource : normalSource)[clamp(mChannels, 1, 4) - 1];

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, w, h, 1, source, GL_UNSIGNED_BYTE, buffer);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	glBindTexture(GL_TEXTURE_2D_ARRAY, binding);
}

void GLTextureArray::GenerateMipmaps()
{
	if (!mMipmap || mInteger) return;
	GLint binding = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &binding);
	glBindTexture(GL_TEXTURE_2D_ARRAY, mTexture);
	glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	glBindTexture(GL_TEXTURE_2D_ARRAY, binding);
}

//==========================================================================
//
// GLRenderTarget
//
//==========================================================================

GLRenderTarget::GLRenderTarget(const RenderTargetDesc &desc) : mDesc(desc)
{
	if (mDesc.colorFormats.Size() == 0 || mDesc.colorFormats.Size() > 8)
		I_FatalError("Render target %s: invalid number of colour attachments (%d)", desc.name.GetChars(), (int)mDesc.colorFormats.Size());
	Create();
}

GLRenderTarget::~GLRenderTarget()
{
	Destroy();
}

bool GLRenderTarget::IsIntegerAttachment(int i) const
{
	return GetRTFormat(mDesc.colorFormats[i]).integer;
}

void GLRenderTarget::Destroy()
{
	if (mFBO) glDeleteFramebuffers(1, &mFBO);
	for (auto &t : mTextures)
	{
		if (t) glDeleteTextures(1, &t);
		t = 0;
	}
	if (mOwnDepth) glDeleteTextures(1, &mOwnDepth);
	mFBO = mOwnDepth = mSharedDepth = 0;
}

void GLRenderTarget::Create()
{
	auto buffers = GLRenderer->mBuffers;
	mWidth = mDesc.width > 0 ? mDesc.width : buffers->GetWidth();
	mHeight = mDesc.height > 0 ? mDesc.height : buffers->GetHeight();

	GLint activeTex, texBinding, fbBinding;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &texBinding);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbBinding);

	glGenFramebuffers(1, &mFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
	FGLDebug::LabelObject(GL_FRAMEBUFFER, mFBO, mDesc.name.GetChars());

	GLenum drawBuffers[8];
	for (unsigned i = 0; i < mDesc.colorFormats.Size(); i++)
	{
		auto &info = GetRTFormat(mDesc.colorFormats[i]);
		glGenTextures(1, &mTextures[i]);
		glBindTexture(GL_TEXTURE_2D, mTextures[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, info.internalFormat, mWidth, mHeight, 0, info.format, info.type, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, mTextures[i], 0);
		drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
		FString name;
		name.Format("%s[%d]", mDesc.name.GetChars(), i);
		FGLDebug::LabelObject(GL_TEXTURE, mTextures[i], name.GetChars());
	}
	glDrawBuffers((GLsizei)mDesc.colorFormats.Size(), drawBuffers);

	if (mDesc.ownDepth)
	{
		glGenTextures(1, &mOwnDepth);
		glBindTexture(GL_TEXTURE_2D, mOwnDepth);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, mWidth, mHeight, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, mOwnDepth, 0);
	}
	else if (mDesc.shareSceneDepth)
	{
		if (buffers->GetSamples() > 1)
		{
			// The multisampled scene depth cannot be paired with single sample colour attachments.
			if (!mWarnedMultisample)
			{
				Printf(TEXTCOLOR_RED "Render target %s: sharing the scene depth buffer is not supported with multisampling; rendering without depth.\n", mDesc.name.GetChars());
				mWarnedMultisample = true;
			}
		}
		else
		{
			bool isTexture = false;
			GLuint depth = buffers->GetSceneDepthStencilHandle(isTexture);
			if (depth)
			{
				if (isTexture) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
				else glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
				mSharedDepth = depth;
			}
		}
	}

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
		I_FatalError("Render target %s: framebuffer incomplete (0x%x)", mDesc.name.GetChars(), (unsigned)status);

	glBindFramebuffer(GL_FRAMEBUFFER, fbBinding);
	glBindTexture(GL_TEXTURE_2D, texBinding);
	glActiveTexture(activeTex);
}

bool GLRenderTarget::Validate()
{
	auto buffers = GLRenderer->mBuffers;
	bool recreate = false;
	if (mDesc.width <= 0 && mWidth != buffers->GetWidth()) recreate = true;
	if (mDesc.height <= 0 && mHeight != buffers->GetHeight()) recreate = true;
	if (mDesc.shareSceneDepth && !mDesc.ownDepth && buffers->GetSamples() <= 1)
	{
		bool isTexture;
		if (buffers->GetSceneDepthStencilHandle(isTexture) != mSharedDepth) recreate = true;
	}
	if (recreate)
	{
		Destroy();
		Create();
	}
	return recreate;
}

}
