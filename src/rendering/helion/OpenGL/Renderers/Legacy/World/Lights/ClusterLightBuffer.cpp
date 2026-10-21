/*
** ClusterLightBuffer.cpp
**
** UZDoom-only (plan M6.1): see ClusterLightBuffer.h.
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
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterLightBuffer.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterFragPrelude.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterLightList.h"
#include "OpenGL/Shader/RenderProgram.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "v_video.h"
#include "printf.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

namespace
{
constexpr int MaxLightsPerCluster = 128;
}

ClusterLightBuffer::ClusterLightBuffer() = default;

ClusterLightBuffer::~ClusterLightBuffer() = default;

uint32_t ClusterLightBuffer::CapacityVec4() const
{
	if (!m_storage)
		return (uint32_t)ClusterFragPrelude::UboLightCapacity();

	// About 64 vec4 (15 lights) per cluster on average, between 1 MB and 64 MB.
	uint64_t capacity = (uint64_t)m_grid.Count() * 64;
	return (uint32_t)std::min<uint64_t>(std::max<uint64_t>(capacity, 65536), 4u * 1024u * 1024u);
}

void ClusterLightBuffer::EnsureStorage(std::unique_ptr<IDataBuffer>& buffer, size_t& haveBytes, size_t bytes)
{
	bytes = std::max<size_t>(bytes, 16);
	if (!buffer)
		buffer.reset(screen->CreateDataBuffer(0, true, false));
	if (haveBytes < bytes)
	{
		// The compute shader fills it; the old storage stays alive for draws that reference it.
		buffer->SetData(bytes, nullptr, BufferUsageType::Static);
		haveBytes = bytes;
	}
}

bool ClusterLightBuffer::Build(FLevelLocals* level, const ViewParams& view)
{
	m_valid = false;
	m_lights.clear();

	if (view.Width <= 0 || view.Height <= 0 || view.ViewMatrix == nullptr || view.ProjectionMatrix == nullptr)
		return false;
	// column 3 row 3 of a perspective projection is -1, of an orthographic one 0
	if (view.ProjectionMatrix[11] == 0.0f)
		return false;

	ClusterLightList::Build(level, view.PortalGroup, m_lights);
	m_grid.Setup(view.ViewportX, view.ViewportY, view.Width, view.Height, view.ProjectionMatrix, view.NearZ, view.FarZ);
	if (m_lights.empty())
		return false;

	m_storage = ClusterFragPrelude::UsesStorageBuffers();
	ClusterAssignProgram* program = m_storage ? ClusterAssignProgram::Shared() : nullptr;
	m_gpu = program != nullptr;
	m_capacity = CapacityVec4();

	const int lightCount = (int)m_lights.size();
	if (m_gpu)
	{
		if (!m_lightsIn)
			m_lightsIn.reset(screen->CreateDataBuffer(0, true, false));
		m_lightsIn->SetData((size_t)lightCount * sizeof(ClusterLight), m_lights.data(), BufferUsageType::Stream);
		m_lightsInBytes = (size_t)lightCount * sizeof(ClusterLight);

		EnsureStorage(m_counts, m_countsBytes, (size_t)m_grid.Count() * sizeof(uint32_t));
		EnsureStorage(m_offsets, m_offsetsBytes, (size_t)m_grid.Count() * sizeof(int32_t));
		EnsureStorage(m_lightsOut, m_lightsOutBytes, (size_t)m_capacity * 16);

		program->Dispatch(m_grid, view.ViewMatrix, lightCount, MaxLightsPerCluster, m_capacity,
			m_lightsIn.get(), m_counts.get(), m_offsets.get(), m_lightsOut.get());
		m_valid = true;
		return true;
	}

	ClusterAssignOptions options;
	options.MaxPerCluster = MaxLightsPerCluster;
	options.CapacityVec4 = m_capacity;
	options.Dedupe = !m_storage;
	ClusterAssignCpu::Assign(m_grid, view.ViewMatrix, m_lights.data(), lightCount, options, m_cpuResult);

	if (m_cpuResult.OverflowClusters > 0 && !m_warnedOverflow)
	{
		m_warnedOverflow = true;
		Printf(PRINT_HIGH, "Dynamic lights: more than %d lights touch some screen clusters, the farthest ones are dropped there.\n", MaxLightsPerCluster);
	}
	if (m_cpuResult.DroppedClusters > 0 && !m_warnedCapacity)
	{
		m_warnedCapacity = true;
		Printf(PRINT_HIGH, "Dynamic lights: the light buffer (%u vec4) is full, %u clusters get no dynamic lights.\n", m_capacity, m_cpuResult.DroppedClusters);
	}

	const size_t offsetBytes = (size_t)m_grid.Count() * sizeof(int32_t);
	if (m_storage)
	{
		if (!m_offsets)
			m_offsets.reset(screen->CreateDataBuffer(0, true, false));
		m_offsets->SetData(offsetBytes, m_cpuResult.Offsets.data(), BufferUsageType::Stream);

		if (!m_lightsOut)
			m_lightsOut.reset(screen->CreateDataBuffer(0, true, false));
		static const float zero[4] = {};
		size_t bytes = (size_t)m_cpuResult.UsedVec4 * 16;
		m_lightsOut->SetData(std::max<size_t>(bytes, 16), bytes ? m_cpuResult.Lights.data() : zero, BufferUsageType::Stream);
	}
	else
	{
		if (!m_offsets)
			m_offsets.reset(screen->CreateTexelBuffer(RPFmt_Int));
		m_offsets->SetData(offsetBytes, m_cpuResult.Offsets.data(), BufferUsageType::Stream);

		// lights[] is a uniform block of fixed size: the whole block is bound, so the buffer has to be that large.
		if (!m_lightsOut)
			m_lightsOut.reset(screen->CreateDataBuffer(0, false, false));
		m_uboScratch.assign((size_t)m_capacity * 4, 0.0f);
		std::copy(m_cpuResult.Lights.begin(), m_cpuResult.Lights.end(), m_uboScratch.begin());
		m_lightsOut->SetData(m_uboScratch.size() * sizeof(float), m_uboScratch.data(), BufferUsageType::Stream);
	}
	m_valid = true;
	return true;
}

void ClusterLightBuffer::Bind(::Helion::Render::OpenGL::Shader::RenderProgram& program) const
{
	ClusterParamsUniform params = m_grid.FragmentParams(m_valid);
	program.Uniforms.SetBlock(ClusterFragPrelude::ParamsBlock, &params, sizeof(params));

	if (!m_valid)
		return;

	auto& state = *screen->RenderState();
	if (m_storage)
	{
		int lightsBinding = program.GetBinding(ClusterFragPrelude::LightBufferStorage);
		if (lightsBinding >= 0)
			state.SetProgramBuffer(lightsBinding, m_lightsOut.get(), 0, 0);
		int offsetsBinding = program.GetBinding(ClusterFragPrelude::OffsetBufferStorage);
		if (offsetsBinding >= 0)
			state.SetProgramBuffer(offsetsBinding, m_offsets.get(), 0, 0);
	}
	else
	{
		int lightsBinding = program.GetBinding(ClusterFragPrelude::LightBufferUniform);
		if (lightsBinding >= 0)
			state.SetProgramBuffer(lightsBinding, m_lightsOut.get(), 0, (size_t)m_capacity * 16);
		int offsetsBinding = program.GetBinding(ClusterFragPrelude::OffsetTexture);
		if (offsetsBinding >= 0)
			state.SetProgramTexelBuffer(offsetsBinding, m_offsets.get());
	}
}

}
