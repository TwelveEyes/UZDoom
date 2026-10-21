/*
** PrimitiveWorldRenderer.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Primitives.PrimitiveWorldRenderer.
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

#include "PrimitiveWorldRenderer.h"
#include "v_video.h"
#include "OpenGL/Shader/ProgramRegistry.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Primitives
{

PrimitiveWorldRenderer::PrimitiveWorldRenderer()
{
	// HelionProgramRegistry owns the program (compiled at startup, plan D22).
	m_program = ::Helion::Render::OpenGL::Shader::HelionProgramRegistry::Get<PrimitiveShader>(::Helion::Render::OpenGL::Shader::ProgramType::Primitive);
	SetupVbo(m_drawItems);
	SetupVbo(m_drawItemsTransparent);
}

PrimitiveWorldRenderer::~PrimitiveWorldRenderer() = default;

void PrimitiveWorldRenderer::SetupVbo(Items& items)
{
	for (int value = 0; value < (int)PrimitiveRenderType::Count; value++)
	{
		int lineWidth = 2;
		if (value == (int)PrimitiveRenderType::Rail)
			lineWidth = 5;

		FString name;
		name.Format("Primitive %d", value);
		items.push_back(std::make_unique<PrimitiveVbo>(*m_program, name, lineWidth));
	}
}

void PrimitiveWorldRenderer::AddSegment(const ::Helion::Geometry::Seg3F& segment, const ::Helion::Geometry::Vec3F& color, float alpha, PrimitiveRenderType type)
{
	PrimitiveVertex start(segment.Start, color, alpha);
	PrimitiveVertex end(segment.End, color, alpha);

	bool transparent = alpha < 1;

	PrimitiveVbo& vboData = transparent ? *m_drawItemsTransparent[(int)type] : *m_drawItems[(int)type];
	vboData.Pipeline.Vbo.Add(start);
	vboData.Pipeline.Vbo.Add(end);
	if (transparent)
		HasTransparent = true;
	else
		HasOpaque = true;
}

void PrimitiveWorldRenderer::RenderAll(const Shared::RenderInfo& renderInfo)
{
	RenderOpaque(renderInfo);
	RenderTransparent(renderInfo);
}

void PrimitiveWorldRenderer::RenderOpaque(const Shared::RenderInfo& renderInfo)
{
	if (HasOpaque)
		Render(renderInfo, m_drawItems, false);
}

void PrimitiveWorldRenderer::RenderTransparent(const Shared::RenderInfo& renderInfo)
{
	if (HasTransparent)
		Render(renderInfo, m_drawItemsTransparent, true);
}

void PrimitiveWorldRenderer::Clear()
{
	if (HasOpaque)
		Clear(m_drawItems);
	if (HasTransparent)
		Clear(m_drawItemsTransparent);
	HasOpaque = false;
	HasTransparent = false;
}

void PrimitiveWorldRenderer::Clear(Items& items)
{
	for (auto& item : items)
		item->Pipeline.Clear();
}

void PrimitiveWorldRenderer::Render(const Shared::RenderInfo& renderInfo, Items& items, bool transparent)
{
	FRenderState* state = screen->RenderState();

	m_program->Bind();
	m_program->Mvp(renderInfo.Uniforms.Mvp);

	if (transparent)
		state->SetCustomBlend(RBF_SrcAlpha, RBF_OneMinusSrcAlpha, RBF_One, RBF_OneMinusSrcAlpha, RBE_Add, RBE_Add);
	else
		state->DisableBlend();

	float pixelWidth = renderInfo.Viewport.width > 0 ? 2.0f / renderInfo.Viewport.width : 0.0f;
	float pixelHeight = renderInfo.Viewport.height > 0 ? 2.0f / renderInfo.Viewport.height : 0.0f;

	for (auto& item : items)
	{
		if (item->Pipeline.Empty())
			continue;

		item->Pipeline.Vbo.UploadIfNeeded();
		item->Pipeline.Bind();

		// GL.LineWidth(item.LineWidth): the line is drawn once per pixel offset inside a disc of LineWidth diameter.
		int width = item->LineWidth;
		float radius = width * 0.5f;
		float center = (width - 1) * 0.5f;
		for (int y = 0; y < width; y++)
		{
			for (int x = 0; x < width; x++)
			{
				float offsetX = x - center;
				float offsetY = y - center;
				if (width > 2 && offsetX * offsetX + offsetY * offsetY > radius * radius)
					continue;

				m_program->LineOffset(::Helion::Geometry::Vec2F(offsetX * pixelWidth, offsetY * pixelHeight));
				item->Pipeline.DrawArrays(Util::PrimitiveType::Lines);
			}
		}

		item->Pipeline.Unbind();
	}

	m_program->Unbind();
}

}
