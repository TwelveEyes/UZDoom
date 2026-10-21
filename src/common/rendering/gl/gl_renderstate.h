/*
** gl_renderstate.h
**
**
**
**---------------------------------------------------------------------------
**
** Copyright 2009-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#ifndef __GL_RENDERSTATE_H
#define __GL_RENDERSTATE_H

#include <algorithm>
#include <string.h>
#include "gl_interface.h"
#include "matrix.h"
#include "hw_renderstate.h"
#include "hw_material.h"
#include "c_cvars.h"

namespace OpenGLRenderer
{

class FShader;
class GLRenderProgram;
class GLRenderTarget;
struct HWSectorPlane;

class FGLRenderState final : public FRenderState
{
	uint8_t mLastDepthClamp : 1;

	float mGlossiness, mSpecularLevel;
	float mShaderTimer;

	int mEffectState;
	int mTempTM = TM_NORMAL;

	FRenderStyle stRenderStyle;
	int stSrcBlend, stDstBlend;
	bool stAlphaTest;
	bool stSplitEnabled;
	int stBlendEquation;

	FShader *activeShader;

	int mNumDrawBuffers = 1;

	bool ApplyShader();
	void ApplyState();

	// Texture binding state
	FMaterial *lastMaterial = nullptr;
	int lastClamp = 0;
	int lastTranslation = 0;
	int maxBoundMaterial = -1;
	size_t mLastMappedLightIndex = SIZE_MAX;
	size_t mLastMappedBoneIndexBase = SIZE_MAX;

	IVertexBuffer *mCurrentVertexBuffer;
	int mCurrentVertexOffsets[2];	// one per binding point
	IIndexBuffer *mCurrentIndexBuffer;

	// Render program mode (hw_renderprogram.h)
	bool mInProgramMode = false;
	bool mProgramBlendActive = false;
	bool mProgramBufferSetupValid = false;
	IVertexBuffer *mProgramVertexBuffer = nullptr;
	IVertexBuffer *mProgramInstanceBuffer = nullptr;
	int mProgramVertexOffset = 0, mProgramInstanceOffset = 0;
	GLRenderProgram *mProgramBound = nullptr;
	IRenderTarget *mCurrentRenderTarget = nullptr;
	unsigned int mUniformRing = 0;
	size_t mUniformRingSize = 0, mUniformRingPos = 0;
	unsigned int mSceneCopyTex = 0, mSceneCopyFBO = 0;
	int mSceneCopyWidth = 0, mSceneCopyHeight = 0;

	void ApplyProgram();
	void ApplyProgramGraphicsState();
	void ApplyProgramVertexSetup(GLRenderProgram *prog);
	void ApplyProgramBlend();
	void LeaveProgramMode();
	void StreamProgramUniforms(int glBinding, const void *data, size_t size);
	void BindSceneCopy(int unit);

public:

	// Binds all resources of the program's bindings (graphics and compute).
	void ApplyProgramResources(GLRenderProgram *prog);

	FGLRenderState()
	{
		Reset();
	}

	void Reset();

	void ClearLastMaterial()
	{
		lastMaterial = nullptr;
	}

	void ApplyMaterial();

	void Apply();
	void ApplyBuffers();
	void ApplyBlendMode();

	void ResetVertexBuffer()
	{
		// forces rebinding with the next 'apply' call.
		mCurrentVertexBuffer = nullptr;
		mCurrentIndexBuffer = nullptr;
		mProgramBufferSetupValid = false;
	}

	void SetSpecular(float glossiness, float specularLevel)
	{
		mGlossiness = glossiness;
		mSpecularLevel = specularLevel;
	}

	void EnableDrawBuffers(int count, bool apply = false) override
	{
		count = min(count, 3);
		if (mNumDrawBuffers != count)
		{
			static GLenum buffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
			glDrawBuffers(count, buffers);
			mNumDrawBuffers = count;
		}
		if (apply) Apply();
	}

	void ToggleState(int state, bool on);

	void ClearScreen() override;
	void Draw(int dt, int index, int count, bool apply = true) override;
	void DrawIndexed(int dt, int index, int count, bool apply = true) override;

	bool SetDepthClamp(bool on) override;
	void SetDepthMask(bool on) override;
	void SetDepthFunc(int func) override;
	void SetDepthRange(float min, float max) override;
	void SetColorMask(bool r, bool g, bool b, bool a) override;
	void SetStencil(int offs, int op, int flags) override;
	void SetCulling(int mode) override;
	void EnableClipDistance(int num, bool state) override;
	void Clear(int targets) override;
	void EnableStencil(bool on) override;
	void SetScissor(int x, int y, int w, int h) override;
	void SetViewport(int x, int y, int w, int h) override;
	void EnableDepthTest(bool on) override;
	void EnableMultisampling(bool on) override;
	void EnableLineSmooth(bool on) override;

	void OnSetProgram(IRenderProgram *prog) override;
	void SetRenderTarget(IRenderTarget *target) override;
	void ClearRenderTarget(float r, float g, float b, float a) override;
	void CopySceneColor() override;
	void DrawInstanced(int dt, int index, int count, int instanceCount) override;
	void DrawIndexedInstanced(int dt, int index, int count, int instanceCount) override;


};

extern FGLRenderState gl_RenderState;

}

#endif
