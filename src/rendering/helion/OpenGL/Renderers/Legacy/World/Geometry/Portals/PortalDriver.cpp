/*
** PortalDriver.cpp
**
** UZDoom-only (plan M5.2), see PortalDriver.h. Port of FPortalSceneState (StartFrame, EndFrame, RenderFirstSkyPortal,
** RenderPortal) and classic RenderPortal / FindPortal of hw_portal.cpp and hw_drawinfo.cpp.
**
**---------------------------------------------------------------------------
**
** Copyright 2004-2018 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include "PortalDriver.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalStencilProgram.h"
#include "Common/Shared/RenderInfo.h"
#include "OpenGL/Shader/ProgramRegistry.h"
#include "g_levellocals.h"
#include "hw_cvars.h"
#include "hw_renderstate.h"
#include "r_utility.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

PortalDriver::PortalDriver(Data::SortedTranslucentRenderer* sortedTranslucent) : m_sorted(sortedTranslucent)
{
	m_stencilVbo = std::make_unique<Buffer::Array::Vertex::StreamVertexBuffer<PortalStencilVertex>>("Portal stencil");
	s_instance = this;
}

PortalDriver::~PortalDriver()
{
	if (s_instance == this)
		s_instance = nullptr;
}

void PortalDriver::BeginScene()
{
	m_uniqueHorizons.clear();
	m_uniquePlaneMirrors.clear();
}

void PortalDriver::StartFrame(const PortalFrameView& view)
{
	if (m_state.RenderDepth == 0)
	{
		m_state.InSkybox = false;
		screen->instack[sector_t::floor] = screen->instack[sector_t::ceiling] = 0;
	}
	m_state.RenderDepth++;
	m_lastFrame.reset();

	auto frame = std::make_unique<Frame>();
	frame->View = view;
	frame->Viewpoint = std::make_unique<FRenderViewpoint>(*view.Viewpoint);
	frame->View.Viewpoint = frame->Viewpoint.get();
	if (view.Clip != nullptr)
	{
		frame->Clip = std::make_unique<PortalClip>(*view.Clip);
		frame->View.Clip = frame->Clip.get();
	}
	// classic mCurrentPortal: the portal that is being rendered when this view is started.
	frame->View.CurrentPortal = m_active.empty() ? nullptr : m_active.back();
	if (frame->View.Level == nullptr)
		frame->View.Level = m_level;
	m_frames.push_back(std::move(frame));
}

bool PortalDriver::HasPortals() const
{
	if (m_frames.empty())
		return false;
	for (const auto& portal : m_frames.back()->Portals)
	{
		if (portal->Lines.size() > 0)
			return true;
	}
	return false;
}

bool PortalDriver::EndFrame()
{
	Frame& frame = *m_frames.back();
	bool rendered = false;

	// The portals are drawn from the back (the last collected portal first), like classic Portals.Pop.
	while (!frame.Portals.empty())
	{
		std::unique_ptr<Portal> portal = std::move(frame.Portals.back());
		frame.Portals.pop_back();
		if (portal->Lines.size() > 0)
		{
			RenderPortal(portal.get(), true);
			rendered = true;
		}
	}
	m_state.RenderDepth--;
	m_lastFrame = std::move(m_frames.back());
	m_frames.pop_back();

	if (m_state.RenderDepth == 0)
		m_stencilVbo->Clear();
	return rendered;
}

// Renders one sky portal without a stencil.
// In more complex scenes using a stencil for skies can severely stall
// the GPU and there's rarely more than one sky visible at a time.
bool PortalDriver::RenderFirstSkyPortal(int recursion)
{
	if (m_frames.empty())
		return false;

	Frame& frame = *m_frames.back();
	const FRenderViewpoint& vp = *frame.View.Viewpoint;
	int bestIndex = -1;
	bool useStencil = vp.bDoOob;

	// Find the one with the highest amount of lines.
	// Normally this is also the one that saves the largest amount
	// of time by drawing it before the scene itself.
	auto& portals = frame.Portals;
	for (int i = (int)portals.size() - 1; i >= 0; --i)
	{
		Portal* p = portals[i].get();
		if (p->Lines.size() > 0 && p->IsSky())
		{
			// Cannot clear the depth buffer inside a portal recursion
			if (recursion && p->NeedDepthBuffer()) continue;

			if (bestIndex < 0 || p->Lines.size() > portals[bestIndex]->Lines.size())
			{
				bestIndex = i;
			}

			// If the portal area contains the current camera viewpoint, let's always use it because it's likely to give the largest area.
			if (p->BoundingBox.contains(useStencil ? vp.OffPos : vp.Pos))
			{
				bestIndex = i;
				break;
			}
		}
	}

	if (bestIndex >= 0)
	{
		std::unique_ptr<Portal> best = std::move(portals[bestIndex]);
		portals.erase(portals.begin() + bestIndex);
		RenderPortal(best.get(), useStencil);
		return true;
	}
	return false;
}

void PortalDriver::RenderPortal(Portal* portal, bool useStencil)
{
	if (!gl_portals)
		return;

	Frame& frame = *m_frames.back();
	const PortalFrameView& outer = frame.View;
	FRenderState& state = *screen->RenderState();

	if (useStencil)
	{
		// The level sky leaves its marks in the stencil buffer; portals count from zero.
		state.EnableStencil(true);
		if (screen->stencilValue == 0)
			state.Clear(CT_Stencil);
	}

	portal->SetupStencil(*this, outer, useStencil);

	PortalClip clip;
	clip.Type = portal->Type();
	clip.UsedStencil = useStencil;
	clip.ThickFogScale = outer.Clip != nullptr ? outer.Clip->ThickFogScale : 1.0f;
	clip.PortalDepth = m_state.RenderDepth;
	// HWPortalState::RenderPortal: orthographic views draw skybox portals with the perspective projection.
	clip.PerspectiveProjection = outer.Viewpoint->bDoOrtho && portal->Type() == PortalType::Skybox;
	clip.MapSections.Resize(outer.Level->NumMapSections);
	clip.MapSections.Zero();

	m_active.push_back(portal);
	FRenderViewpoint vp = *outer.Viewpoint;
	portal->DrawContents(*this, outer, vp, clip);
	m_active.pop_back();

	// state.SetVertexBuffer(...), screen->mViewpoints->Bind(state, vpIndex)
	if (Hooks.RestoreView)
		Hooks.RestoreView(outer);

	portal->RemoveStencil(*this, outer, useStencil);

	if (useStencil && screen->stencilValue == 0)
		state.EnableStencil(false);
}

void PortalDriver::UnbindProgram()
{
	if (auto* current = ::Helion::Render::OpenGL::Shader::RenderProgram::Current())
		current->Unbind();
}

Portal* PortalDriver::FindPortal(const void* source)
{
	if (m_frames.empty())
		return nullptr;

	auto& portals = m_frames.back()->Portals;
	for (int i = (int)portals.size() - 1; i >= 0; i--)
	{
		if (portals[i] && portals[i]->GetSource() == source)
			return portals[i].get();
	}
	return nullptr;
}

void PortalDriver::Add(std::unique_ptr<Portal> portal)
{
	m_frames.back()->Portals.push_back(std::move(portal));
}

//-----------------------------------------------------------------------------
//
// Stencil geometry
//
//-----------------------------------------------------------------------------

void PortalDriver::AddStencilTriangle(const float a[3], const float b[3], const float c[3])
{
	AddStencilVertex(a[0], a[1], a[2]);
	AddStencilVertex(b[0], b[1], b[2]);
	AddStencilVertex(c[0], c[1], c[2]);
}

void PortalDriver::AddStencilQuad(const float a[3], const float b[3], const float c[3], const float d[3])
{
	AddStencilTriangle(a, b, c);
	AddStencilTriangle(a, c, d);
}

void PortalDriver::EnsureStencilProgram()
{
	using ::Helion::Render::OpenGL::Shader::HelionProgramRegistry;
	using ::Helion::Render::OpenGL::Shader::ProgramType;
	PortalStencilProgram* program = HelionProgramRegistry::Get<PortalStencilProgram>(ProgramType::PortalStencil);
	if (program != m_stencilProgram || m_stencilPipeline == nullptr)
	{
		m_stencilProgram = program;
		m_stencilPipeline = std::make_unique<VertexPipeline<PortalStencilVertex>>(*program, *m_stencilVbo, "Portal stencil");
	}
}

void PortalDriver::BeginStencilDraw(const PortalFrameView& view)
{
	EnsureStencilProgram();
	// The stencil geometry has no consistent winding.
	screen->RenderState()->SetCulling(Cull_None);
	m_stencilProgram->Bind();
	m_stencilProgram->SetMvp(view.Mvp);
	m_stencilVbo->UploadIfNeeded();
	m_stencilPipeline->Bind();
}

void PortalDriver::DrawStencilRange(int first, int count)
{
	if (count <= 0)
		return;
	m_stencilPipeline->DrawArrays(Util::PrimitiveType::Triangles, first, count);
}

void PortalDriver::EndStencilDraw(const PortalFrameView& view)
{
	m_stencilProgram->Unbind();
	screen->RenderState()->SetCulling(view.Culling);
}

void PortalDriver::NestedStencilPass(const std::function<void()>& drawMask, const std::function<void()>& drawSky)
{
	FRenderState& state = *screen->RenderState();
	state.EnableStencil(true);

	// Mark the pixels of the mask that are inside of the current stencil value (the portal area).
	state.SetStencil(0, SOP_Increment, SF_ColorMaskOff);
	state.SetColorMask(false, false, false, false);
	drawMask();
	state.SetColorMask(true, true, true, true);
	screen->stencilValue++;

	// Draw where the mask is, without depth test.
	state.SetStencil(0, SOP_Keep, SF_AllOn);
	state.EnableDepthTest(false);
	drawSky();
	state.EnableDepthTest(true);

	// Give the portal its stencil value back: decrement the marked pixels (the same geometry passes the LEqual depth test again).
	state.SetColorMask(false, false, false, false);
	state.SetDepthMask(false);
	state.SetDepthFunc(DF_LEqual);
	state.SetStencil(0, SOP_Decrement, SF_AllOn);
	drawMask();
	screen->stencilValue--;
	state.SetStencil(0, SOP_Keep, SF_AllOn);
	state.SetDepthFunc(DF_Less);
	state.SetDepthMask(true);
	state.SetColorMask(true, true, true, true);
}

}
