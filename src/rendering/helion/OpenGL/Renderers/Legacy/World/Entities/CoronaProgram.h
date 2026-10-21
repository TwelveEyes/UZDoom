/*
** CoronaProgram.h
**
** UZDoom-only (plan M4.1): the program of CoronaRenderer.
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

#include <span>
#include "tarray.h"
#include "OpenGL/Renderers/Legacy/World/RenderProgramBase.h"
#include "OpenGL/Vertex/VertexAttributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

// C# StructLayout(LayoutKind.Sequential, Pack = 1)
#pragma pack(push, 1)
struct CoronaVertex
{
	// [VertexAttribute("pos", size: 2)] normalized device coordinates
	float X;
	float Y;

	// [VertexAttribute("uv", size: 2)]
	float U;
	float V;

	static std::span<const Vertex::VertexAttributeAttribute> VertexAttributes()
	{
		static constexpr Vertex::VertexAttributeAttribute attributes[] =
		{
			{ "pos", Vertex::VertexAttributeAttribute::InferFromField, 2 },
			{ "uv", Vertex::VertexAttributeAttribute::InferFromField, 2 },
		};
		return attributes;
	}
};
#pragma pack(pop)

static_assert(sizeof(CoronaVertex) == 4 * sizeof(float), "CoronaVertex has to stay tightly packed");

// A screen space textured quad: fullbright sprite texture (array texture, palette or truecolor) times alpha.
// Create with RenderProgram::Create<CoronaProgram>() (HelionProgramRegistry ProgramType::Corona).
class CoronaProgram : public RenderProgramBase
{
public:
	CoronaProgram();

	// Layer of the array texture (GLLegacyTexture::ArrayIndex).
	void CoronaLayer(int layer);
	void CoronaAlpha(float alpha);

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;
	void ProgramCreated() override;

private:
	int m_layerLocation = -1;
	int m_alphaLocation = -1;
};

}
