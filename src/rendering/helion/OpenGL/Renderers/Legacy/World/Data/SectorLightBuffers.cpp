/*
** SectorLightBuffers.cpp
**
** UZDoom-only. Port of the sector light buffer code of Helion Renderer.World.cs.
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

#include "OpenGL/Renderers/Legacy/World/Data/SectorLightBuffers.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstring>
#include "BindTextures.h"
#include "Util/Constants.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderVars.h"
#include "g_levellocals.h"
#include "c_cvars.h"

// The black fog distance of the light modes without a fog colour (helion_lightmode.fp reads it from the level constants).
CVAR(Int, gl_distfog, 70, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
EXTERN_CVAR(Int, r_distance_cull_type)
EXTERN_CVAR(Float, r_visibility)
EXTERN_CVAR(Int, r_extralight)
EXTERN_CVAR(Int, gl_weaponlight)

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

using Helion::Util::Constants::LightBuffer;
using Textures::SizedInternalFormat;

namespace
{

constexpr int FourComponents = Textures::GLBufferTextureStorage<float>::FourComponentLength;

// Helion's FogDensity is the UDMF value / 510, UZDoom keeps it as UDMF value / 2.
constexpr float FogDensityScale = 1.0f / 255.0f;

template<typename T>
TArray<T> AllocLightSectorArray(int sectorBufferCount, int components)
{
	TArray<T> array;
	array.Resize((unsigned)(sectorBufferCount * components));
	std::fill(array.begin(), array.end(), T());
	return array;
}

}

SectorLightBuffers::SectorLightBuffers() = default;

SectorLightBuffers::~SectorLightBuffers()
{
	Dispose();
}

int SectorLightBuffers::SectorBufferCount(FLevelLocals* level)
{
	return (int)level->sectors.Size() * LightBuffer::BufferSize + LightBuffer::SectorIndexStart;
}

// Sector.GetByteLightLevel. UZDoom has no separate foggy / sector colour light rules at this point: the plain clamped level.
int SectorLightBuffers::GetByteLightLevel(const sector_t* sector)
{
	return std::clamp((int)sector->GetLightLevel(), 0, 255);
}

SectorLightBuffers::SectorSignature SectorLightBuffers::GetSignature(const sector_t* sector)
{
	SectorSignature signature;
	signature.LightLevel = GetByteLightLevel(sector);
	signature.LightColor = sector->Colormap.LightColor.d;
	signature.FadeColor = sector->Colormap.FadeColor.d;
	signature.FogDensity = sector->Colormap.FogDensity;
	signature.Desaturation = sector->Colormap.Desaturation;
	return signature;
}

SectorLightBuffers::FogGlobals SectorLightBuffers::GetFogGlobals(FLevelLocals* level)
{
	FogGlobals globals;
	globals.LightMode = (int)getRealLightmode(level, true);
	globals.DistFog = gl_distfog;
	globals.LevelFlags = (int)(level->flags & LEVEL_HASFADETABLE);
	globals.LevelFlags3 = (int)(level->flags3 & LEVEL3_NOLIGHTFADE);
	globals.FogDensity = level->fogdensity;
	globals.OutsideFogDensity = level->outsidefogdensity;
	globals.OutsideFog = level->info ? level->info->outsidefog : 0;
	globals.CullType = r_distance_cull_type;
	globals.CullDist = level->culldist;
	globals.CullColor = level->cullcolor.d;
	return globals;
}

// The level constants of the truecolor light stage (HelionLevelLight of UzdoomContract::LightStageDeclarations):
// texel 0 = MAXDIST of SetShaderLight, brightfog, r_extralight (RescaleLightLevel), gl_weaponlight (getExtraLight);
// texel 1 = LEVEL3_FORCEFAKECONTRAST.
void SectorLightBuffers::GetLevelTexels(FLevelLocals* level, float* dest)
{
	dest[0] = 256.0f * (r_visibility / 8.0f);
	dest[1] = level->brightfog ? 1.0f : 0.0f;
	dest[2] = (float)*r_extralight;
	dest[3] = (float)*gl_weaponlight;
	dest[4] = (level->flags3 & LEVEL3_FORCEFAKECONTRAST) ? 1.0f : 0.0f;
	dest[5] = 0;
	dest[6] = 0;
	dest[7] = 0;
}

// UZDoom-only (truecolor): hw_setcolor.cpp SetFog and hw_lighting.cpp GetFogDensity (blend factor 0) without the light level, which
// the shader adds per fragment (HelionFogDensity in shaders/glsl/helion_lightmode.fp). w is a density code:
//   >= ForcedFog: forced fog (applied to fullbright surfaces too) of density w - ForcedFog;  >= 0: the fog density;
//   -1: case 5 (density from the light level);  <= -2: black fog distfogtable, gl_distfog = -w - 2.
// Skybox fog (portalState.inskybox) is the portal milestone's (M5).
SectorLightBuffers::Vec4 SectorLightBuffers::GetUzdoomFog(const sector_t* sector) const
{
	const FColormap& cmap = sector->Colormap;
	const FogGlobals& globals = m_fogGlobals;
	ELightMode lightmode = (ELightMode)globals.LightMode;
	PalEntry fogcolor;
	float code;

	if (globals.CullType > 1)
	{
		code = ForcedFog + (float)std::clamp((int)(19200.0 / globals.CullDist), 1, 255);
		fogcolor = cmap.FadeColor != 0 ? cmap.FadeColor : PalEntry(globals.CullColor);
	}
	else if (globals.LevelFlags != 0)
	{
		code = ForcedFog + 70;
		fogcolor = PalEntry(0x808080);
	}
	else
	{
		fogcolor = cmap.FadeColor;
		bool doomSoftware = lightmode == ELightMode::ZDoomSoftware || lightmode == ELightMode::DoomSoftware;
		if (lightmode == ELightMode::DoomLegacy)
			code = globals.FogDensity ? (float)globals.FogDensity : 18.0f;
		else if (cmap.FogDensity != 0)
			code = (float)cmap.FogDensity;
		else if ((fogcolor.d & 0xffffff) == 0)
			code = (!doomSoftware && globals.LevelFlags3 == 0) ? -(2.0f + (float)std::max(globals.DistFog, 0)) : 0.0f;
		else if (globals.OutsideFogDensity != 0 && APART(globals.OutsideFog) != 0xff && (fogcolor.d & 0xffffff) == (globals.OutsideFog & 0xffffff))
			code = (float)globals.OutsideFogDensity;
		else if (globals.FogDensity != 0)
			code = (float)globals.FogDensity;
		else
			code = -1.0f;
	}

	Vec4 result;
	result.X = fogcolor.r / 255.0f;
	result.Y = fogcolor.g / 255.0f;
	result.Z = fogcolor.b / 255.0f;
	result.W = code;
	return result;
}

SectorLightBuffers::Vec4 SectorLightBuffers::GetSectorFog(const sector_t* sector, int lightLevel) const
{
	if (!m_paletteColorMode)
		return GetUzdoomFog(sector);
	return GetSectorFogDensity(sector->Colormap.FadeColor.d, lightLevel, sector->Colormap.FogDensity * FogDensityScale);
}

void SectorLightBuffers::Init(FLevelLocals* level, bool alloc)
{
	m_level = level;
	m_sectorFog = false;
	m_sectorColor = false;
	m_paletteColorMode = Shader::ShaderVars::PaletteColorMode;
	m_lastGlobalViewFog = Vec4();
	m_fogGlobals = GetFogGlobals(level);

	const unsigned sectorCount = level->sectors.Size();
	m_updateLightSectors.EnsureCapacity(sectorCount);
	m_updateColorMapSectors.EnsureCapacity(sectorCount);
	m_updateFogColorSectors.EnsureCapacity(sectorCount);
	m_updateLightSectors.ClearAndReset();
	m_updateColorMapSectors.ClearAndReset();
	m_updateFogColorSectors.ClearAndReset();

	m_signatures.resize(sectorCount);
	for (unsigned i = 0; i < sectorCount; i++)
		m_signatures[i] = GetSignature(&level->sectors[i]);

	const int sectorBufferCount = SectorBufferCount(level);

	// Light buffer
	if (alloc || !m_sectorLightsBuffer || m_sectorLightsBuffer->DataLength() != sectorBufferCount)
	{
		m_sectorLightsBuffer = std::make_unique<LightStorage>("Sector light levels", AllocLightSectorArray<uint32_t>(sectorBufferCount, 1), SizedInternalFormat::R8ui, false);
		const int length = m_sectorLightsBuffer->DataLength();
		m_sectorLightsBuffer->Map([this, length](uint32_t* data) { InitLightBuffer(data, length); });
	}
	else
	{
		auto mapped = m_sectorLightsBuffer->GetMappedBufferAndBind();
		InitLightBuffer(mapped.MappedMemoryPtr, m_sectorLightsBuffer->DataLength());
		m_sectorLightsBuffer->Unbind();
	}

	// Colormap buffer
	if (alloc || !m_sectorColorMapsBuffer || m_sectorColorMapsBuffer->DataLength() != sectorBufferCount * FourComponents)
	{
		m_sectorColorMapsBuffer = std::make_unique<FloatStorage>("Sector colormaps", AllocLightSectorArray<float>(sectorBufferCount, FourComponents), SizedInternalFormat::Rgba32f, false);
		const int length = m_sectorColorMapsBuffer->DataLength();
		m_sectorColorMapsBuffer->Map([this, length](float* data) { InitSectorColorMap(data, length); });
	}
	else
	{
		auto mapped = m_sectorColorMapsBuffer->GetMappedBufferAndBind();
		InitSectorColorMap(mapped.MappedMemoryPtr, m_sectorColorMapsBuffer->DataLength());
		m_sectorColorMapsBuffer->Unbind();
	}

	// Fog buffer
	if (alloc || !m_sectorFogBuffer || m_sectorFogBuffer->DataLength() != sectorBufferCount * FourComponents)
	{
		m_sectorFogBuffer = std::make_unique<FloatStorage>("Sector fog", AllocLightSectorArray<float>(sectorBufferCount, FourComponents), SizedInternalFormat::Rgba32f, false);
		const int length = m_sectorFogBuffer->DataLength();
		m_sectorFogBuffer->Map([this, length](float* data) { InitSectorFogBuffer(data, length); });
	}
	else
	{
		auto mapped = m_sectorFogBuffer->GetMappedBufferAndBind();
		InitSectorFogBuffer(mapped.MappedMemoryPtr, m_sectorFogBuffer->DataLength());
		m_sectorFogBuffer->Unbind();
	}

	// Glow buffer
	const int glowLength = (GlowLevelTexels + (int)sectorCount * GlowTexels) * FourComponents;
	if (alloc || !m_sectorGlowBuffer || m_sectorGlowBuffer->DataLength() != glowLength)
		m_sectorGlowBuffer = std::make_unique<FloatStorage>("Sector glow", AllocLightSectorArray<float>(GlowLevelTexels + (int)sectorCount * GlowTexels, FourComponents), SizedInternalFormat::Rgba32f, false);
	m_glow.assign((size_t)glowLength, 0.0f);
	UpdateGlow(true);
}

void SectorLightBuffers::InitLightBuffer(uint32_t* lightBuffer, int length)
{
	lightBuffer[LightBuffer::DarkIndex] = 0;
	lightBuffer[LightBuffer::FullBrightIndex] = 255;

	for (int i = 0; i < LightBuffer::ColorMapCount; i++)
		lightBuffer[LightBuffer::ColorMapStartIndex + i] =
			(uint8_t)(256 - ((LightBuffer::ColorMapCount - i) * 256 / LightBuffer::ColorMapCount));

	int lowest = 0, highest = 0;
	for (unsigned i = 0; i < m_level->sectors.Size(); i++)
	{
		const sector_t* sector = &m_level->sectors[i];
		SetSectorLight(lightBuffer, length, sector, GetByteLightLevel(sector), lowest, highest);
	}
}

void SectorLightBuffers::InitSectorColorMap(float* colorMapBuffer, int length)
{
	// UZDoom: every slot below the sectors is white (Helion only wrote slot 0): the truecolor programs always read this buffer.
	for (int i = 0; i < LightBuffer::SectorIndexStart; i++)
	{
		colorMapBuffer[i * FourComponents + 0] = 1;
		colorMapBuffer[i * FourComponents + 1] = 1;
		colorMapBuffer[i * FourComponents + 2] = 1;
		colorMapBuffer[i * FourComponents + 3] = 0;
	}

	int lowest = 0, highest = 0;
	for (unsigned i = 0; i < m_level->sectors.Size(); i++)
		SetSectorColorMap(colorMapBuffer, length, &m_level->sectors[i], lowest, highest);
}

void SectorLightBuffers::InitSectorFogBuffer(float* fadeBuffer, int length)
{
	fadeBuffer[0] = 0;
	fadeBuffer[1] = 0;
	fadeBuffer[2] = 0;

	int lowest = 0, highest = 0;
	for (unsigned i = 0; i < m_level->sectors.Size(); i++)
	{
		const sector_t* sector = &m_level->sectors[i];
		SetSectorFog(fadeBuffer, length, sector, GetByteLightLevel(sector), lowest, highest);
	}
}

void SectorLightBuffers::SetGlobalViewFog(int sectorId)
{
	if (!m_sectorFogBuffer || m_level == nullptr || sectorId < 0 || sectorId >= (int)m_level->sectors.Size())
		return;

	const sector_t* sector = &m_level->sectors[sectorId];
	Vec4 fog = GetSectorFog(sector, GetByteLightLevel(sector));
	if (fog == m_lastGlobalViewFog)
		return;

	m_lastGlobalViewFog = fog;
	auto mapped = m_sectorFogBuffer->GetMappedBufferAndBind();
	float* fogData = mapped.MappedMemoryPtr;

	// Sectors fog color start after LightBuffer::SectorIndexStart so index 0 can be used for forcing a global view color.
	fogData[0] = fog.X;
	fogData[1] = fog.Y;
	fogData[2] = fog.Z;
	fogData[3] = fog.W;

	m_sectorFogBuffer->SetDirtyRange(0, FourComponents);
	m_sectorFogBuffer->Unbind();
}

void SectorLightBuffers::SetSectorFog(float* fogBuffer, int length, const sector_t* sector, int lightLevel, int& lowest, int& highest)
{
	const int index = sector->Index() * LightBuffer::BufferSize + LightBuffer::SectorIndexStart;
	Vec4 fog = GetSectorFog(sector, lightLevel);

	// Sector.IgnoreFogColor has no UZDoom equivalent; fog is never ignored.

	if ((sector->Colormap.FadeColor.d & 0xFFFFFF) != 0)
		m_sectorFog = true;

	assert(length > (index + LightBuffer::BufferSize) * FourComponents - 1 && "Invalid sector id for sector fog buffer");

	for (int offset : { LightBuffer::FloorOffset, LightBuffer::CeilingOffset, LightBuffer::WallOffset })
	{
		float* dest = &fogBuffer[(index + offset) * FourComponents];
		dest[0] = fog.X;
		dest[1] = fog.Y;
		dest[2] = fog.Z;
		dest[3] = fog.W;
	}

	lowest = std::min(lowest, index);
	highest = std::max(highest, index);
}

void SectorLightBuffers::SetSectorColorMap(float* colorMapBuffer, int length, const sector_t* sector, int& lowest, int& highest)
{
	const int index = sector->Index() * LightBuffer::BufferSize + LightBuffer::SectorIndexStart;
	Vec3 setColor = GetSectorSetColor(sector);

	if (setColor.X != 1 || setColor.Y != 1 || setColor.Z != 1)
		m_sectorColor = true;

	assert(length > (index + LightBuffer::BufferSize) * FourComponents - 1 && "Invalid sector id for sector colormap buffer");

	for (int offset : { LightBuffer::FloorOffset, LightBuffer::CeilingOffset, LightBuffer::WallOffset })
	{
		float* dest = &colorMapBuffer[(index + offset) * FourComponents];
		dest[0] = setColor.X;
		dest[1] = setColor.Y;
		dest[2] = setColor.Z;
		// UZDoom: FColormap::Desaturation (the uDesaturationFactor of SetColor).
		dest[3] = sector->Colormap.Desaturation / 255.0f;
	}

	lowest = std::min(lowest, index);
	highest = std::max(highest, index);
}

void SectorLightBuffers::SetSectorLight(uint32_t* lightData, int length, const sector_t* sector, int lightLevel, int& lowest, int& highest)
{
	const int index = sector->Index() * LightBuffer::BufferSize + LightBuffer::SectorIndexStart;
	assert(length > index + (LightBuffer::BufferSize - 1) && "Invalid sector id for sector light buffer");
	(void)length;
	lightData[index + LightBuffer::FloorOffset] = (uint32_t)lightLevel;
	lightData[index + LightBuffer::CeilingOffset] = (uint32_t)lightLevel;
	lightData[index + LightBuffer::WallOffset] = (uint32_t)lightLevel;

	lowest = std::min(lowest, index);
	highest = std::max(highest, index);
}

SectorLightBuffers::Vec4 SectorLightBuffers::GetSectorFogDensity(uint32_t fadeColor, int lightLevel, float fogDensity)
{
	constexpr float FadeFactor = 0.004f;
	// The alpha byte of FadeColor is the fade density in UZDoom, the colour is the rgb part only.
	if ((fadeColor & 0xFFFFFF) == 0)
		return Vec4();

	if (fogDensity == 0)
		fogDensity = (1.0f - lightLevel / 255.0f) * FadeFactor;
	else
		fogDensity *= FadeFactor;

	PalEntry color;
	color.d = fadeColor;
	Vec4 result;
	result.X = color.r / 255.0f;
	result.Y = color.g / 255.0f;
	result.Z = color.b / 255.0f;
	result.W = fogDensity;
	return result;
}

SectorLightBuffers::Vec3 SectorLightBuffers::GetSectorSetColor(const sector_t* sector)
{
	// True color always uses rgb color mix.
	// Palette color uses r channel as colormap index when b channel is -1.
	// When b channel is not -1 then palette color mode will treat as true color mix to support rgb mix values.
	// UZDoom sectors have no colormap index, so a sector is either white (no colormap) or an rgb mix. The desaturation is in w (SetSectorColorMap).
	const PalEntry lightColor = sector->Colormap.LightColor;
	const bool hasColor = (lightColor.d & 0xFFFFFF) != 0xFFFFFF;

	if (hasColor)
		return { lightColor.r / 255.0f, lightColor.g / 255.0f, lightColor.b / 255.0f };

	if (!Shader::ShaderVars::PaletteColorMode)
		return { 1, 1, 1 };

	return { 0, -1, -1 };
}

void SectorLightBuffers::OnSectorLightChanged(sector_t* sector)
{
	m_updateLightSectors.Add(sector);
}

void SectorLightBuffers::OnSectorColorMapChanged(sector_t* sector)
{
	m_updateColorMapSectors.Add(sector);
}

void SectorLightBuffers::OnSectorFogColorChanged(sector_t* sector)
{
	m_updateFogColorSectors.Add(sector);
}

// Compares the values the buffers were last written from with the sectors.
void SectorLightBuffers::DetectChanges()
{
	if (m_level == nullptr)
		return;

	const unsigned count = std::min((unsigned)m_signatures.size(), m_level->sectors.Size());
	for (unsigned i = 0; i < count; i++)
	{
		sector_t* sector = &m_level->sectors[i];
		SectorSignature current = GetSignature(sector);
		SectorSignature& cached = m_signatures[i];

		if (current.LightLevel != cached.LightLevel)
			m_updateLightSectors.Add(sector);
		if (current.LightColor != cached.LightColor || current.Desaturation != cached.Desaturation)
			m_updateColorMapSectors.Add(sector);
		if (current.FadeColor != cached.FadeColor || current.FogDensity != cached.FogDensity)
			m_updateFogColorSectors.Add(sector);

		cached = current;
	}
}

void SectorLightBuffers::Update()
{
	if (m_level == nullptr)
		return;

	// The colormap and fog content depends on the palette mode; rewrite everything when it switched.
	bool paletteSwitched = m_paletteColorMode != Shader::ShaderVars::PaletteColorMode;
	if (paletteSwitched && m_sectorColorMapsBuffer)
	{
		m_paletteColorMode = Shader::ShaderVars::PaletteColorMode;
		m_sectorColor = false;
		auto mapped = m_sectorColorMapsBuffer->GetMappedBufferAndBind();
		InitSectorColorMap(mapped.MappedMemoryPtr, m_sectorColorMapsBuffer->DataLength());
		m_sectorColorMapsBuffer->Unbind();
	}

	// The truecolor fog codes follow the light mode, gl_distfog and the level fog settings (GetUzdoomFog).
	FogGlobals globals = GetFogGlobals(m_level);
	if ((paletteSwitched || !(globals == m_fogGlobals)) && m_sectorFogBuffer)
	{
		m_fogGlobals = globals;
		m_sectorFog = false;
		auto mapped = m_sectorFogBuffer->GetMappedBufferAndBind();
		InitSectorFogBuffer(mapped.MappedMemoryPtr, m_sectorFogBuffer->DataLength());
		m_sectorFogBuffer->Unbind();
	}

	DetectChanges();
	UpdateLights();
	UpdateColorMaps();
	UpdateFogColors();
	UpdateGlow(false);
	m_updateLightSectors.Clear();
	m_updateColorMapSectors.Clear();
	m_updateFogColorSectors.Clear();
}

void SectorLightBuffers::UpdateLights()
{
	if (m_updateLightSectors.UpdateSectors.Size() == 0 || !m_sectorLightsBuffer || !m_sectorFogBuffer)
		return;

	{
		auto mapped = m_sectorLightsBuffer->GetMappedBufferAndBind();
		const int length = m_sectorLightsBuffer->DataLength();
		int lowest = length, highest = 0;

		for (unsigned i = 0; i < m_updateLightSectors.UpdateSectors.Size(); i++)
		{
			const sector_t* sector = m_updateLightSectors.UpdateSectors[i];
			SetSectorLight(mapped.MappedMemoryPtr, length, sector, GetByteLightLevel(sector), lowest, highest);
		}

		m_sectorLightsBuffer->SetDirtyRange(lowest, highest - lowest + LightBuffer::BufferSize);
		// This was done in the same loop but would cause crashes on 3.3 GPUs
		m_sectorLightsBuffer->Unbind();
	}

	{
		auto mapped = m_sectorFogBuffer->GetMappedBufferAndBind();
		const int length = m_sectorFogBuffer->DataLength();
		int lowest = length, highest = 0;

		for (unsigned i = 0; i < m_updateLightSectors.UpdateSectors.Size(); i++)
		{
			const sector_t* sector = m_updateLightSectors.UpdateSectors[i];
			SetSectorFog(mapped.MappedMemoryPtr, length, sector, GetByteLightLevel(sector), lowest, highest);
		}

		m_sectorFogBuffer->SetDirtyRange(lowest * FourComponents, (highest - lowest + LightBuffer::BufferSize) * FourComponents);
		m_sectorFogBuffer->Unbind();
	}
}

void SectorLightBuffers::UpdateColorMaps()
{
	if (m_updateColorMapSectors.UpdateSectors.Size() == 0 || !m_sectorColorMapsBuffer)
		return;

	auto mapped = m_sectorColorMapsBuffer->GetMappedBufferAndBind();
	const int length = m_sectorColorMapsBuffer->DataLength();
	int lowest = length, highest = 0;

	for (unsigned i = 0; i < m_updateColorMapSectors.UpdateSectors.Size(); i++)
		SetSectorColorMap(mapped.MappedMemoryPtr, length, m_updateColorMapSectors.UpdateSectors[i], lowest, highest);

	m_sectorColorMapsBuffer->SetDirtyRange(lowest * FourComponents, (highest - lowest + LightBuffer::BufferSize) * FourComponents);
	m_sectorColorMapsBuffer->Unbind();
}

void SectorLightBuffers::UpdateFogColors()
{
	if (m_updateFogColorSectors.UpdateSectors.Size() == 0 || !m_sectorFogBuffer)
		return;

	auto mapped = m_sectorFogBuffer->GetMappedBufferAndBind();
	const int length = m_sectorFogBuffer->DataLength();
	int lowest = length, highest = 0;

	for (unsigned i = 0; i < m_updateFogColorSectors.UpdateSectors.Size(); i++)
	{
		const sector_t* sector = m_updateFogColorSectors.UpdateSectors[i];
		SetSectorFog(mapped.MappedMemoryPtr, length, sector, GetByteLightLevel(sector), lowest, highest);
	}

	m_sectorFogBuffer->SetDirtyRange(lowest * FourComponents, (highest - lowest + LightBuffer::BufferSize) * FourComponents);
	m_sectorFogBuffer->Unbind();
}

// UZDoom-only: sector_t::GetWallGlow (GLDEFS glow textures, UDMF glow colours and heights) and the glow planes of the walls
// (hw_walls.cpp SetGlowPlanes with frontsector->ceilingplane / floorplane). Glowing textures can animate and planes move without
// any hook, so the values are compared every frame.
void SectorLightBuffers::UpdateGlow(bool force)
{
	if (!m_sectorGlowBuffer || m_level == nullptr)
		return;

	constexpr int levelFloats = GlowLevelTexels * FourComponents;
	constexpr int sectorFloats = GlowTexels * FourComponents;
	if (m_glow.size() < (size_t)levelFloats)
		return;

	int first = INT_MAX, last = -1;
	auto store = [&](int offset, const float* values, int count) {
		float* cached = &m_glow[(size_t)offset];
		if (force || memcmp(cached, values, (size_t)count * sizeof(float)) != 0)
		{
			memcpy(cached, values, (size_t)count * sizeof(float));
			first = std::min(first, offset);
			last = std::max(last, offset + count);
		}
	};

	float level[levelFloats];
	GetLevelTexels(m_level, level);
	store(0, level, levelFloats);

	const unsigned count = std::min((unsigned)((m_glow.size() - levelFloats) / sectorFloats), m_level->sectors.Size());
	for (unsigned i = 0; i < count; i++)
	{
		sector_t* sector = &m_level->sectors[i];
		float values[sectorFloats];
		float* top = values;
		float* bottom = values + 4;
		sector->GetWallGlow(top, bottom);
		if (top[3] <= 0)
			top[0] = top[1] = top[2] = top[3] = 0;
		if (bottom[3] <= 0)
			bottom[0] = bottom[1] = bottom[2] = bottom[3] = 0;

		const secplane_t* planes[2] = { &sector->ceilingplane, &sector->floorplane };
		for (int p = 0; p < 2; p++)
		{
			const DVector3& n = planes[p]->Normal();
			float* dest = values + 8 + p * 4;
			dest[0] = (float)n.X;
			dest[1] = (float)n.Y;
			dest[2] = (float)planes[p]->negiC;
			dest[3] = (float)planes[p]->fD();
		}
		store(levelFloats + (int)i * sectorFloats, values, sectorFloats);
	}

	if (last < 0)
		return;

	auto mapped = m_sectorGlowBuffer->GetMappedBufferAndBind();
	const int length = last - first;
	memcpy(mapped.MappedMemoryPtr + first, m_glow.data() + first, (size_t)length * sizeof(float));
	m_sectorGlowBuffer->SetDirtyRange(first, length);
	m_sectorGlowBuffer->Unbind();
}

void SectorLightBuffers::BindTextures()
{
	if (m_sectorLightsBuffer)
		m_sectorLightsBuffer->BindTexture(Render::BindTextures::SectorLight);
	if (m_sectorColorMapsBuffer)
		m_sectorColorMapsBuffer->BindTexture(Render::BindTextures::SectorColormap);
	if (m_sectorFogBuffer)
		m_sectorFogBuffer->BindTexture(Render::BindTextures::SectorFog);
	if (m_sectorGlowBuffer)
		m_sectorGlowBuffer->BindTexture(Render::BindTextures::SectorGlow);
}

void SectorLightBuffers::Dispose()
{
	m_sectorLightsBuffer.reset();
	m_sectorColorMapsBuffer.reset();
	m_sectorFogBuffer.reset();
	m_sectorGlowBuffer.reset();
	m_glow.clear();
	m_signatures.clear();
	m_level = nullptr;
}

}
