/*
** GLWorldRenderContext.h
**
** Port of Helion.Render.OpenGL.GLWorldRenderContext.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Draw / DrawAutomap queue the world command. The debug drawing calls are "not implemented in the legacy
** renderer" in Helion as well and do nothing here. GetCameraPlayer() is the viewer actor handed to Begin().
*/

#pragma once

#include "Common/Commands/RenderCommands.h"
#include "Common/Context/WorldRenderContext.h"
#include "Common/Renderers/IWorldRenderContext.h"
#include "Common/Shared/OldCamera.h"

class AActor;

namespace Helion::Render::OpenGL
{

class GLWorldRenderContext : public Common::Renderers::IWorldRenderContext
{
public:
	explicit GLWorldRenderContext(Commands::RenderCommands& commands) : m_commands(commands) { }

	void Begin(Common::Context::WorldRenderContext* context, AActor* cameraPlayer)
	{
		m_context = context;
		m_cameraPlayer = cameraPlayer;
	}

	void Draw(FLevelLocals* world) override;
	void DrawAutomap(FLevelLocals* world) override { (void)world; /* The automap is drawn by UZDoom (plan D7). */ }

	void DrawLine(const Geometry::Seg3D&, Geometry::Color) override { }
	void DrawLines(std::span<const Geometry::Seg3D>, Geometry::Color) override { }
	void DrawRay(const Geometry::Ray3D&, Geometry::Color) override { }
	void DrawRays(std::span<const Geometry::Ray3D>, Geometry::Color) override { }
	void DrawTriangle(const Geometry::Triangle3D&, Geometry::Color) override { }
	void DrawTriangles(std::span<const Geometry::Triangle3D>, Geometry::Color) override { }
	void FillTriangle(const Geometry::Triangle3D&, Geometry::Color) override { }
	void FillTriangles(std::span<const Geometry::Triangle3D>, Geometry::Color) override { }
	void DrawQuad(const Geometry::Quad3D&, Geometry::Color) override { }
	void DrawQuads(std::span<const Geometry::Quad3D>, Geometry::Color) override { }
	void FillQuad(const Geometry::Quad3D&, Geometry::Color) override { }
	void FillQuads(std::span<const Geometry::Quad3D>, Geometry::Color) override { }
	void FillPlane(const Geometry::PlaneD&, Geometry::Color) override { }
	void FillPlanes(std::span<const Geometry::PlaneD>, Geometry::Color) override { }
	void DrawBox(const Geometry::Box3D&, Geometry::Color) override { }
	void DrawBoxes(std::span<const Geometry::Box3D>, Geometry::Color) override { }
	void FillBox(const Geometry::Box3D&, Geometry::Color) override { }
	void FillBoxes(std::span<const Geometry::Box3D>, Geometry::Color) override { }
	void DrawImage(const char*, const Geometry::Quad3D&, std::optional<Geometry::Color> = std::nullopt) override { }
	void DrawSurface(const char*, const Geometry::Quad3D&, std::optional<Geometry::Color> = std::nullopt) override { }

private:
	Shared::OldCamera m_oldCamera;
	Commands::RenderCommands& m_commands;
	Common::Context::WorldRenderContext* m_context = nullptr;
	AActor* m_cameraPlayer = nullptr;
};

}
