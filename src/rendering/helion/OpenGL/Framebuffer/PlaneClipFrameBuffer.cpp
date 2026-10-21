/*
** PlaneClipFrameBuffer.cpp
**
** Port of Helion.Render.OpenGL.Framebuffer.PlaneClipFrameBuffer.
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

#include <stdexcept>
#include "PlaneClipFrameBuffer.h"
#include "OpenGL/Shader/TextureUnits.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Framebuffer
{

PlaneClipFrameBuffer::~PlaneClipFrameBuffer() = default;

void PlaneClipFrameBuffer::CreateOrUpdate(const FString& name, PlaneClipType type, Geometry::Dimension dimension, const GLFramebuffer* colorFramebuffer, bool forceCreate)
{
	(void)colorFramebuffer;	// see the file comment in the header

	if (!ShouldRecreate(dimension, forceCreate))
		return;

	DeleteData();

	m_dimension = dimension;

	RenderTargetDesc desc;
	desc.name = name;
	desc.width = dimension.Width;
	desc.height = dimension.Height;
	desc.colorFormats.Push(type == PlaneClipType::Wall ? RTF_RGBA16F : RTF_RGBA32F);
	desc.shareSceneDepth = false;
	desc.ownDepth = true;

	m_target.reset(screen->CreateRenderTarget(desc));
	if (m_target == nullptr)
		throw std::runtime_error("Failed to complete PlaneClip framebuffer");
}

bool PlaneClipFrameBuffer::ShouldRecreate(Geometry::Dimension dimension, bool force) const
{
	if (force)
		return true;

	return m_target == nullptr || dimension != m_dimension;
}

void PlaneClipFrameBuffer::Clear()
{
	FRenderState* state = screen->RenderState();
	state->Clear(CT_Depth);
	state->ClearRenderTarget(-255.0f, 1e30f, -255.0f, 1e30f);
}

void PlaneClipFrameBuffer::StartRender()
{
	screen->RenderState()->SetCustomBlend(RBF_One, RBF_Zero, RBF_One, RBF_Zero, RBE_Add, RBE_Add);
}

void PlaneClipFrameBuffer::BindPlaneTexture(TextureUnit textureUnit)
{
	Shader::TextureUnits::BindRenderTarget(textureUnit, m_target.get(), 0);
}

void PlaneClipFrameBuffer::BindFrameBuffer()
{
	screen->RenderState()->SetRenderTarget(m_target.get());
}

void PlaneClipFrameBuffer::UnbindFrameBuffer()
{
	screen->RenderState()->SetRenderTarget(nullptr);
}

void PlaneClipFrameBuffer::DeleteData()
{
	m_target.reset();
}

}
