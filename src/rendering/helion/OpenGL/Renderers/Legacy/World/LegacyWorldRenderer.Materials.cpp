/*
** LegacyWorldRenderer.Materials.cpp
**
** UZDoom-only partial of LegacyWorldRenderer: world geometry with a material texture (plan D13).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Walls and flats whose texture is not drawn from an array texture (GLTextureManager::UsesArray: canvas textures,
** GLDEFS material / custom shaders, normal / specular / PBR / detail / glow layers) cannot be sampled by the array
** programs. RenderWorldData::Draw and StaticCacheGeometryRenderer::RenderGeometryTypes hand them to
** DrawMaterialGeometry (Data::RenderWorldData::MaterialDraw), which draws the same vertices with the material program
** of the texture's shader index (StaticMaterial / InterpolationMaterial) and the layers of its FMaterial, like
** FRenderState::SetMaterial in HWWall::RenderTexturedWall / HWFlat::DrawFlat. Only the opaque world pass has
** material programs; the other passes (OIT, clip buffers) keep the array texture.
*/

#include "OpenGL/Renderers/Legacy/World/LegacyWorldRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldData.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticShader.h"
#include "OpenGL/Shader/ProgramRegistry.h"
#include "OpenGL/Shader/TextureUnits.h"
#include "OpenGL/Texture/GLTextureManager.h"
#include "Shader/UzdoomContract.h"
#include "BindTextures.h"
#include "hw_material.h"
#include "hw_cvars.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "textures.h"
#include "v_video.h"

CVAR(Float, gl_mask_threshold, 0.5f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

using ::Helion::Render::OpenGL::Shader::HelionProgramRegistry;
using ::Helion::Render::OpenGL::Shader::ProgramType;
using ::Helion::Render::OpenGL::Shader::TextureUnits;

bool LegacyWorldRenderer::DrawMaterialGeometry(Texture::Legacy::GLLegacyTexture* texture, const std::function<void()>& draw)
{
	FGameTexture* source = texture != nullptr ? texture->Source : nullptr;
	if (source == nullptr || m_currentRenderInfo == nullptr || OpenGL::Texture::GLTextureManager::UsesArray(source))
		return false;

	OpenGL::Shader::RenderProgram* current = OpenGL::Shader::RenderProgram::Current();
	const bool isStatic = current == m_staticProgram;
	if (!isStatic && current != m_interpolationProgram)
		return false;

	FMaterial* material = FMaterial::ValidateTexture(source, 0);
	if (material == nullptr)
		return false;

	int shaderIndex = material->GetShaderIndex();
	if (shaderIndex < 0 || shaderIndex >= Shader::UzdoomContract::MaterialShaderCount())
		shaderIndex = 0;

	RenderProgramBase* program;
	if (isStatic)
	{
		StaticShader* staticProgram = HelionProgramRegistry::Get<StaticShader>(ProgramType::StaticMaterial, shaderIndex, NORMAL_PASS);
		staticProgram->Bind();
		SetStaticUniforms(staticProgram, *m_currentRenderInfo);
		program = staticProgram;
	}
	else
	{
		InterpolationShader* interpolationProgram = HelionProgramRegistry::Get<InterpolationShader>(ProgramType::InterpolationMaterial, shaderIndex, NORMAL_PASS);
		SetInterpolationUniforms(interpolationProgram, *m_currentRenderInfo);
		program = interpolationProgram;
	}

	// The hardware textures of the layers (created by the backend when they are first used); a camera texture that is seen is
	// rendered again next frame (FCanvasTextureInfo::UpdateAll). The static mode draws the camera textures of the whole map:
	// PerformRender marks the ones its BSP walk reached instead.
	screen->PrecacheMaterial(material, 0);
	if (source->isHardwareCanvas() && !m_renderStatic)
		static_cast<FCanvasTexture*>(source->GetTexture())->NeedUpdate();

	int clamp = source->GetClampMode((int)texture->Flags & (int)CLAMP_XY);
	MaterialLayerInfo* layer = nullptr;
	TextureUnits::BindTexture(BindTextures::BoundTexture, material->GetLayer(0, 0, &layer), clamp);
	program->Tex(BindTextures::BoundTexture);
	int numLayers = std::min(material->NumLayers(), 12);
	for (int i = 1; i < numLayers; i++)
	{
		OpenGL::TextureUnit unit = (OpenGL::TextureUnit)((int)BindTextures::MaterialLayer1 + i - 1);
		TextureUnits::BindTexture(unit, material->GetLayer(i, 0, &layer), clamp);
		program->MaterialTexture(i + 1, unit);
	}

	// FRenderState::GetTextureModeAndFlags: camera textures are opaque.
	int textureMode = source->isHardwareCanvas() && !source->GetTranslucency() ? TM_OPAQUE : TM_NORMAL;
	program->TextureMode(textureMode | material->GetLayerFlags());
	// classic RenderScene: the solid pass has no alpha test, masked textures test against gl_mask_threshold.
	program->AlphaThreshold(source->isHardwareCanvas() ? -1.0f : gl_mask_threshold - 0.001f);
	program->FogEnabled(0);
	program->MaterialFlipY(source->isHardwareCanvas());

	StreamData stream = {};
	stream.uObjectColor = PalEntry(0xffffffff);
	stream.uObjectColor2 = PalEntry(0);
	stream.uVertexColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	FVector2 detail = material->GetDetailScale();
	stream.uDetailParms = { detail.X, detail.Y, 2, 0 };
	stream.timer = (float)((double)(screen->FrameTime - screen->RenderState()->firstFrame) * (double)source->GetShaderSpeed() / 1000.0);
	program->MaterialStreamData(stream);

	draw();

	for (int i = 1; i < numLayers; i++)
		TextureUnits::Unbind((OpenGL::TextureUnit)((int)BindTextures::MaterialLayer1 + i - 1));
	current->Bind();
	return true;
}

}
