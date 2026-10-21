/*
** RenderCommands.cpp
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
*/

#include "Common/Commands/RenderCommands.h"

namespace Helion::Render::OpenGL::Commands
{

RenderCommands::RenderCommands(Geometry::Dimension renderDimensions, Geometry::Dimension windowDimensions) :
	m_renderDimensions(renderDimensions),
	m_windowDimensions(windowDimensions)
{
	ResolutionInfo.VirtualDimensions = renderDimensions;
}

void RenderCommands::UpdateRenderDimension(Geometry::Dimension renderDimension, Geometry::Dimension windowDimension)
{
	m_renderDimensions = renderDimension;
	m_windowDimensions = windowDimension;
	ResolutionInfo = Commands::ResolutionInfo();
	ResolutionInfo.VirtualDimensions = renderDimension;
}

void RenderCommands::Begin()
{
	Commands.clear();
	ClearCommands.clear();
	WorldCommands.clear();
	ViewportCommands.clear();
	ScissorCommands.clear();

	ResolutionInfo = Commands::ResolutionInfo();
	ResolutionInfo.VirtualDimensions = RenderDimension();
}

void RenderCommands::Clear(Geometry::Color color, bool depth, bool stencil)
{
	Commands.emplace_back(RenderCommandType::Clear, (int)ClearCommands.size());
	ClearCommands.emplace_back(true, depth, stencil, color);
}

void RenderCommands::ClearDepth()
{
	Commands.emplace_back(RenderCommandType::Clear, (int)ClearCommands.size());
	ClearCommands.push_back(Types::ClearRenderCommand::DepthOnly());
}

void RenderCommands::DrawWorld(FLevelLocals* world, Shared::OldCamera* camera, int gametick, float fraction, AActor* viewerEntity, bool drawAutomap,
	const Geometry::Vec2I& automapOffset, double automapScale)
{
	Commands.emplace_back(RenderCommandType::World, (int)WorldCommands.size());
	WorldCommands.emplace_back(world, camera, gametick, fraction, viewerEntity, drawAutomap, automapOffset, automapScale);
}

void RenderCommands::Viewport(Geometry::Dimension dimension, Geometry::Vec2I offset)
{
	Commands.emplace_back(RenderCommandType::Viewport, (int)ViewportCommands.size());
	ViewportCommands.emplace_back(dimension, offset);
}

void RenderCommands::Scissor(const Geometry::Box2I& box, Types::ScissorEnable enable)
{
	Commands.emplace_back(RenderCommandType::Scissor, (int)ScissorCommands.size());
	ScissorCommands.push_back({ box, enable });
}

}
