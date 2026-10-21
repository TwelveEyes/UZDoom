/*
** RenderCommands.h
**
** Port of Helion.Render.OpenGL.Commands.RenderCommands.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Only the commands of the 3D scene exist: Clear, World, Viewport and Scissor. The Image, Shape, Text,
** Hud, Transition, Automap and DrawVirtualFrameBuffer commands (and the virtual resolution translation
** of the 2D drawing that goes with them) are not ported: UZDoom draws all of that through F2DDrawer after
** the scene (plan D7). Config / FpsTracker / ImageDrawInfoProvider members are dropped for the same reason.
*/

#pragma once

#include <vector>
#include "Common/Commands/ResolutionInfo.h"
#include "Common/Commands/Types/ClearRenderCommand.h"
#include "Common/Commands/Types/DrawWorldCommand.h"
#include "Common/Commands/Types/ScissorCommand.h"
#include "Common/Commands/Types/ViewportCommand.h"
#include "Common/Shared/OldCamera.h"
#include "Util/Geometry.h"

struct FLevelLocals;
class AActor;

namespace Helion::Render::OpenGL::Commands
{

enum class RenderCommandType
{
	Clear,
	World,
	Viewport,
	Scissor
};

struct RenderCommand
{
	RenderCommandType Type;
	int Index;

	RenderCommand(RenderCommandType type, int index) : Type(type), Index(index)
	{
	}
};

class RenderCommands
{
public:
	Commands::ResolutionInfo ResolutionInfo;
	std::vector<RenderCommand> Commands;
	std::vector<Types::ClearRenderCommand> ClearCommands;
	std::vector<Types::DrawWorldCommand> WorldCommands;
	std::vector<Types::ViewportCommand> ViewportCommands;
	std::vector<Types::ScissorCommand> ScissorCommands;

	RenderCommands(Geometry::Dimension renderDimensions, Geometry::Dimension windowDimensions);

	Geometry::Dimension RenderDimension() const { return m_renderDimensions; }
	Geometry::Dimension WindowDimension() const { return m_windowDimensions; }

	void UpdateRenderDimension(Geometry::Dimension renderDimension, Geometry::Dimension windowDimension);

	void Begin();

	void Clear(Geometry::Color color, bool depth = false, bool stencil = false);
	void ClearDepth();

	void DrawWorld(FLevelLocals* world, Shared::OldCamera* camera, int gametick, float fraction, AActor* viewerEntity, bool drawAutomap,
		const Geometry::Vec2I& automapOffset, double automapScale);

	void Viewport(Geometry::Dimension dimension, Geometry::Vec2I offset = Geometry::Vec2I(0, 0));

	void Scissor(const Geometry::Box2I& box, Types::ScissorEnable enable);

private:
	Geometry::Dimension m_renderDimensions;
	Geometry::Dimension m_windowDimensions;
};

}
