/*
** DrawWorldCommand.h
**
** Port of Helion.Render.OpenGL.Commands.Types.DrawWorldCommand.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** IWorld maps onto FLevelLocals*, Entity onto AActor*.
*/

#pragma once

#include "Common/Shared/OldCamera.h"
#include "Util/Geometry.h"

struct FLevelLocals;
class AActor;

namespace Helion::Render::OpenGL::Commands::Types
{

struct DrawWorldCommand
{
	const bool DrawAutomap;
	const int Gametick;
	const float GametickFraction;
	const double AutomapScale;
	const Geometry::Vec2I AutomapOffset;
	FLevelLocals* const World;
	Shared::OldCamera* const Camera;
	AActor* const ViewerEntity;

	DrawWorldCommand(FLevelLocals* world, Shared::OldCamera* camera, int gametick, float gametickFraction,
		AActor* viewerEntity, bool drawAutomap, const Geometry::Vec2I& automapOffset, double automapScale) :
		DrawAutomap(drawAutomap),
		Gametick(gametick),
		GametickFraction(gametickFraction),
		AutomapScale(automapScale),
		AutomapOffset(automapOffset),
		World(world),
		Camera(camera),
		ViewerEntity(viewerEntity)
	{
	}
};

}
