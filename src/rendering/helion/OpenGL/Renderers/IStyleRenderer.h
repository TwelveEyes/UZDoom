/*
** IStyleRenderer.h
**
** Port of Helion.Render.OpenGL.Renderers.IStyleRenderer.
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

#include "OpenGL/Renderers/Legacy/World/Data/RenderDataStyle.h"

namespace Helion::Render::OpenGL::Renderers
{

class IStyleRenderer
{
public:
	virtual ~IStyleRenderer() = default;

	virtual void Render(Legacy::World::Data::RenderDataStyle style) = 0;
	virtual bool HasStyleToRender(Legacy::World::Data::RenderDataStyle style) = 0;
	virtual bool HasAlphaToRender() = 0;
	virtual void RenderAllAlpha() = 0;
};

class StyleRendererBase : public IStyleRenderer
{
public:
	bool HasAlphaToRender() override
	{
		using Legacy::World::Data::RenderDataStyle;
		if (HasStyleToRender(RenderDataStyle::FogBarrier))
			return true;

		for (RenderDataStyle style : AlphaStyles)
		{
			if (HasStyleToRender(style))
				return true;
		}

		return false;
	}

	void RenderAllAlpha() override
	{
		for (Legacy::World::Data::RenderDataStyle style : AlphaStyles)
			Render(style);
	}

private:
	static constexpr Legacy::World::Data::RenderDataStyle AlphaStyles[] = {
		Legacy::World::Data::RenderDataStyle::Translucent,
		Legacy::World::Data::RenderDataStyle::Add,
		Legacy::World::Data::RenderDataStyle::ColorAdd };
};

}
