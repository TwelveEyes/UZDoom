/*
** SectorLightBuffers.h
**
** UZDoom-only. Sector light / colormap / fog buffer textures of Helion Renderer.World.cs (InitLightBuffer ... SetGlobalViewFog).
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
#include <vector>
#include "OpenGL/Textures/GLBufferTextureStorage.h"
#include "World/SectorUpdates.h"

struct FLevelLocals;
struct sector_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

// Owns the buffer textures read by the world shaders:
//   sector lights    (R8ui, element type uint32_t - see GLBufferTexture.h): one byte light level per floor/ceiling/wall slot
//   sector colormaps (Rgba32f): rgb mix of the sector colour, or (colormapIndex, -1, -1) in palette mode; w = desaturation (0..1)
//   sector fog       (Rgba32f): rgb fade colour and fog density in w. Truecolor (UZDoom light modes): w is the density code of
//                    GetUzdoomFog (helion_lightmode.fp HelionFogDensity)
//   sector glow      (Rgba32f, UZDoom-only): GlowLevelTexels texels of level constants (GetLevelTexels), then GlowTexels per
//                    sector: ceiling glow (rgb, height), floor glow, ceiling plane, floor plane (nx, ny, negiC, D) -
//                    sector_t::GetWallGlow and the glow planes of hw_walls.cpp SetGlowPlanes
// Index of a sector slot: sector->Index() * LightBuffer::BufferSize + LightBuffer::SectorIndexStart (+ Floor/Ceiling/WallOffset).
class SectorLightBuffers
{
public:
	SectorLightBuffers();
	~SectorLightBuffers();

	SectorLightBuffers(const SectorLightBuffers&) = delete;
	SectorLightBuffers& operator=(const SectorLightBuffers&) = delete;

	// Renderer.World InitLightBuffer calls. alloc = !SameAsPreviousMap: reallocate the buffers, otherwise rewrite them in place.
	void Init(FLevelLocals* level, bool alloc);

	// Optional change hooks. Update() also detects changes of the sector values itself, so calling these is not required.
	void OnSectorLightChanged(sector_t* sector);
	void OnSectorColorMapChanged(sector_t* sector);
	void OnSectorFogColorChanged(sector_t* sector);

	// UpdateBuffers: flushes the pending lists (UpdateLights, UpdateColorMaps, UpdateFogColors).
	void Update();

	void SetGlobalViewFog(int sectorId);
	void BindTextures();

	// Truecolor programs always read the colour and fog buffers: the UZDoom light modes darken with black fog too.
	bool SectorFog() const { return m_sectorFog || !m_paletteColorMode; }
	bool SectorColor() const { return m_sectorColor || !m_paletteColorMode; }

	// Layout of the glow buffer (see the class comment).
	static constexpr int GlowLevelTexels = 2;
	static constexpr int GlowTexels = 4;
	// Offset of the forced fog codes of GetUzdoomFog (HelionForcedFog of helion_lightmode.fp).
	static constexpr float ForcedFog = 10000.0f;

	void Dispose();

private:
	using LightStorage = Textures::GLBufferTextureStorage<uint32_t>;
	using FloatStorage = Textures::GLBufferTextureStorage<float>;

	// What the buffers were last written from, to detect changes made behind the hooks' back.
	struct SectorSignature
	{
		int LightLevel = 0;
		uint32_t LightColor = 0;
		uint32_t FadeColor = 0;
		int FogDensity = 0;
		int Desaturation = 0;
	};

	// The level / CVar state the truecolor fog codes depend on (GetUzdoomFog).
	struct FogGlobals
	{
		int LightMode = 0;
		int DistFog = 0;
		int LevelFlags = 0;
		int LevelFlags3 = 0;
		int FogDensity = 0;
		int OutsideFogDensity = 0;
		uint32_t OutsideFog = 0;
		int CullType = 0;
		double CullDist = 0;
		uint32_t CullColor = 0;
		bool operator==(const FogGlobals& o) const = default;
	};

	struct Vec4
	{
		float X = 0, Y = 0, Z = 0, W = 0;
		bool operator==(const Vec4& o) const { return X == o.X && Y == o.Y && Z == o.Z && W == o.W; }
	};

	struct Vec3
	{
		float X, Y, Z;
	};

	static int SectorBufferCount(FLevelLocals* level);
	static SectorSignature GetSignature(const sector_t* sector);
	static int GetByteLightLevel(const sector_t* sector);
	static Vec4 GetSectorFogDensity(uint32_t fadeColor, int lightLevel, float fogDensity);
	Vec4 GetUzdoomFog(const sector_t* sector) const;
	Vec4 GetSectorFog(const sector_t* sector, int lightLevel) const;
	static FogGlobals GetFogGlobals(FLevelLocals* level);
	static void GetLevelTexels(FLevelLocals* level, float* dest);
	static Vec3 GetSectorSetColor(const sector_t* sector);

	void InitLightBuffer(uint32_t* lightBuffer, int length);
	void InitSectorColorMap(float* colorMapBuffer, int length);
	void InitSectorFogBuffer(float* fadeBuffer, int length);

	void SetSectorLight(uint32_t* lightData, int length, const sector_t* sector, int lightLevel, int& lowest, int& highest);
	void SetSectorColorMap(float* colorMapBuffer, int length, const sector_t* sector, int& lowest, int& highest);
	void SetSectorFog(float* fogBuffer, int length, const sector_t* sector, int lightLevel, int& lowest, int& highest);

	void DetectChanges();
	void UpdateLights();
	void UpdateColorMaps();
	void UpdateFogColors();
	void UpdateGlow(bool force);

	FLevelLocals* m_level = nullptr;
	std::unique_ptr<LightStorage> m_sectorLightsBuffer;
	std::unique_ptr<FloatStorage> m_sectorColorMapsBuffer;
	std::unique_ptr<FloatStorage> m_sectorFogBuffer;
	std::unique_ptr<FloatStorage> m_sectorGlowBuffer;
	std::vector<float> m_glow;
	FogGlobals m_fogGlobals;
	std::vector<SectorSignature> m_signatures;
	Helion::World::SectorUpdates::SectorUpdateList m_updateLightSectors;
	Helion::World::SectorUpdates::SectorUpdateList m_updateColorMapSectors;
	Helion::World::SectorUpdates::SectorUpdateList m_updateFogColorSectors;
	Vec4 m_lastGlobalViewFog;
	bool m_sectorFog = false;
	bool m_sectorColor = false;
	bool m_paletteColorMode = false;
};

}
