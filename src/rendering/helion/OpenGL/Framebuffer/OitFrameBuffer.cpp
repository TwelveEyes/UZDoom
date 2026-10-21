/*
** OitFrameBuffer.cpp
**
** Port of Helion.Render.OpenGL.Framebuffer.OitFrameBuffer.
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
#include "OitFrameBuffer.h"
#include "OpenGL/Shader/TextureUnits.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Framebuffer
{

void OitFrameBuffer::CreateOrUpdate(Geometry::Dimension dimension, const FramebufferTexture& opaqueDepthTexture)
{
	if (!opaqueDepthTexture.IsDepth() || !opaqueDepthTexture.IsScene())
		throw std::logic_error("The OIT framebuffer can only share the depth of the scene target");

	m_oitDimension = dimension;
	if (m_target != nullptr)
		return;

	RenderTargetDesc desc;
	desc.name = "OIT Framebuffer";
	desc.width = 0;		// scene size, the size of the depth buffer that is shared
	desc.height = 0;
	desc.colorFormats.Push(RTF_RGBA16F);	// accum
	desc.colorFormats.Push(RTF_RG16F);		// accum count
	desc.colorFormats.Push(RTF_R8);			// fuzz
	desc.shareSceneDepth = true;
	desc.ownDepth = false;

	m_target.reset(screen->CreateRenderTarget(desc));
	if (m_target == nullptr)
		throw std::runtime_error("Failed to complete oit framebuffer");
}

void OitFrameBuffer::StartRender()
{
	BindFrameBuffer();
	screen->RenderState()->ClearRenderTarget(0.0f, 0.0f, 0.0f, 0.0f);
	SetBlendEquations();
}

void OitFrameBuffer::SetBlendEquations()
{
	FRenderState* state = screen->RenderState();
	for (int attachment = 0; attachment < 3; attachment++)
		state->SetCustomBlendAttachment(attachment, RBF_One, RBF_One, RBF_One, RBF_One, RBE_Add, RBE_Add);
}

void OitFrameBuffer::BindFrameBuffer()
{
	screen->RenderState()->SetRenderTarget(m_target.get());
}

void OitFrameBuffer::BindTextures(TextureUnit accumTexture, TextureUnit accumCountTexture, TextureUnit fuzzTexture, TextureUnit opaqueTexture, const GLFramebuffer& framebuffer)
{
	using Shader::TextureUnits;

	TextureUnits::BindRenderTarget(accumTexture, m_target.get(), 0);
	TextureUnits::BindRenderTarget(accumCountTexture, m_target.get(), 1);
	TextureUnits::BindRenderTarget(fuzzTexture, m_target.get(), 2);
	framebuffer.ColorAttachment0.Bind(opaqueTexture);
}

}
