/*
** gl_renderstate_program.cpp
**
** FGLRenderState: render program mode (hw_renderprogram.h)
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
#include "hw_cvars.h"
#include "flatvertices.h"
#include "gl_shader.h"
#include "gl_renderer.h"
#include "gl_renderbuffers.h"
#include "gl_hwtexture.h"
#include "gl_buffers.h"
#include "gl_renderprogram.h"
#include "gl_samplers.h"
#include "hw_clock.h"

namespace OpenGLRenderer
{

static GLenum BlendFactorToGL(int f)
{
	static const GLenum table[] = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
	return table[clamp(f, 0, 9)];
}

static GLenum BlendEquationToGL(int e)
{
	static const GLenum table[] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX };
	return table[clamp(e, 0, 4)];
}

struct ProgramAttrFormat
{
	GLenum type;
	int size;
	bool normalize;
	bool integer;
};

static ProgramAttrFormat GetProgramAttrFormat(int format)
{
	switch (format)
	{
	case VFmt_Float4: return { GL_FLOAT, 4, false, false };
	case VFmt_Float3: return { GL_FLOAT, 3, false, false };
	case VFmt_Float2: return { GL_FLOAT, 2, false, false };
	case VFmt_Float: return { GL_FLOAT, 1, false, false };
	case VFmt_Byte4: return { GL_UNSIGNED_BYTE, 4, true, false };
	case VFmt_Packed_A2R10G10B10: return { GL_INT_2_10_10_10_REV, 4, true, false };
	case VFmt_Byte4_UInt: return { GL_UNSIGNED_BYTE, 4, false, true };
	case RPFmt_UInt: return { GL_UNSIGNED_INT, 1, false, true };
	case RPFmt_Int: return { GL_INT, 1, false, true };
	case RPFmt_UInt2: return { GL_UNSIGNED_INT, 2, false, true };
	case RPFmt_UInt4: return { GL_UNSIGNED_INT, 4, false, true };
	}
	return { GL_FLOAT, 4, false, false };
}

static GLenum RenderTargetFormatToImageFormat(int fmt)
{
	switch (fmt)
	{
	case RTF_RGBA8: return GL_RGBA8;
	case RTF_RGBA16F: return GL_RGBA16F;
	case RTF_RG16F: return GL_RG16F;
	case RTF_R8: return GL_R8;
	case RTF_R32F: return GL_R32F;
	case RTF_RGBA32F: return GL_RGBA32F;
	default: return GL_R32UI;
	}
}

static const int dt2glProgram[] = { GL_POINTS, GL_LINES, GL_TRIANGLES, GL_TRIANGLE_FAN, GL_TRIANGLE_STRIP };

//==========================================================================
//
// Entering / leaving program mode
//
//==========================================================================

void FGLRenderState::OnSetProgram(IRenderProgram *prog)
{
	// Going back to the table shaders restores their state immediately: other code (post processing,
	// 2D drawing) may touch GL directly before the next Apply().
	if (prog == nullptr && mInProgramMode) LeaveProgramMode();
}

void FGLRenderState::LeaveProgramMode()
{
	mInProgramMode = false;
	glProvokingVertex(GL_LAST_VERTEX_CONVENTION);

	if (mCurrentRenderTarget) SetRenderTarget(nullptr);

	// vertex attribute state: the table shader path re-enables everything it needs on the next buffer bind
	for (int i = 0; i < 16; i++)
	{
		glVertexAttribDivisor(i, 0);
		if (i >= VATTR_MAX) glDisableVertexAttribArray(i);
	}
	mProgramBufferSetupValid = false;
	mProgramBound = nullptr;
	mCurrentVertexBuffer = nullptr;
	mCurrentIndexBuffer = nullptr;

	// blending
	if (mProgramBlendActive)
	{
		glEnable(GL_BLEND);
		mProgramBlendActive = false;
	}
	ApplyBlendMode();
	stRenderStyle = mRenderStyle;

	// polygon offset, material bindings. The material is not re-applied: the pointer may be stale (its FMaterial can be
	// deleted after its last draw, e.g. the start screen's), and every textured table draw calls SetMaterial anyway.
	mBias.mChanged = true;
	mMaterial.Reset();
	lastMaterial = nullptr;
	maxBoundMaterial = -1;
	FHardwareTexture::InitGlobalState();
	mLastMappedLightIndex = SIZE_MAX;
	mLastMappedBoneIndexBase = SIZE_MAX;
}

//==========================================================================
//
// Blend / depth bias
//
//==========================================================================

void FGLRenderState::ApplyProgramBlend()
{
	if (mCustomBlend.enabled)
	{
		mProgramBlendActive = true;
		if (!mCustomBlend.blend)
		{
			glDisable(GL_BLEND);
		}
		else
		{
			glEnable(GL_BLEND);
			glBlendFuncSeparate(BlendFactorToGL(mCustomBlend.srcRGB), BlendFactorToGL(mCustomBlend.dstRGB), BlendFactorToGL(mCustomBlend.srcA), BlendFactorToGL(mCustomBlend.dstA));
			glBlendEquationSeparate(BlendEquationToGL(mCustomBlend.equationRGB), BlendEquationToGL(mCustomBlend.equationA));
			for (int i = 0; i < mCustomBlend.numAttachments; i++)
			{
				const auto &a = mCustomBlend.attachment[i];
				if (a.blend)
				{
					glBlendFuncSeparatei(i, BlendFactorToGL(a.srcRGB), BlendFactorToGL(a.dstRGB), BlendFactorToGL(a.srcA), BlendFactorToGL(a.dstA));
					glBlendEquationSeparatei(i, BlendEquationToGL(a.equationRGB), BlendEquationToGL(a.equationA));
				}
				else
				{
					// no per-attachment enable in GL 3.3 core: One/Zero with Add overwrites the attachment
					glBlendFuncSeparatei(i, GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
					glBlendEquationSeparatei(i, GL_FUNC_ADD, GL_FUNC_ADD);
				}
			}
		}
	}
	else
	{
		if (mProgramBlendActive)
		{
			// leaving custom blending: re-apply the style blend below
			glEnable(GL_BLEND);
			mProgramBlendActive = false;
			ApplyBlendMode();
			stRenderStyle = mRenderStyle;
		}
		else if (mRenderStyle != stRenderStyle)
		{
			ApplyBlendMode();
			stRenderStyle = mRenderStyle;
		}
	}
}

void FGLRenderState::ApplyProgramGraphicsState()
{
	ApplyProgramBlend();

	if (mBias.mChanged)
	{
		if (mBias.mFactor == 0 && mBias.mUnits == 0) glDisable(GL_POLYGON_OFFSET_FILL);
		else glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(mBias.mFactor, mBias.mUnits);
		mBias.mChanged = false;
	}
}

//==========================================================================
//
// Vertex setup. The program description carries the vertex layout; binding
// point 0 is the vertex buffer, binding point 1 the instance buffer.
//
//==========================================================================

void FGLRenderState::ApplyProgramVertexSetup(GLRenderProgram *prog)
{
	const auto &desc = prog->Desc();

	if (!mProgramBufferSetupValid || mProgramBound != prog || mProgramVertexBuffer != mVertexBuffer || mProgramVertexOffset != mVertexOffsets[0]
		|| mProgramInstanceBuffer != mInstanceBuffer || mProgramInstanceOffset != mInstanceOffset)
	{
		bool used[16] = {};
		for (auto &attr : desc.vertexAttributes)
		{
			if (attr.location < 0 || attr.location >= 16) continue;
			int binding = attr.binding == 1 ? 1 : 0;
			IVertexBuffer *vb = binding == 1 ? mInstanceBuffer : mVertexBuffer;
			if (!vb) continue;
			int offset = binding == 1 ? mInstanceOffset : mVertexOffsets[0];

			static_cast<GLBuffer*>(static_cast<GLVertexBuffer*>(vb))->Bind();
			auto fmt = GetProgramAttrFormat(attr.format);
			size_t ofs = attr.offset + desc.vertexStride[binding] * offset;

			glEnableVertexAttribArray(attr.location);
			if (fmt.integer) glVertexAttribIPointer(attr.location, fmt.size, fmt.type, (GLsizei)desc.vertexStride[binding], (void*)(intptr_t)ofs);
			else glVertexAttribPointer(attr.location, fmt.size, fmt.type, fmt.normalize, (GLsizei)desc.vertexStride[binding], (void*)(intptr_t)ofs);
			glVertexAttribDivisor(attr.location, binding == desc.instanceDivisorBinding ? 1 : 0);
			used[attr.location] = true;
		}
		for (int i = 0; i < 16; i++)
		{
			if (!used[i])
			{
				glDisableVertexAttribArray(i);
				glVertexAttribDivisor(i, 0);
			}
		}
		mProgramBufferSetupValid = true;
		mProgramVertexBuffer = mVertexBuffer;
		mProgramVertexOffset = mVertexOffsets[0];
		mProgramInstanceBuffer = mInstanceBuffer;
		mProgramInstanceOffset = mInstanceOffset;
	}

	if (mIndexBuffer != mCurrentIndexBuffer)
	{
		if (mIndexBuffer) static_cast<GLIndexBuffer*>(mIndexBuffer)->Bind();
		mCurrentIndexBuffer = mIndexBuffer;
	}
}

//==========================================================================
//
// Apply for programs
//
//==========================================================================

void FGLRenderState::ApplyProgram()
{
	auto prog = static_cast<GLRenderProgram*>(mProgram);
	if (!prog->IsValid() || prog->Desc().isCompute) return;

	if (!mInProgramMode)
	{
		mInProgramMode = true;
		// Helion's flat varyings (uv clamp) are taken from the first vertex of a triangle, which is also Vulkan's convention.
		glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
		mProgramBufferSetupValid = false;
		mProgramBound = nullptr;
	}

	if (mCurrentRenderTarget)
	{
		auto rt = static_cast<GLRenderTarget*>(mCurrentRenderTarget);
		unsigned int oldFBO = rt->FBO();
		rt->Validate();
		if (rt->FBO() != oldFBO)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, rt->FBO());
			mProgramChanged = true;
		}
	}

	bool programChanged = mProgramBound != prog;
	if (programChanged) glUseProgram(prog->Handle());

	ApplyProgramGraphicsState();
	ApplyProgramVertexSetup(prog);
	if (mProgramChanged || programChanged)
	{
		ApplyProgramResources(prog);
		mProgramChanged = false;
	}
	mProgramBound = prog;
}

//==========================================================================
//
// Uniform streaming: per-draw uniform blocks go through one big ring buffer.
//
//==========================================================================

void FGLRenderState::StreamProgramUniforms(int glBinding, const void *data, size_t size)
{
	size_t align = max<size_t>((size_t)gl.uniformblockalignment, 16);
	size_t asize = (size + align - 1) / align * align;

	if (!mUniformRing)
	{
		glGenBuffers(1, &mUniformRing);
		mUniformRingSize = max<size_t>(4 * 1024 * 1024, asize);
		glBindBuffer(GL_UNIFORM_BUFFER, mUniformRing);
		glBufferData(GL_UNIFORM_BUFFER, mUniformRingSize, nullptr, GL_STREAM_DRAW);
		mUniformRingPos = 0;
	}
	else
	{
		glBindBuffer(GL_UNIFORM_BUFFER, mUniformRing);
		if (mUniformRingPos + asize > mUniformRingSize)
		{
			// orphan the storage; the driver keeps the old one alive until the pending draws are done
			glBufferData(GL_UNIFORM_BUFFER, mUniformRingSize, nullptr, GL_STREAM_DRAW);
			mUniformRingPos = 0;
		}
	}
	glBufferSubData(GL_UNIFORM_BUFFER, mUniformRingPos, size, data);
	glBindBufferRange(GL_UNIFORM_BUFFER, glBinding, mUniformRing, mUniformRingPos, size);
	mUniformRingPos += asize;
}

//==========================================================================
//
// Resource binding
//
//==========================================================================

static void BindProgramTexture(int unit, GLenum target, unsigned int handle, int clampmode)
{
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(target, handle);
	if (target != GL_TEXTURE_BUFFER && GLRenderer->mSamplerManager)
	{
		GLRenderer->mSamplerManager->Bind(unit, clamp(clampmode, 0, NUMSAMPLERS - 1), 0);
	}
}

void FGLRenderState::ApplyProgramResources(GLRenderProgram *prog)
{
	for (auto &b : prog->Desc().bindings)
	{
		const auto &slot = mProgramSlots[b.binding];
		int unit = ProgramGLTextureUnit(b.binding);
		int glBinding = ProgramGLBufferBindingBase + b.binding;
		bool isInteger = b.kind == RenderProgramBinding::USampler2D || b.kind == RenderProgramBinding::USampler2DArray;

		switch (b.kind)
		{
		case RenderProgramBinding::UniformBlock:
		case RenderProgramBinding::StorageBuffer:
		{
			bool ssbo = b.kind == RenderProgramBinding::StorageBuffer;
			if (slot.type == FProgramBindingSlot::Uniforms && !ssbo)
			{
				StreamProgramUniforms(glBinding, &mProgramUniformData[slot.uniformsOffset], slot.uniformsSize);
			}
			else if (slot.type == FProgramBindingSlot::Buffer && slot.buffer)
			{
				auto glbuf = dynamic_cast<GLBuffer*>(slot.buffer);
				if (glbuf)
				{
					GLenum target = ssbo ? GL_SHADER_STORAGE_BUFFER : GL_UNIFORM_BUFFER;
					size_t size = slot.size ? slot.size : slot.buffer->Size() - slot.offset;
					glBindBufferRange(target, glBinding, glbuf->BufferId(), slot.offset, size);
				}
			}
			break;
		}

		case RenderProgramBinding::Sampler2D:
		case RenderProgramBinding::USampler2D:
		case RenderProgramBinding::Sampler2DArray:
		case RenderProgramBinding::USampler2DArray:
		{
			bool array = b.kind == RenderProgramBinding::Sampler2DArray || b.kind == RenderProgramBinding::USampler2DArray;
			GLenum target = array ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
			if (slot.type == FProgramBindingSlot::Texture && !slot.texture)
			{
				// Nothing bound: the default texture object reads black, a stale texture of an earlier draw must not stay on the unit.
				BindProgramTexture(unit, target, 0, CLAMP_NONE);
			}
			else if (slot.type == FProgramBindingSlot::Texture && slot.texture)
			{
				unsigned int handle = 0;
				if (auto arr = dynamic_cast<GLTextureArray*>(slot.texture)) handle = arr->TextureHandle();
				else handle = static_cast<FHardwareTexture*>(slot.texture)->GetTextureHandle();
				int clampmode = slot.clampmode;
				if (isInteger) clampmode = CLAMP_NOFILTER_XY;
				else if (slot.filter == 1 && clampmode < CLAMP_NOFILTER) clampmode += CLAMP_NOFILTER - CLAMP_NONE;
				BindProgramTexture(unit, target, handle, clampmode);
			}
			else if (slot.type == FProgramBindingSlot::RenderTargetTexture && slot.target)
			{
				auto rt = static_cast<GLRenderTarget*>(slot.target);
				unsigned int handle = slot.attachment >= 0 ? rt->Texture(slot.attachment) : rt->DepthTexture();
				BindProgramTexture(unit, GL_TEXTURE_2D, handle, CLAMP_NOFILTER_XY);
			}
			else if (slot.type == FProgramBindingSlot::SceneColorTexture)
			{
				BindProgramTexture(unit, GL_TEXTURE_2D, mSceneCopyTex, CLAMP_XY_NOMIP);
			}
			else if (slot.type == FProgramBindingSlot::SceneDepthTexture)
			{
				GLRenderer->mBuffers->BindSceneDepthTexture(unit);
				if (GLRenderer->mSamplerManager) GLRenderer->mSamplerManager->Bind(unit, CLAMP_NOFILTER_XY, 0);
			}
			else if (slot.type == FProgramBindingSlot::ShadowMapTexture)
			{
				GLRenderer->mBuffers->BindShadowMapTexture(unit);
				if (GLRenderer->mSamplerManager) GLRenderer->mSamplerManager->Bind(unit, CLAMP_NOFILTER_XY, 0);
			}
			break;
		}

		case RenderProgramBinding::SamplerBuffer:
		case RenderProgramBinding::USamplerBuffer:
		case RenderProgramBinding::ISamplerBuffer:
		{
			if (slot.type == FProgramBindingSlot::TexelBuffer && slot.buffer)
			{
				if (auto tb = dynamic_cast<GLTexelBuffer*>(slot.buffer))
					BindProgramTexture(unit, GL_TEXTURE_BUFFER, tb->TextureHandle(), 0);
			}
			break;
		}

		case RenderProgramBinding::Image2D:
		{
			if (slot.type == FProgramBindingSlot::RenderTargetTexture && slot.target)
			{
				auto rt = static_cast<GLRenderTarget*>(slot.target);
				GLenum access = b.qualifiers.Compare("readonly") == 0 ? GL_READ_ONLY : b.qualifiers.Compare("writeonly") == 0 ? GL_WRITE_ONLY : GL_READ_WRITE;
				glBindImageTexture(b.binding, rt->Texture(slot.attachment), 0, GL_FALSE, 0, access, RenderTargetFormatToImageFormat(rt->Desc().colorFormats[slot.attachment]));
			}
			break;
		}
		}
	}
	glActiveTexture(GL_TEXTURE0);
}

//==========================================================================
//
// Render targets
//
//==========================================================================

void FGLRenderState::SetRenderTarget(IRenderTarget *target)
{
	mCurrentRenderTarget = target;
	if (target)
	{
		auto rt = static_cast<GLRenderTarget*>(target);
		rt->Validate();
		glBindFramebuffer(GL_FRAMEBUFFER, rt->FBO());
	}
	else
	{
		GLRenderer->mBuffers->RebindSceneFB();
	}
}

void FGLRenderState::ClearRenderTarget(float r, float g, float b, float a)
{
	const float color[4] = { r, g, b, a };
	if (mCurrentRenderTarget)
	{
		auto rt = static_cast<GLRenderTarget*>(mCurrentRenderTarget);
		for (int i = 0; i < rt->NumAttachments(); i++)
		{
			if (rt->IsIntegerAttachment(i))
			{
				const GLuint icolor[4] = { (GLuint)r, (GLuint)g, (GLuint)b, (GLuint)a };
				glClearBufferuiv(GL_COLOR, i, icolor);
			}
			else
			{
				glClearBufferfv(GL_COLOR, i, color);
			}
		}
	}
	else
	{
		for (int i = 0; i < mNumDrawBuffers; i++)
			glClearBufferfv(GL_COLOR, i, color);
	}
}

//==========================================================================
//
// Copies the scene colour buffer (resolving multisampling) into a texture
// that can be bound with SetProgramSceneTexture.
//
//==========================================================================

void FGLRenderState::CopySceneColor()
{
	auto buffers = GLRenderer->mBuffers;
	int w = buffers->GetWidth(), h = buffers->GetHeight();

	if (!mSceneCopyTex || mSceneCopyWidth != w || mSceneCopyHeight != h)
	{
		GLint texBinding;
		glActiveTexture(GL_TEXTURE0);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &texBinding);
		if (!mSceneCopyTex) glGenTextures(1, &mSceneCopyTex);
		if (!mSceneCopyFBO) glGenFramebuffers(1, &mSceneCopyFBO);
		glBindTexture(GL_TEXTURE_2D, mSceneCopyTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindFramebuffer(GL_FRAMEBUFFER, mSceneCopyFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mSceneCopyTex, 0);
		glBindTexture(GL_TEXTURE_2D, texBinding);
		mSceneCopyWidth = w;
		mSceneCopyHeight = h;
	}

	bool scissor = !!glIsEnabled(GL_SCISSOR_TEST);
	if (scissor) glDisable(GL_SCISSOR_TEST);

	buffers->RebindSceneFB();	// binds read and draw
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mSceneCopyFBO);
	glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	if (scissor) glEnable(GL_SCISSOR_TEST);
	SetRenderTarget(mCurrentRenderTarget);
	mProgramChanged = true;
}

//==========================================================================
//
// Instanced draws
//
//==========================================================================

void FGLRenderState::DrawInstanced(int dt, int index, int count, int instanceCount)
{
	Apply();
	drawcalls.Clock();
	glDrawArraysInstanced(dt2glProgram[dt], index, count, instanceCount);
	drawcalls.Unclock();
}

void FGLRenderState::DrawIndexedInstanced(int dt, int index, int count, int instanceCount)
{
	Apply();
	drawcalls.Clock();
	glDrawElementsInstanced(dt2glProgram[dt], count, GL_UNSIGNED_INT, (void*)(intptr_t)(index * sizeof(uint32_t)), instanceCount);
	drawcalls.Unclock();
}

}
