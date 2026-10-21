/*
** Renderer.World.cpp
**
** Port of Helion.Render.Renderer (Renderer.World.cs): light buffer indices.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The sector light / colormap / fog texel buffers of Renderer.World.cs are SectorLightBuffers
** (Data/SectorLightBuffers.h); the map data / line heights buffers of the vanilla sprite clipping are here.
** Sector.Id is sector_t::Index(); Sector.TransferFloorLightSector / TransferCeilingLightSector are the
** sector passed in: callers pass the light sector the geometry already resolved (hw_fakeflat rules).
** Helion's Wall (Upper/Middle/Lower) is the side_t tier (side_t::top/mid/bottom).
*/

#include "Renderer.h"
#include "BindTextures.h"
#include "HelionCVars.h"
#include "RenderBlock.h"
#include "Util/Constants.h"
#include "World/Geometry/Sectors/RenderSector.h"
#include "g_levellocals.h"
#include "r_defs.h"

namespace Helion::Render
{

using Helion::Util::Constants::LightBuffer;
using OpenGL::Renderers::Legacy::World::Geometry::Static::LightBufferType;

int Renderer::GetLightBufferIndex(const side_t* side, int tier, const sector_t* sector, int& overrideLightIndex)
{
	// The shader will add the light level at this index plus the vertex light level.
	// Return LightBuffer.DarkIndex (lightlevel=0) to not add the sectors light level if absolute.
	int tierFlag = WALLF_ABSLIGHTING_TIER << (tier == side_t::top ? 0 : tier == side_t::mid ? 1 : 2);
	overrideLightIndex = (side->Flags & (WALLF_ABSLIGHTING | tierFlag)) ? LightBuffer::DarkIndex + 1 : 0;
	return GetLightBufferIndex(sector, LightBufferType::Wall);
}

int Renderer::GetLightBufferIndex(const sector_t* sector, int planePos, LightBufferType type, int& overrideLightIndex)
{
	overrideLightIndex = (sector->planes[planePos].Flags & PLANEF_ABSLIGHTING) ? LightBuffer::DarkIndex + 1 : 0;
	return GetLightBufferIndex(sector, type);
}

int Renderer::GetLightBufferIndex(const sector_t* sector, LightBufferType type)
{
	int id = sector->Index();
	switch (type)
	{
	case LightBufferType::Floor: return id * LightBuffer::BufferSize + LightBuffer::SectorIndexStart + LightBuffer::FloorOffset;
	case LightBufferType::Ceiling: return id * LightBuffer::BufferSize + LightBuffer::SectorIndexStart + LightBuffer::CeilingOffset;
	case LightBufferType::Wall: return id * LightBuffer::BufferSize + LightBuffer::SectorIndexStart + LightBuffer::WallOffset;
	}
	return id * LightBuffer::BufferSize + LightBuffer::SectorIndexStart;
}

// Renderer.World.cs BlockSide
enum class BlockSide
{
	Front = 1,
	Back = 2,
	Both = 3
};

void Renderer::SetMapDataBuffer(FLevelLocals* world)
{
	if (!gl_vanillarender)
	{
		m_mapDataBuffer.reset();
		return;
	}

	constexpr int Components = OpenGL::Textures::GLBufferTextureStorage<float>::FourComponentLength;
	TArray<float> data(world->lines.Size() * Components, true);
	for (unsigned i = 0; i < world->lines.Size(); i++)
	{
		const line_t& line = world->lines[i];
		int index = i * Components;
		data[index] = (float)line.v1->fX();
		data[index + 1] = (float)line.v1->fY();
		data[index + 2] = (float)line.v2->fX();
		data[index + 3] = (float)line.v2->fY();
	}
	m_mapDataBuffer = std::make_unique<OpenGL::Textures::GLBufferTextureStorage<float>>("Map data buffer", std::move(data), OpenGL::Textures::SizedInternalFormat::Rgba32f, false);
}

void Renderer::SetLineHeights(FLevelLocals* world)
{
	m_updateLineHeights.ClearAndReset();
	if (!gl_vanillarender)
	{
		m_lineHeightsBuffer.reset();
		return;
	}

	m_updateLineHeights.EnsureCapacity(world->sectors.Size());
	constexpr int Components = OpenGL::Textures::GLBufferTextureStorage<float>::FourComponentLength;
	TArray<float> data(world->lines.Size() * Components, true);
	for (unsigned i = 0; i < world->lines.Size(); i++)
		SetLineHeightBuffer(data.Data(), &world->lines[i]);
	m_lineHeightsBuffer = std::make_unique<OpenGL::Textures::GLBufferTextureStorage<float>>("Line heights data buffer", std::move(data), OpenGL::Textures::SizedInternalFormat::Rgba32f, false);
}

// UZDoom: the floor height of a side at the line (the higher end point of a sloped floor) and the one of the previous tick (the
// plane interpolation, Helion's SectorPlane.PrevZ).
static void GetLineFloorZ(sector_t* sector, const line_t* line, float& prevZ, float& z)
{
	const secplane_t& plane = sector->floorplane;
	double zv = std::max(plane.ZatPoint(line->v1), plane.ZatPoint(line->v2));
	double texZ = sector->GetPlaneTexZ(sector_t::floor);
	double prevTexZ = ::Helion::World::Geometry::Sectors::PrevPlaneTexZ(sector, sector_t::floor, texZ);
	z = (float)zv;
	prevZ = (float)(zv + prevTexZ - texZ);
}

void Renderer::SetLineHeightBuffer(float* buffer, line_t* line)
{
	float prevFloorZ, floorZ;
	GetLineFloorZ(line->frontsector, line, prevFloorZ, floorZ);
	if (line->backsector != nullptr)
	{
		float backPrevZ, backZ;
		GetLineFloorZ(line->backsector, line, backPrevZ, backZ);
		prevFloorZ = std::max(prevFloorZ, backPrevZ);
		floorZ = std::max(floorZ, backZ);
	}

	int index = line->Index() * OpenGL::Textures::GLBufferTextureStorage<float>::FourComponentLength;
	buffer[index] = prevFloorZ;
	buffer[index + 1] = floorZ;
	buffer[index + 3] = 0;

	// CoverWallUtil.GetProjectHeights forces top and bottom projection to cover. Ensure it's set to blocked for the shader.
	if (IsBlocked(line))
	{
		buffer[index + 2] = (float)BlockSide::Both;
		return;
	}

	int blockSide = 0;
	if (line->sidedef[0]->GetTexture(side_t::mid).isValid())
		blockSide |= (int)BlockSide::Front;
	if (line->sidedef[1] != nullptr && line->sidedef[1]->GetTexture(side_t::mid).isValid())
		blockSide |= (int)BlockSide::Back;

	if (line->backsector != nullptr)
	{
		float backPrevZ, backZ, frontPrevZ, frontZ;
		GetLineFloorZ(line->frontsector, line, frontPrevZ, frontZ);
		GetLineFloorZ(line->backsector, line, backPrevZ, backZ);
		if (backZ < frontZ)
			blockSide |= (int)BlockSide::Front;
		else if (frontZ < backZ)
			blockSide |= (int)BlockSide::Back;
	}

	buffer[index + 2] = (float)blockSide;
}

bool Renderer::IsBlocked(line_t* line)
{
	if (line->backsector == nullptr || line->sidedef[1] == nullptr)
		return true;

	return RenderBlock::IsBlocked(line->sidedef[0], line->frontsector, line->backsector) ||
		RenderBlock::IsBlocked(line->sidedef[1], line->backsector, line->frontsector);
}

// UZDoom: Helion raises SectorMove every tick of a move; here the sectors with a moving floor are picked up every frame and the
// ones that finished come from SectorMoveCompleteEvent.
void Renderer::UpdateLineHeights()
{
	if (m_lineHeightsBuffer == nullptr || m_world == nullptr)
		return;

	for (sector_t& sector : m_world->sectors)
	{
		if (sector.renderDynamic[sector_t::floor] & SD_Movement)
			m_updateLineHeights.Add(&sector);
	}

	if (m_updateLineHeights.UpdateSectors.Size() == 0)
		return;

	auto mapped = m_lineHeightsBuffer->GetMappedBufferAndBind();
	float* buffer = mapped.MappedMemoryPtr;
	for (sector_t* sector : m_updateLineHeights.UpdateSectors)
	{
		for (line_t* line : sector->Lines)
			SetLineHeightBuffer(buffer, line);
	}
	m_lineHeightsBuffer->Unbind();
	m_updateLineHeights.Clear();
}

void Renderer::BindMapDataBuffer()
{
	if (m_mapDataBuffer)
		m_mapDataBuffer->BindTexture(BindTextures::MapLineData);
}

void Renderer::BindLineHeightsBuffer()
{
	if (m_lineHeightsBuffer)
		m_lineHeightsBuffer->BindTexture(BindTextures::LineHeights);
}

}
