/*
** CoronaRenderer.h
**
** UZDoom-only (plan M4.1). Helion style port of classic DrawCoronas / DrawCorona (hw_drawinfo.cpp) and the corona branch of
** HWSprite::Process (hw_sprites.cpp). Both are disabled in UZDoom (#if 0, there is no native ACorona class and the ZScript
** `Corona` actor is commented out), the port follows the disabled code:
**   - actors of the class `Corona` are not drawn as sprites, the entity renderer hands them to AddCorona (IsCorona),
**   - a corona is visible when a trace (MF_SOLID / ML_BLOCKEVERYTHING, the viewpoint actor is skipped) from the middle of the
**     corona to the viewer hits nothing; the fade of the corona moves towards 1 (visible) or 0 with 9 per second,
**     coronas farther away than their RenderRadius are skipped,
**   - the alpha is fade * actor alpha * distance fade (near 10 % and beyond 50 % of the RenderRadius),
**   - drawn after the entities as a camera facing quad of the sprite of the actor (its current frame, scaled by actor Scale),
**     without depth test or depth write (the trace is the occlusion test, the original has no depth based one), in the render style
**     of the actor (STYLE_Add by default).
** The original fade is a member of ACorona; here the renderer keeps it per actor.
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
#include <unordered_map>
#include "tarray.h"
#include "Common/Shared/RenderInfo.h"
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/Renderers/Legacy/World/Entities/CoronaProgram.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "OpenGL/VertexPipeline.h"

class AActor;
class PClassActor;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

class CoronaRenderer
{
public:
	// program from HelionProgramRegistry (ProgramType::Corona).
	CoronaRenderer(CoronaProgram* program, Texture::Legacy::LegacyGLTextureManager* textureManager);
	~CoronaRenderer();

	CoronaRenderer(const CoronaRenderer&) = delete;
	CoronaRenderer& operator=(const CoronaRenderer&) = delete;

	// thing->IsKindOf(NAME_Corona): the class does not exist in a game that does not define it, then nothing is a corona.
	bool IsCorona(AActor* thing) const;

	// HWSprite::Process: hands a corona actor (IsCorona) that is visible this frame to the renderer.
	void AddCorona(AActor* thing);

	// classic DrawCoronas: needs gl_coronas.
	void Render(Shared::RenderInfo& renderInfo);

	void Clear();

private:
	struct CoronaState
	{
		float Fade = 0;
		uint64_t LastFrame = 0;
	};

	struct DrawItem
	{
		Texture::Legacy::GLLegacyTexture* Texture;
		int Layer;
		float Alpha;
		AActor* Thing;
		int VertexIndex;
	};

	CoronaProgram* m_program;
	Texture::Legacy::LegacyGLTextureManager* m_textureManager;
	PClassActor* m_coronaClass;
	bool m_paletteColorMode;

	Buffer::Array::Vertex::DynamicVertexBuffer<CoronaVertex> m_vbo;
	std::unique_ptr<VertexPipeline<CoronaVertex>> m_pipeline;
	TArray<AActor*> m_coronas;
	std::unordered_map<AActor*, CoronaState> m_state;
	uint64_t m_lastFrameTime = 0;
	uint64_t m_frame = 0;

	bool ProcessCorona(AActor* corona, float timeElapsed, Shared::RenderInfo& renderInfo, DrawItem& item);
};

}
