/*
** PrimitiveWorldRenderer.h
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

#pragma once

#include <memory>
#include <vector>
#include "Common/Shared/RenderInfo.h"
#include "PrimitiveRenderType.h"
#include "PrimitiveShader.h"
#include "PrimitiveVbo.h"
#include "Util/Geometry.h"

namespace Helion::Geometry
{

// Helion.Geometry.Segments.Seg3F (Util/Geometry.h has the double precision Seg3D only).
struct Seg3F
{
	Vec3F Start;
	Vec3F End;
};

}

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Primitives
{

// Renders solid lines and triangles.
class PrimitiveWorldRenderer
{
public:
	bool HasOpaque = false;
	bool HasTransparent = false;

	PrimitiveWorldRenderer();
	~PrimitiveWorldRenderer();

	PrimitiveWorldRenderer(const PrimitiveWorldRenderer&) = delete;
	PrimitiveWorldRenderer& operator=(const PrimitiveWorldRenderer&) = delete;

	void AddSegment(const ::Helion::Geometry::Seg3F& segment, const ::Helion::Geometry::Vec3F& color, float alpha, PrimitiveRenderType type);
	void RenderAll(const Shared::RenderInfo& renderInfo);
	void RenderOpaque(const Shared::RenderInfo& renderInfo);
	void RenderTransparent(const Shared::RenderInfo& renderInfo);
	void Clear();

private:
	using Items = std::vector<std::unique_ptr<PrimitiveVbo>>;

	PrimitiveShader* m_program = nullptr;	// HelionProgramRegistry
	Items m_drawItems;
	Items m_drawItemsTransparent;

	void SetupVbo(Items& items);
	static void Clear(Items& items);
	void Render(const Shared::RenderInfo& renderInfo, Items& items, bool transparent);
};

}
