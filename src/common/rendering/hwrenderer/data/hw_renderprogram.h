/*
** hw_renderprogram.h
**
** Generic render program API: explicit GLSL programs with their own
** bindings, used by the Helion-style scene renderer (src/rendering/helion).
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

//==========================================================================
//
// HEADER FILES
//
//==========================================================================

#include <stddef.h>
#include <stdint.h>
#include "tarray.h"
#include "zstring.h"
#include "buffers.h"
#include "shaderuniforms.h"

class IHardwareTexture;

//==========================================================================
//
// TYPES
//
//==========================================================================

// Extra vertex/texel formats used by render programs (appended to EVertexAttributeFormat's values).
enum ERenderProgramFormat
{
	RPFmt_Float4 = VFmt_Float4,
	RPFmt_Float3 = VFmt_Float3,
	RPFmt_Float2 = VFmt_Float2,
	RPFmt_Float = VFmt_Float,
	RPFmt_Byte4 = VFmt_Byte4,
	RPFmt_Byte4_UInt = VFmt_Byte4_UInt,
	RPFmt_UInt = 100,		// one 32-bit unsigned int per texel / attribute
	RPFmt_Int,				// one 32-bit signed int
	RPFmt_UInt2,
	RPFmt_UInt4,
};

struct RenderProgramBinding
{
	enum Kind
	{
		UniformBlock,		// std140 block; per-draw data via FRenderState::SetProgramUniforms, or a buffer via SetProgramBuffer
		StorageBuffer,		// std430 buffer via SetProgramBuffer
		Sampler2D,			// SetProgramTexture / SetProgramRenderTargetTexture
		Sampler2DArray,		// SetProgramTexture with a texture created by CreateTextureArray
		USampler2D,			// integer textures (palette index layers)
		USampler2DArray,
		SamplerBuffer,		// texel buffers via SetProgramTexelBuffer
		USamplerBuffer,
		ISamplerBuffer,
		Image2D,			// compute output images (Vulkan + GL 4.3 only)
	};

	const char* name;		// GLSL identifier (block instance name for UniformBlock/StorageBuffer, sampler name otherwise)
	Kind kind;
	int binding;			// program-local binding index, 0..MaxProgramBindings-1
	bool vertex;
	bool fragment;
	bool compute;
	// UniformBlock / StorageBuffer only: block type name and member text generated with
	// ShaderInputsOutputs::GenerateStruct<T>() (hw_shaderpatcher.h) (the "{ ... }" part including braces).
	FString blockTypeName;
	FString blockBody;
	// StorageBuffer only: "readonly", "writeonly" or empty.
	FString qualifiers;
};

struct RenderProgramIO
{
	enum Position { VInput, VOutput, FOutput } position;
	UniformType type;
	FString name;
	int location = -1;
	FString qualifier;		// "flat", "noperspective" or empty (varyings only)
};

struct RenderProgramDesc
{
	FString name;				// debug label and binary cache key
	FString vertexSource;		// GLSL bodies WITHOUT #version or prelude
	FString fragmentSource;
	FString computeSource;
	FString defines;			// "#define X\n" lines, emitted right after the generated prelude
	TArray<RenderProgramBinding> bindings;
	// Vertex inputs (VInput; location = attribute location), varyings (VOutput; location assigned in
	// declaration order on Vulkan, ignored on GL) and fragment outputs (FOutput; location = draw buffer).
	TArray<RenderProgramIO> inputsOutputs;
	// Vertex fetch layout. Binding point 0 = per vertex, 1 = per instance when instanceDivisorBinding == 1.
	int numBindingPoints = 1;
	TArray<FVertexBufferAttribute> vertexAttributes;	// format may be any ERenderProgramFormat
	size_t vertexStride[2] = { 0, 0 };
	int instanceDivisorBinding = -1;	// -1 = no instancing
	bool isCompute = false;
	int computeLocalSize[3] = { 1, 1, 1 };
	// Colour attachments written by the fragment stage (1 = scene, OIT = 3).
	int numColorOutputs = 1;
	// Vulkan only (DFrameBuffer::RaytracingEnabled): the fragment stage traces against the light shadow acceleration structure
	// of the fixed descriptor set (TopLevelAS, set 0 binding 2, like the table shaders); the backend binds set 0 for the program.
	bool rayQuery = false;
};

class IRenderProgram
{
public:
	virtual ~IRenderProgram() = default;
	virtual bool IsValid() const = 0;
	virtual const RenderProgramDesc& Desc() const = 0;
};

enum ERenderTargetFormat
{
	RTF_RGBA8,
	RTF_RGBA16F,
	RTF_RG16F,
	RTF_R8,
	RTF_R32F,
	RTF_R32UI,
	RTF_RGBA32F,
};

struct RenderTargetDesc
{
	FString name;
	int width = 0, height = 0;		// 0 = scene size (recreated on resize by the backend)
	TArray<int> colorFormats;		// ERenderTargetFormat per colour attachment
	bool shareSceneDepth = true;	// attach the current scene depth/stencil buffer
	bool ownDepth = false;			// own depth texture (plane clip buffers)
};

// Offscreen colour attachments (OIT accumulation, plane/wall clip buffers).
class IRenderTarget
{
public:
	virtual ~IRenderTarget() = default;
	virtual int Width() const = 0;
	virtual int Height() const = 0;
	virtual const RenderTargetDesc& Desc() const = 0;
};

enum ERenderBlendFactor
{
	RBF_Zero,
	RBF_One,
	RBF_SrcColor,
	RBF_OneMinusSrcColor,
	RBF_SrcAlpha,
	RBF_OneMinusSrcAlpha,
	RBF_DstColor,
	RBF_OneMinusDstColor,
	RBF_DstAlpha,
	RBF_OneMinusDstAlpha,
};

enum ERenderBlendEquation
{
	RBE_Add,
	RBE_Subtract,
	RBE_ReverseSubtract,
	RBE_Min,
	RBE_Max,
};

struct FCustomBlendAttachment
{
	bool blend = true;
	int srcRGB = RBF_One, dstRGB = RBF_Zero, srcA = RBF_One, dstA = RBF_Zero;
	int equationRGB = RBE_Add, equationA = RBE_Add;
};

struct FCustomBlendState
{
	bool enabled = false;	// false = blend from SetRenderStyle as for table shaders
	bool blend = true;		// false = blending disabled (overwrite)
	int srcRGB = RBF_One, dstRGB = RBF_Zero, srcA = RBF_One, dstA = RBF_Zero;
	int equationRGB = RBE_Add, equationA = RBE_Add;
	// numAttachments > 0: attachment[i] overrides the state above for colour attachment i
	// (weighted blended OIT needs a different blend per attachment; needs GL 4.0 / Vulkan independentBlend).
	int numAttachments = 0;
	FCustomBlendAttachment attachment[3];
};

enum
{
	// The material programs of the world with the clustered lights need 25 (contract samplers texture2..12, ShadowMap,
	// LightMap, the Helion sector buffers, the light / cluster buffers). Storage buffers are numbered first (RenderProgram).
	MaxProgramBindings = 32,
	// OpenGL: uniform block / storage block binding point of program binding N is ProgramGLBufferBindingBase + N
	// (binding points below it belong to the table shaders).
	ProgramGLBufferBindingBase = 8,
};

// Current binding of one program slot, maintained by FRenderState and consumed by the backends.
struct FProgramBindingSlot
{
	enum Type { None, Buffer, Uniforms, Texture, RenderTargetTexture, SceneColorTexture, SceneDepthTexture, ShadowMapTexture, TexelBuffer };
	Type type = None;
	IBuffer* buffer = nullptr;
	size_t offset = 0, size = 0;
	IHardwareTexture* texture = nullptr;
	IRenderTarget* target = nullptr;
	int attachment = 0;
	int clampmode = 0;		// CLAMP_* (hw_material.h)
	int filter = 0;			// 0 = gl_texture_filter setting, 1 = nearest, 2 = linear
	int uniformsOffset = 0;	// into FRenderState::mProgramUniformData
	int uniformsSize = 0;
};

//==========================================================================
//
// PROTOTYPES
//
//==========================================================================

// Emits #version, extensions, the binding declarations, vertex inputs / varyings / fragment outputs
// and desc.defines for one stage. The caller appends the stage body.
FString GenerateProgramPrelude(const RenderProgramDesc& desc, bool isVulkan, bool isFrag, bool isCompute);
