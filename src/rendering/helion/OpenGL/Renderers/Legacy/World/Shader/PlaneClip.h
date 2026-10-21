/*
** PlaneClip.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Shader.PlaneClip.
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

#include "zstring.h"
#include "FragFunction.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

enum class WallClipFragOptions
{
	None,
	AlphaSample,
	DiscardNegativeMapId
};

enum class PlaneClipFragOptions
{
	None,
	AlphaSample
};

class PlaneClip
{
public:
	static FString WritePlaneFragFunction(PlaneClipFragOptions options);
	static FString WriteWallFragFunction(WallClipFragOptions options);
	static FString GetTransparentDiscard(OitOptions options);
	static FString GetOutPlane(bool planeClip);
	static FString GetOutTargets(bool planeClip);

private:
	static FString GetDiscardNegativeMapId(bool discard);
	static FString GetAlphaSample(bool alphaSample);
};

}
