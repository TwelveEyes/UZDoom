/*
** FloodFillRenderer.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Portals.FloodFill.FloodFillRenderer.
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

#include "FloodFillRenderer.h"

#include <algorithm>
#include <cassert>
#include <cfloat>
#include "g_levellocals.h"
#include "r_sky.h"
#include "r_defs.h"
#include "textureid.h"
#include "BindTextures.h"
#include "Renderer.h"
#include "OpenGL/Renderers/Legacy/World/VertexOptions.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Static/LightBufferType.h"
#include "OpenGL/Shader/ProgramRegistry.h"
#include "Util/Constants.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals::FloodFill
{

using ::Helion::World::Geometry::Sectors::SectorPlaneFace;
using ::Helion::World::Static::SideTexture;
using Shared::World::WallVertices;
using Static::LightBufferType;
using Texture::Legacy::GLLegacyTexture;

// Helion Constants.MaxTextureHeight
static constexpr int MaxTextureHeight = 16384;

FloodFillRenderer::FloodFillRenderer(Texture::Legacy::LegacyGLTextureManager& glTextureManager, FloodFillRenderMode renderMode)
	: m_glTextureManager(glTextureManager), m_renderMode(renderMode)
{
}

FloodFillRenderer::~FloodFillRenderer()
{
	ClearData();
}

FloodFillProgram* FloodFillRenderer::GetProgram()
{
	return ::Helion::Render::OpenGL::Shader::HelionProgramRegistry::Get<FloodFillProgram>(::Helion::Render::OpenGL::Shader::ProgramType::FloodFill);
}

FloodFillWallClipProgram* FloodFillRenderer::GetWallClipProgram()
{
	return ::Helion::Render::OpenGL::Shader::HelionProgramRegistry::Get<FloodFillWallClipProgram>(::Helion::Render::OpenGL::Shader::ProgramType::FloodFillWallClip);
}

void FloodFillRenderer::UpdateTo(FLevelLocals* world)
{
	m_world = world;
	ClearData();
}

bool FloodFillRenderer::IsSkyTexture(int textureHandle) const
{
	return textureHandle == skyflatnum.GetIndex();
}

FloodFillInfo& FloodFillRenderer::CreateFloodFillInfo(const RenderPlane& plane)
{
	FString label = GetLabel(plane);
	Texture::Legacy::GLLegacyTexture* texture = m_glTextureManager.GetParentArrayTexture(plane.TextureHandle);
	m_floodFillInfos.push_back(std::make_unique<FloodFillInfo>(plane.TextureHandle, texture, plane.Z, *GetProgram(), label));
	return *m_floodFillInfos.back();
}

FloodFillInfo& FloodFillRenderer::GetOrCreateFloodFillInfo(const RenderPlane& plane, int arrayTextureHandle)
{
	auto it = m_textureHandleToFloodFillInfoIndex.find(arrayTextureHandle);
	if (it != m_textureHandleToFloodFillInfoIndex.end())
		return *m_floodFillInfos[(size_t)it->second];

	m_textureHandleToFloodFillInfoIndex[arrayTextureHandle] = (int)m_floodFillInfos.size();
	return CreateFloodFillInfo(plane);
}

FloodGeometry* FloodFillRenderer::TryGetFloodGeometry(int floodKey)
{
	floodKey--;
	if (floodKey < 0 || floodKey >= (int)m_floodGeometry.size())
		return nullptr;

	return &m_floodGeometry[(size_t)floodKey];
}

bool FloodFillRenderer::TryGetFloodFillInfoIndex(int textureHandle, int& index)
{
	textureHandle = m_glTextureManager.GetWorldArrayTextureHandle(textureHandle, true);
	auto it = m_textureHandleToFloodFillInfoIndex.find(textureHandle);
	bool success = it != m_textureHandleToFloodFillInfoIndex.end();
	assert(success && "Failed to find flood fill info");
	if (success)
		index = it->second;
	return success;
}

void FloodFillRenderer::UpdateStaticWall(int floodKey, const RenderPlane& floodPlane, const WallVertices& vertices, double minPlaneZ, double maxPlaneZ,
	SideTexture sideTexture, int mapId, bool isFloodFillPlane)
{
	if (m_renderMode == FloodFillRenderMode::Dynamic)
	{
		AddStaticWall(floodPlane, vertices, minPlaneZ, maxPlaneZ, sideTexture, mapId, isFloodFillPlane);
		return;
	}

	FloodGeometry* dataPtr = TryGetFloodGeometry(floodKey);
	if (dataPtr == nullptr)
	{
		AddStaticWall(floodPlane, vertices, minPlaneZ, maxPlaneZ, sideTexture, mapId, isFloodFillPlane);
		return;
	}

	FloodGeometry& data = *dataPtr;
	int index;
	if (!TryGetFloodFillInfoIndex(data.TextureHandle, index))
		return;

	FloodFillInfo& floodInfo = *m_floodFillInfos[(size_t)index];
	float minZ = (float)minPlaneZ;
	float maxZ = (float)maxPlaneZ;
	float planeZ = (float)floodPlane.Z;
	float prevPlaneZ = (float)floodPlane.PrevZ;

	float topZ = vertices.TopLeft.Z;
	float bottomZ = vertices.BottomRight.Z;
	float prevTopZ = vertices.PrevTopZ;
	float prevBottomZ = vertices.PrevBottomZ;

	int upper = sideTexture == SideTexture::Upper ? 1 : 0;
	int lower = sideTexture == SideTexture::Lower ? 1 : 0;
	int textureIndex = m_glTextureManager.GetTexture(FSetTextureID(floodPlane.TextureHandle))->ArrayIndex;
	float options = VertexOptions::PackSurface(0, 0, 0, upper, lower, data.OverrideLightIndex, textureIndex);

	FloodFillVertex topLeft(::Helion::Geometry::Vec3F(vertices.TopLeft.X, vertices.TopLeft.Y, topZ),
		prevTopZ, planeZ, prevPlaneZ, minZ, maxZ, options, data.RenderOptions, mapId);
	FloodFillVertex topRight(::Helion::Geometry::Vec3F(vertices.BottomRight.X, vertices.BottomRight.Y, topZ),
		prevTopZ, planeZ, prevPlaneZ, minZ, maxZ, options, data.RenderOptions, mapId);
	FloodFillVertex bottomLeft(::Helion::Geometry::Vec3F(vertices.TopLeft.X, vertices.TopLeft.Y, bottomZ),
		prevBottomZ, planeZ, prevPlaneZ, minZ, maxZ, options, data.RenderOptions, mapId);
	FloodFillVertex bottomRight(::Helion::Geometry::Vec3F(vertices.BottomRight.X, vertices.BottomRight.Y, bottomZ),
		prevBottomZ, planeZ, prevPlaneZ, minZ, maxZ, options, data.RenderOptions, mapId);

	auto& vbo = *floodInfo.Vbo;
	vbo.Data[(unsigned)data.VboOffset] = topLeft;
	vbo.Data[(unsigned)data.VboOffset + 1] = bottomLeft;
	vbo.Data[(unsigned)data.VboOffset + 2] = topRight;
	vbo.Data[(unsigned)data.VboOffset + 3] = topRight;
	vbo.Data[(unsigned)data.VboOffset + 4] = bottomLeft;
	vbo.Data[(unsigned)data.VboOffset + 5] = bottomRight;

	if (isFloodFillPlane)
		ProjectFloodPlane(vbo, data.VboOffset + VerticesPerWall, vertices, minZ, maxZ, planeZ, prevPlaneZ, data.OverrideLightIndex,
			maxPlaneZ > MaxTextureHeight ? -MaxTextureHeight : MaxTextureHeight, false, data.RenderOptions, mapId, textureIndex);

	vbo.UploadSubData(data.VboOffset, data.Vertices);
}

int FloodFillRenderer::AddStaticWall(const RenderPlane& sectorPlane, const WallVertices& vertices, double minPlaneZ, double maxPlaneZ,
	SideTexture sideTexture, int mapId, bool isFloodFillPlane)
{
	if (IsSkyTexture(sectorPlane.TextureHandle))
		return 0;

	int vertexCount = isFloodFillPlane ? (FloodPlaneAddCount + 1) * VerticesPerWall : VerticesPerWall;
	float minZ = (float)minPlaneZ;
	float maxZ = (float)maxPlaneZ;
	float planeZ = (float)sectorPlane.Z;
	float prevPlaneZ = (float)sectorPlane.PrevZ;

	int arrayTextureHandle = m_glTextureManager.GetWorldArrayTextureHandle(sectorPlane.TextureHandle, true);
	FloodFillInfo& floodFillInfo = GetOrCreateFloodFillInfo(sectorPlane, arrayTextureHandle);

	int lightIndex;
	int overrideLightIndex;
	if (sectorPlane.Facing == SectorPlaneFace::Floor)
		lightIndex = ::Helion::Render::Renderer::GetLightBufferIndex(sectorPlane.Sector, (int)SectorPlaneFace::Floor, LightBufferType::Floor, overrideLightIndex);
	else
		lightIndex = ::Helion::Render::Renderer::GetLightBufferIndex(sectorPlane.Sector, (int)SectorPlaneFace::Ceiling, LightBufferType::Ceiling, overrideLightIndex);

	// UZDoom: signed, relative plane light can be negative (VertexOptions::PackRender).
	int flatLightLevel = std::clamp(sectorPlane.LightLevelAdd, -256, 255);
	float lightBufferIndex = VertexOptions::PackRender(lightIndex, flatLightLevel);

	for (auto node = m_freeData.begin(); node != m_freeData.end(); ++node)
	{
		FloodGeometry& data = *node;
		if (data.ArrayTextureHandle != arrayTextureHandle || data.Vertices != vertexCount)
			continue;

		FloodGeometry reused(data.Key, data.TextureHandle, arrayTextureHandle, overrideLightIndex, lightBufferIndex, data.VboOffset, data.Vertices);
		m_freeData.erase(node);

		m_floodGeometry[(size_t)reused.Key - 1] = reused;
		UpdateStaticWall(reused.Key, sectorPlane, vertices, minPlaneZ, maxPlaneZ, sideTexture, mapId);
		return reused.Key;
	}

	// Zero means "no handle" which the callers use to tell they don't have a handle.
	int newKey = (int)m_floodGeometry.size() + 1;
	auto& vbo = *floodFillInfo.Vbo;

	m_floodGeometry.push_back(FloodGeometry(newKey, floodFillInfo.TextureHandle, arrayTextureHandle, overrideLightIndex, lightBufferIndex, vbo.Count(), vertexCount));

	int upper = sideTexture == SideTexture::Upper ? 1 : 0;
	int lower = sideTexture == SideTexture::Lower ? 1 : 0;
	int textureIndex = m_glTextureManager.GetTexture(FSetTextureID(sectorPlane.TextureHandle))->ArrayIndex;
	float options = VertexOptions::PackSurface(0, 0, 0, upper, lower, overrideLightIndex, textureIndex);

	FloodFillVertex topLeft(::Helion::Geometry::Vec3F(vertices.TopLeft.X, vertices.TopLeft.Y, vertices.TopLeft.Z),
		vertices.TopLeft.Z, planeZ, prevPlaneZ, minZ, maxZ, options, lightBufferIndex, mapId);
	FloodFillVertex topRight(::Helion::Geometry::Vec3F(vertices.BottomRight.X, vertices.BottomRight.Y, vertices.TopLeft.Z),
		vertices.TopLeft.Z, planeZ, prevPlaneZ, minZ, maxZ, options, lightBufferIndex, mapId);
	FloodFillVertex bottomLeft(::Helion::Geometry::Vec3F(vertices.TopLeft.X, vertices.TopLeft.Y, vertices.BottomRight.Z),
		vertices.BottomRight.Z, planeZ, prevPlaneZ, minZ, maxZ, options, lightBufferIndex, mapId);
	FloodFillVertex bottomRight(::Helion::Geometry::Vec3F(vertices.BottomRight.X, vertices.BottomRight.Y, vertices.BottomRight.Z),
		vertices.BottomRight.Z, planeZ, prevPlaneZ, minZ, maxZ, options, lightBufferIndex, mapId);

	unsigned offset = vbo.Data.Reserve(VerticesPerWall);
	vbo.Data[offset++] = topLeft;
	vbo.Data[offset++] = bottomLeft;
	vbo.Data[offset++] = topRight;
	vbo.Data[offset++] = topRight;
	vbo.Data[offset++] = bottomLeft;
	vbo.Data[offset++] = bottomRight;
	vbo.SetNotUploaded();

	if (isFloodFillPlane)
		ProjectFloodPlane(vbo, 0, vertices, minZ, maxZ, planeZ, prevPlaneZ, lightIndex,
			maxPlaneZ > MaxTextureHeight ? -MaxTextureHeight : MaxTextureHeight, true, (float)lightIndex, mapId, textureIndex);

	if (m_renderMode == FloodFillRenderMode::Dynamic)
		return 0;

	return newKey;
}

// add = the vertices are appended to the buffer (startIndex is ignored), otherwise they overwrite the ones at startIndex.
void FloodFillRenderer::ProjectFloodPlane(Buffer::Array::Vertex::VertexBufferObject<FloodFillVertex>& vbo, int startIndex,
	const WallVertices& vertices, float minZ, float maxZ, float planeZ, float prevPlaneZ, int overrideLightIndex, int addHeight, bool add,
	float renderOptions, int mapId, int textureIndex)
{
	unsigned index;
	if (add)
		index = vbo.Data.Reserve(FloodPlaneAddCount * VerticesPerWall);
	else
		index = (unsigned)startIndex;

	float currentAddHeight = (float)addHeight;
	for (int i = 0; i < FloodPlaneAddCount; i++)
	{
		float topLeftZ = vertices.TopLeft.Z + currentAddHeight;
		float bottomRightZ = vertices.BottomRight.Z + currentAddHeight;
		float prevTopZ = vertices.PrevTopZ + currentAddHeight;
		float prevBottomZ = vertices.PrevBottomZ + currentAddHeight;

		float surfaceOptions = VertexOptions::PackSurface(0, 0, 0, 0, 0, overrideLightIndex, textureIndex);
		FloodFillVertex topLeft(::Helion::Geometry::Vec3F(vertices.TopLeft.X, vertices.TopLeft.Y, topLeftZ),
			prevTopZ, planeZ, prevPlaneZ, minZ, maxZ, surfaceOptions, renderOptions, mapId);
		FloodFillVertex topRight(::Helion::Geometry::Vec3F(vertices.BottomRight.X, vertices.BottomRight.Y, topLeftZ),
			prevTopZ, planeZ, prevPlaneZ, minZ, maxZ, surfaceOptions, renderOptions, mapId);
		FloodFillVertex bottomLeft(::Helion::Geometry::Vec3F(vertices.TopLeft.X, vertices.TopLeft.Y, bottomRightZ),
			prevBottomZ, planeZ, prevPlaneZ, minZ, maxZ, surfaceOptions, renderOptions, mapId);
		FloodFillVertex bottomRight(::Helion::Geometry::Vec3F(vertices.BottomRight.X, vertices.BottomRight.Y, bottomRightZ),
			prevBottomZ, planeZ, prevPlaneZ, minZ, maxZ, surfaceOptions, renderOptions, mapId);

		vbo.Data[index++] = topLeft;
		vbo.Data[index++] = bottomLeft;
		vbo.Data[index++] = topRight;
		vbo.Data[index++] = topRight;
		vbo.Data[index++] = bottomLeft;
		vbo.Data[index++] = bottomRight;
		currentAddHeight += (float)addHeight;
	}
}

void FloodFillRenderer::ClearStaticWall(int floodKey)
{
	if (m_renderMode == FloodFillRenderMode::Dynamic)
		return;

	FloodGeometry* data = TryGetFloodGeometry(floodKey);
	if (data != nullptr)
	{
		int listIndex;
		if (!TryGetFloodFillInfoIndex(data->TextureHandle, listIndex))
			return;

		FloodFillInfo& info = *m_floodFillInfos[(size_t)listIndex];
		OverwriteAndSubUploadVboWithZero(*info.Vbo, data->VboOffset, data->Vertices);
		m_freeData.push_back(*data);
	}
	else
	{
		assert(false && "Trying to clear a flood fill wall that was never added");
	}
}

void FloodFillRenderer::OverwriteAndSubUploadVboWithZero(Buffer::Array::Vertex::VertexBufferObject<FloodFillVertex>& vbo, int bufferOffset, int vertices)
{
	for (int i = 0; i < vertices; i++)
	{
		unsigned index = (unsigned)(bufferOffset + i);
		vbo.Data[index] = FloodFillVertex(::Helion::Geometry::Vec3F(0, 0, 0), 0, 0, 0, FLT_MAX, -FLT_MAX, 0, 0, -1);
	}

	vbo.UploadSubData(bufferOffset, vertices);
}

void FloodFillRenderer::Render(Shared::RenderInfo& renderInfo)
{
	Render(*GetProgram(), renderInfo, false);
}

void FloodFillRenderer::RenderWallClip(Shared::RenderInfo& renderInfo)
{
	Render(*GetWallClipProgram(), renderInfo, true);
}

void FloodFillRenderer::Render(FloodFillProgram& program, Shared::RenderInfo& renderInfo, bool wallClip)
{
	program.Bind();
	SetUniforms(program, renderInfo);

	for (size_t i = 0; i < m_floodFillInfos.size(); i++)
	{
		FloodFillInfo& info = *m_floodFillInfos[i];
		if (info.Pipeline->Empty())
			continue;

		if (!wallClip)
		{
			GLLegacyTexture* texture = m_glTextureManager.GetTexture(FSetTextureID(info.TextureHandle));
			GLLegacyTexture* brightmapTexture = m_glTextureManager.GetBrightmapTexture(info.TextureHandle);
			texture->Bind(BindTextures::BoundTexture);
			if (brightmapTexture != nullptr)
				brightmapTexture->Bind(BindTextures::BrightmapTexture);
			else
				GLLegacyTexture::Unbind(BindTextures::BrightmapTexture);
		}

		info.Vbo->UploadIfNeeded();
		info.Pipeline->Bind();
		info.Pipeline->DrawArrays();
	}
}

void FloodFillRenderer::SetUniforms(FloodFillProgram& program, Shared::RenderInfo& renderInfo)
{
	program.BoundTexture(BindTextures::BoundTexture);
	program.BrightmapTexture(BindTextures::BrightmapTexture);
	program.SectorLightTexture(BindTextures::SectorLight);
	program.ColormapTexture(BindTextures::Colormap);
	program.SectorColormapTexture(BindTextures::SectorColormap);
	program.SectorFogTexture(BindTextures::SectorFog);
	program.Camera(renderInfo.Camera->PositionInterpolated);
	program.CameraDirection(renderInfo.Camera->Direction);
	program.Mvp(renderInfo.Uniforms.Mvp);
	program.TimeFrac(renderInfo.TickFraction);
	program.HasInvulnerability(renderInfo.Uniforms.DrawInvulnerability);
	program.MvpNoPitch(renderInfo.Uniforms.MvpNoPitch);
	program.LightLevelMix(renderInfo.Uniforms.Mix);
	program.ExtraLight(renderInfo.Uniforms.ExtraLightOrColorMapIndex);
	program.DistanceOffset(renderInfo.Uniforms.DistanceOffset);
	program.ColorMix(renderInfo.Uniforms.ColorMix.Global);
	program.PaletteIndex((int)renderInfo.Uniforms.PaletteIndex);
	program.ColorMapIndex(renderInfo.Uniforms.ColorMapUniforms.GlobalIndex);
	program.LightMode(renderInfo.Uniforms.LightMode);
	program.GammaCorrection(renderInfo.Uniforms.GammaCorrection);
	program.UseBrightmaps(renderInfo.Uniforms.UseBrightmaps);
	program.UseSectorColor(renderInfo.Uniforms.SectorColor);
	program.UseSectorFog(renderInfo.Uniforms.SectorFogIndex);

	if (ContractUniforms)
		ContractUniforms(program, renderInfo);
}

void FloodFillRenderer::ClearVertices()
{
	assert(m_renderMode == FloodFillRenderMode::Dynamic && "Clear should only be called on dynamic flood fill renderer");

	for (auto& info : m_floodFillInfos)
		info->Pipeline->Clear();

	m_floodGeometry.clear();
}

void FloodFillRenderer::RenderHorizon(Shared::RenderInfo& renderInfo, const RenderPlane& plane, const std::vector<::Helion::Geometry::Vec3F>& triangles)
{
	if (triangles.empty() || IsSkyTexture(plane.TextureHandle))
		return;

	GLLegacyTexture* texture = m_glTextureManager.GetTexture(FSetTextureID(plane.TextureHandle));
	if (texture == nullptr)
		return;

	FloodFillProgram* program = GetProgram();
	if (m_horizonVbo == nullptr)
	{
		m_horizonVbo = std::make_unique<Buffer::Array::Vertex::StreamVertexBuffer<FloodFillVertex>>("Horizon");
		m_horizonPipeline = std::make_unique<VertexPipeline<FloodFillVertex>>(*program, *m_horizonVbo, "Horizon");
	}

	bool floor = plane.Facing == SectorPlaneFace::Floor;
	int overrideLightIndex;
	int lightIndex = floor ?
		::Helion::Render::Renderer::GetLightBufferIndex(plane.Sector, (int)SectorPlaneFace::Floor, LightBufferType::Floor, overrideLightIndex) :
		::Helion::Render::Renderer::GetLightBufferIndex(plane.Sector, (int)SectorPlaneFace::Ceiling, LightBufferType::Ceiling, overrideLightIndex);
	float lightBufferIndex = VertexOptions::PackRender(lightIndex, std::clamp(plane.LightLevelAdd, -256, 255));
	float options = VertexOptions::PackSurface(0, 0, 0, 0, 0, overrideLightIndex, texture->ArrayIndex);

	// The camera has to be above a floor / below a ceiling (FloodFillProgram collapses the geometry otherwise).
	constexpr float Unbounded = 1.0e9f;
	float planeZ = (float)plane.Z;
	float minZ = floor ? planeZ : -Unbounded;
	float maxZ = floor ? Unbounded : planeZ;

	m_horizonVbo->Clear();
	for (const ::Helion::Geometry::Vec3F& corner : triangles)
		m_horizonVbo->Add(FloodFillVertex(corner, corner.Z, planeZ, planeZ, minZ, maxZ, options, lightBufferIndex, 0));

	program->Bind();
	SetUniforms(*program, renderInfo);
	texture->Bind(BindTextures::BoundTexture);
	if (GLLegacyTexture* brightmapTexture = m_glTextureManager.GetBrightmapTexture(plane.TextureHandle))
		brightmapTexture->Bind(BindTextures::BrightmapTexture);
	else
		GLLegacyTexture::Unbind(BindTextures::BrightmapTexture);

	m_horizonVbo->UploadIfNeeded();
	m_horizonPipeline->Bind();
	m_horizonPipeline->DrawArrays();
}

void FloodFillRenderer::ClearData()
{
	m_floodFillInfos.clear();
	m_textureHandleToFloodFillInfoIndex.clear();
	m_floodGeometry.clear();
	m_freeData.clear();
}

FString FloodFillRenderer::GetLabel(const RenderPlane& plane)
{
	FString label;
	label.Format("Flood fill (Texture %d, Z = %f)", plane.TextureHandle, plane.Z);
	return label;
}

}
