/*
** ClusterAssignProgram.h
**
** UZDoom-only (plan M6.1): the compute program of the clustered dynamic lights (lightcluster.comp, the GLSL text lives
** in the class like the text of every Helion program). One program, three dispatches per view:
**
**   pass 0  count: one invocation per cluster tests every light against the cluster box and stores the size in vec4
**           of the cluster's record (0 = no light)
**   pass 1  scan:  one workgroup turns the sizes into offsets (exclusive prefix sum, -1 for empty or not fitting)
**   pass 2  write: one invocation per cluster writes header + light records at its offset
**
** ClusterAssignCpu is the C++ twin of this shader and produces the same buffers.
**
** RenderProgram (OpenGL/Shader/RenderProgram.h) only knows vertex/fragment programs, so this class builds its
** RenderProgramDesc directly (isCompute, bindings listed explicitly) and creates the program through
** DFrameBuffer::CreateRenderProgram, exactly the call RenderProgram makes.
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

#pragma once

#include <cstdint>
#include <memory>
#include "zstring.h"
#include "hwrenderer/data/hw_renderprogram.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterGrid.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

// std140 block ClusterAssignParams.
struct ClusterAssignUniforms
{
	float View[16];
	float Proj[4];			// sx, sy, ox, oy
	float Range[4];			// near, far, slice scale, slice bias
	float Viewport[4];
	int32_t Grid[4];		// CountX, CountY, slices, tile pixels
	int32_t Limits[4];		// max lights per cluster, light count, capacity (vec4), pass
};
static_assert(sizeof(ClusterAssignUniforms) == 144, "ClusterAssignUniforms is a std140 block");

class ClusterAssignProgram
{
public:
	// Program binding slots (RenderProgramBinding::binding).
	enum Binding
	{
		BindParams = 0,		// uniform block ClusterAssignParams (FRenderState::SetProgramUniforms)
		BindLightsIn = 1,	// readonly buffer ClusterLightInputBuffer: vec4 uClLights[] (4 per light, ClusterLight layout)
		BindCounts = 2,		// buffer ClusterCountBuffer: uint uClCounts[] (per cluster, scratch)
		BindOffsets = 3,	// buffer ClusterOffsetBuffer: int clusterOffsets[] (output)
		BindLightsOut = 4,	// writeonly buffer LightBufferSSO: vec4 lights[] (output)
	};

	// The program of the renderer, compiled at startup with the other render programs (HelionProgramRegistry::CompileNext,
	// plan D22) when the backend has compute shaders and storage buffers (DFrameBuffer::SupportsCompute,
	// ClusterFragPrelude::UsesStorageBuffers). nullptr otherwise: the lights are assigned by ClusterAssignCpu.
	static ClusterAssignProgram* Shared() { return s_shared.get(); }
	static void CreateShared();
	static void ReleaseShared() { s_shared.reset(); }

	ClusterAssignProgram(const ClusterAssignProgram&) = delete;
	ClusterAssignProgram& operator=(const ClusterAssignProgram&) = delete;
	~ClusterAssignProgram();

	// The GLSL compute body (without #version / prelude) and the program description, for tools that only want the text.
	static FString ComputeShader();
	static RenderProgramDesc MakeDesc();

	IRenderProgram* Handle() const { return m_program.get(); }

	// Assigns lightCount lights (already uploaded to lightsIn as ClusterLight records) to the clusters of the grid.
	// The three buffers must be storage buffers (DFrameBuffer::CreateDataBuffer(.., true, ..)): counts holds at least
	// grid.Count() uints, offsets at least grid.Count() ints, lightsOut capacityVec4 vec4. Leaves the render state in
	// table shader mode (SetProgram(nullptr)): call it before the world programs are bound for the view.
	void Dispatch(const ClusterGrid& grid, const float* view, int lightCount, int maxPerCluster, uint32_t capacityVec4,
		IBuffer* lightsIn, IBuffer* counts, IBuffer* offsets, IBuffer* lightsOut);

private:
	ClusterAssignProgram() = default;

	std::unique_ptr<IRenderProgram> m_program;

	inline static std::unique_ptr<ClusterAssignProgram> s_shared;
};

}
