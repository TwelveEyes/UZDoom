/*
** RenderProgram.h
**
** Port of Helion.Render.OpenGL.Shader.RenderProgram.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion's RenderProgram compiles GLSL text and introspects the linked program.
** Here the constructor path builds a RenderProgramDesc (hw_renderprogram.h) from
** the same GLSL text and creates the program through DFrameBuffer::CreateRenderProgram;
** the interface (uniforms, samplers, blocks, vertex inputs, varyings, fragment
** outputs) is taken from the declarations in the stage sources (ProgramSource.h),
** which are then removed from the source because GenerateProgramPrelude emits them.
**
** C# calls the virtual VertexShader()/FragmentShader() from the base constructor.
** C++ cannot, so programs are created with RenderProgram::Create<T>(args...), which
** constructs the object and then runs Initialize().
**
** Helion's GeometryShader() hook is not ported: no Helion program overrides it and the
** render program API has no geometry stage.
**
*/

#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include "tarray.h"
#include "zstring.h"
#include "hwrenderer/data/hw_renderprogram.h"
#include "ProgramUniforms.h"
#include "ProgramAttributes.h"
#include "OpenGL/Vertex/VaoAttribute.h"

class FRenderState;

namespace Helion::Render::OpenGL::Shader
{

class RenderProgram
{
public:
	const FString Label;
	ProgramUniforms Uniforms;
	ProgramAttributes Attributes;

	// UZDoom (plan M5.2, portal views): the clip plane of the view (classic SetClipLine / SetClipHeight). Programs that
	// declare `uniform vec4 helionClipPlane` write gl_ClipDistance[0] = dot(helionClipPlane, vec4(world position, 1)); FlushState
	// sends the current plane and enables the GL clip distance for them while a plane is active. (0, 0, 0, 1) = no clipping.
	static constexpr const char* ClipPlaneUniform = "helionClipPlane";
	static void SetClipPlane(const FVector4& plane)
	{
		s_clipPlane = plane;
		s_clipPlaneActive = plane.X != 0 || plane.Y != 0 || plane.Z != 0;
	}
	static const FVector4& ClipPlane() { return s_clipPlane; }

	template<typename T, typename... Args>
	static std::unique_ptr<T> Create(Args&&... args)
	{
		std::unique_ptr<T> program = std::make_unique<T>(std::forward<Args>(args)...);
		program->Initialize();
		return program;
	}

	virtual ~RenderProgram();

	RenderProgram(const RenderProgram&) = delete;
	RenderProgram& operator=(const RenderProgram&) = delete;

	// Makes this the program of subsequent draws (FRenderState::SetProgram) and the target of the static
	// ProgramUniforms::Set functions. Everything is re-sent to the backend before the next draw.
	void Bind();
	// Back to the table shaders (FRenderState::SetProgram(nullptr), which also drops program bindings and custom blend).
	void Unbind();

	// Sends the dirty uniform blocks and the changed texture unit bindings to the render state. Called before every draw
	// that goes through VertexPipeline / VertexBufferObject; call it manually before raw FRenderState draws.
	void FlushState();
	static void FlushCurrent();
	static RenderProgram* Current() { return s_current; }

	IRenderProgram* Handle() const { return m_program.get(); }
	bool IsValid() const { return m_program != nullptr && m_program->IsValid(); }
	const RenderProgramDesc& Desc() const { return m_desc; }

	// Binding index of a block (type or instance name) or sampler, for FRenderState::SetProgramBuffer / SetProgramTexelBuffer; -1 if absent.
	int GetBinding(const FString& name) const;

protected:
	explicit RenderProgram(const FString& label);

	virtual FString VertexShader() = 0;
	virtual std::optional<FString> FragmentShader() { return std::nullopt; }

	// The attribute table of the vertex struct the program is fed with; nullptr for programs without vertex inputs.
	virtual const TArray<Vertex::VaoAttribute>* VertexLayout() const { return nullptr; }

	// Called at the end of Initialize, once Uniforms/Attributes are populated (Helion: the rest of the base constructor body).
	virtual void ProgramCreated() { }
	// UZDoom-only: called at the end of Bind, for the per view state every draw of the program needs (dynamic lights).
	virtual void OnBound() { }

	// Creates the backend program (DFrameBuffer::CreateRenderProgram). A seam for tools that only want the generated
	// RenderProgramDesc / GLSL.
	virtual IRenderProgram* CreateBackendProgram(const RenderProgramDesc& desc);

private:
	std::unique_ptr<IRenderProgram> m_program;
	RenderProgramDesc m_desc;
	std::deque<FString> m_names;	// owns the const char* names of m_desc.bindings
	TArray<unsigned> m_samplerVersions;
	int m_clipPlaneLocation = -1;

	inline static RenderProgram* s_current = nullptr;
	inline static FVector4 s_clipPlane = FVector4(0, 0, 0, 1);
	inline static bool s_clipPlaneActive = false;
	inline static bool s_clipDistanceEnabled = false;

	void Initialize();
	const char* StoreName(const FString& name);
	static void SetClipDistanceEnabled(FRenderState& state, bool enabled);
};

}
