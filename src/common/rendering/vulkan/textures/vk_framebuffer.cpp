/*
** vk_framebuffer.cpp
**
** Vulkan backend
**
**---------------------------------------------------------------------------
**
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Copyright 2016-2020 Magnus Norddahl
**
** SPDX-License-Identifier: Zlib
**
**---------------------------------------------------------------------------
**
*/

#include <zvulkan/vulkanobjects.h>
#include <zvulkan/vulkandevice.h>
#include <zvulkan/vulkanbuilders.h>
#include <zvulkan/vulkanswapchain.h>
#include "vulkan/system/vk_renderdevice.h"
#include "vulkan/renderer/vk_postprocess.h"
#include "vk_framebuffer.h"
#include "printf.h"

CVAR(Bool, vk_hdr, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR(Bool, vk_exclusivefullscreen, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);

VkFramebufferManager::VkFramebufferManager(VulkanRenderDevice* fb) : fb(fb)
{
	SwapChain = VulkanSwapChainBuilder()
		.Create(fb->device.get());

	SwapChainImageAvailableSemaphore = SemaphoreBuilder()
		.DebugName("SwapChainImageAvailableSemaphore")
		.Create(fb->device.get());
}

VkFramebufferManager::~VkFramebufferManager()
{
}

void VkFramebufferManager::AcquireImage()
{
	bool exclusiveFullscreen = fb->IsFullscreen() && vk_exclusivefullscreen;

	// The swapchain extent is clamped to the surface's maximum extent, which on
	// X11 is the window's actual geometry when the swapchain is created. A window
	// manager can map the window smaller than requested and grow it later (e.g.
	// Hyprland's initial floating size) without the client size the engine reads
	// ever changing, so the swapchain would stay at the clamped size for good and
	// the frame would cover only the top-left of the window. While the swapchain
	// is smaller than the client area, retry the creation every few frames until
	// the surface allows the full size.
	bool clamped = SwapChain->Width() > 0 && (SwapChain->Width() != CurrentWidth || SwapChain->Height() != CurrentHeight);
	bool retryClamped = clamped && ++ClampedRetryFrames >= 30;
	if (!clamped) ClampedRetryFrames = 0;

	if (retryClamped || SwapChain->Lost() || fb->GetClientWidth() != CurrentWidth || fb->GetClientHeight() != CurrentHeight || fb->GetVSync() != CurrentVSync || CurrentHdr != vk_hdr || CurrentExclusiveFullscreen != exclusiveFullscreen)
	{
		ClampedRetryFrames = 0;
		Framebuffers.clear();

		CurrentWidth = fb->GetClientWidth();
		CurrentHeight = fb->GetClientHeight();
		CurrentVSync = fb->GetVSync();
		CurrentHdr = vk_hdr;
		CurrentExclusiveFullscreen = exclusiveFullscreen;

		SwapChain->Create(CurrentWidth, CurrentHeight, CurrentVSync ? 2 : 3, CurrentVSync, CurrentHdr, CurrentExclusiveFullscreen);

		if (SwapChain->Width() > 0 && (SwapChain->Width() != CurrentWidth || SwapChain->Height() != CurrentHeight))
		{
			if (!ClampedReported)
				Printf("Vulkan swapchain clamped to %dx%d (window client area %dx%d); retrying\n", SwapChain->Width(), SwapChain->Height(), CurrentWidth, CurrentHeight);
			ClampedReported = true;
		}
		else if (ClampedReported)
		{
			Printf("Vulkan swapchain resized to %dx%d\n", SwapChain->Width(), SwapChain->Height());
			ClampedReported = false;
		}

		RenderFinishedSemaphores.clear();
		for (int i = 0; i < SwapChain->ImageCount(); i++)
		{
			RenderFinishedSemaphores.push_back(SemaphoreBuilder()
				.DebugName("RenderFinishedSemaphore")
				.Create(fb->device.get()));
		}
	}

	PresentImageIndex = SwapChain->AcquireImage(SwapChainImageAvailableSemaphore.get());
	if (PresentImageIndex != -1)
	{
		// The letterbox is sized from the window client area, which can be larger than the
		// swapchain: the X11/Vulkan surface may not support the requested size, in which case
		// the swapchain is clamped to its maximum extent. Keep the present viewport within the
		// swapchain or the overflowing edges get clipped to black.
		IntRect presentBox = fb->mOutputLetterbox;
		if (presentBox.left < 0)
		{
			presentBox.width += presentBox.left;
			presentBox.left = 0;
		}
		if (presentBox.top < 0)
		{
			presentBox.height += presentBox.top;
			presentBox.top = 0;
		}
		if (presentBox.width > SwapChain->Width() - presentBox.left)
			presentBox.width = SwapChain->Width() - presentBox.left;
		if (presentBox.height > SwapChain->Height() - presentBox.top)
			presentBox.height = SwapChain->Height() - presentBox.top;
		fb->GetPostprocess()->DrawPresentTexture(presentBox, true, false);
	}
}

void VkFramebufferManager::QueuePresent()
{
	if (PresentImageIndex != -1)
		SwapChain->QueuePresent(PresentImageIndex, RenderFinishedSemaphores[PresentImageIndex].get());
}
