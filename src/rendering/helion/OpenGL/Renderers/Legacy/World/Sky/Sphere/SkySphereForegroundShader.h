/*
** SkySphereForegroundShader.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkySphereForegroundShader.
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

#include "SkySphereShader.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

// The layer drawn over the sky (UZDoom: the front layer of a double sky).
class SkySphereForegroundShader : public SkySphereShader
{
public:
	SkySphereForegroundShader() : SkySphereShader("Sky foreground texture") { }

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
};

}
