/*
** ClusterLightBuffer.h
**
** UZDoom-only (plan M6.1): owns the buffers of the clustered dynamic lights and builds them once per view:
**
**   Build(level, view)  collects the lights (ClusterLightList), sets up the ClusterGrid, assigns lights to clusters
**                       (compute shader ClusterAssignProgram when the backend has compute shaders, ClusterAssignCpu
**                       otherwise) and leaves the
**                       result in the lights[] / cluster offset buffers
**   Bind(program)       after RenderProgram::Bind(): sets the ClusterParams block and binds the buffers of the
**                       program's lights[] / offsets declarations (ClusterFragPrelude)
**
** The environment variable is read once in the constructor. It is temporary (plan M6.4) and is removed after the
** CPU/GPU comparison captures.
**
** Every view of a frame (main view, each portal view, camera textures) calls Build for its own viewpoint. The CPU path
** re-uploads its buffers with BufferUsageType::Stream (new storage per view, so draws recorded for earlier views keep
** the data they were recorded with); the compute path reuses its buffers with a barrier before and after the dispatches.
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

#include <memory>
#include <vector>
#include "hwrenderer/data/buffers.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterAssignCpu.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterAssignProgram.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterGrid.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterLightList.h"

struct FLevelLocals;

namespace Helion::Render::OpenGL::Shader
{
class RenderProgram;
}

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

class ClusterLightBuffer
{
public:
	struct ViewParams
	{
		// The scene viewport in gl_FragCoord space.
		int ViewportX = 0, ViewportY = 0, Width = 0, Height = 0;
		// Column-major HWViewpointUniforms::ViewMatrix / ProjectionMatrix of the view (VSMatrix::get()).
		const float* ViewMatrix = nullptr;
		const float* ProjectionMatrix = nullptr;
		float NearZ = 5.0f;
		// Frustum far plane, or the render distance (MaxDistance / r_distance_cull_type / culldist) when that is smaller.
		float FarZ = 65536.0f;
		// sector->PortalGroup of the view's sector (positions of lights across portals are made relative to it).
		int PortalGroup = 0;
	};

	ClusterLightBuffer();
	~ClusterLightBuffer();

	ClusterLightBuffer(const ClusterLightBuffer&) = delete;
	ClusterLightBuffer& operator=(const ClusterLightBuffer&) = delete;

	// Returns true when the view has dynamic lights; false leaves Bind to only disable the lookup (uClState.x = 0).
	// Orthographic projections are not clustered (the grid and the fragment lookup assume a perspective depth): such
	// views get no dynamic lights.
	bool Build(FLevelLocals* level, const ViewParams& view);

	// Bind gives the programs no lights until the next Build.
	void Clear() { m_valid = false; }

	void Bind(::Helion::Render::OpenGL::Shader::RenderProgram& program) const;

private:
	static void EnsureStorage(std::unique_ptr<IDataBuffer>& buffer, size_t& haveBytes, size_t bytes);
	uint32_t CapacityVec4() const;

	bool m_gpu = false;
	bool m_valid = false;
	bool m_storage = false;
	bool m_warnedOverflow = false;
	bool m_warnedCapacity = false;
	uint32_t m_capacity = 0;

	ClusterGrid m_grid;
	std::vector<ClusterLight> m_lights;
	ClusterAssignResult m_cpuResult;
	std::vector<float> m_uboScratch;

	std::unique_ptr<IDataBuffer> m_lightsIn;	// compute: ClusterLight records
	std::unique_ptr<IDataBuffer> m_counts;		// compute: scratch
	std::unique_ptr<IDataBuffer> m_offsets;		// storage mode: int clusterOffsets[]; uniform mode: texel buffer
	std::unique_ptr<IDataBuffer> m_lightsOut;	// lights[]
	size_t m_lightsInBytes = 0, m_countsBytes = 0, m_offsetsBytes = 0, m_lightsOutBytes = 0;
};

}
