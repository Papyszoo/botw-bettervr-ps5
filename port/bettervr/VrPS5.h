// SPDX-License-Identifier: GPL-3.0-or-later
// BotW-BetterVR on PS5, M2 (docs/bettervr-ps5/PLAN.md): the TV's picture on a flat screen in the
// PS VR2 headset, through hmd2 (third_party/hmd2, from photo-splat-vr) in its external mode. No head
// tracking and no stereo yet: both eyes get the same picture, centred, and Hmd2 reprojects it with
// the pose it predicted for the frame, so it stays in front of the head.
//
// The switch is a file of its own, not a setting: /data/ps5cemu/bettervr.json (beside ps5cemu.json)
// with {"vr": true}, read once when a game first presents. Without it, or with "vr": false, nothing
// here runs and the game is played flat as before. While VR is on, frame generation and ReShade are
// off (patches/cemu 0048 skips them).
//
// Cemu's VulkanRenderer::SwapBuffer (patches/cemu 0048) calls it at each TV frame, on the GPU thread:
//   Record   once the TV's picture is in the swapchain image, before it is presented: waits for
//            Hmd2's frame slot, begins its frame, and records into Cemu's command buffer the scale of
//            the picture into both halves of the eye buffer Hmd2 handed out
//   Submit   after that command buffer has finished on the GPU: the flip to the headset
//   Shutdown before Cemu's device goes (its destructor), and after the GPU is idle
// A game ends with a LoadExec, not Cemu's destructor, so the app calls PS5Cemu_VrStop first
// (app/emulator.cpp's LeaveEndingToSystem): Hmd2 off from any thread, as a title that ends with
// VrTracker2 live faults the tracker (third_party/hmd2/hmd2.h). The calls share one lock.
// The three eye buffers are host-visible buffers on Cemu's own device, mapped, whose addresses Hmd2
// samples (third_party/hmd2/reference/eye_buffers.*, the path photo-splat-vr takes on the console).

#pragma once

#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

namespace ps5vr
{
	struct Frame
	{
		VkDevice device;
		VkPhysicalDevice physical;
		VkCommandBuffer commands; // the renderer's current one, outside a render pass
		VkImage image;			  // the TV's picture, about to be presented
		VkFormat format;
		VkExtent2D extent;
		VkImageLayout layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // image's, before and after
	};

	// Whether bettervr.json turns VR on (read once). False after the headset failed to start.
	bool Enabled();
	// True: a headset frame was begun and its copy recorded into frame.commands; call Submit once those
	// commands have run. False: nothing recorded (VR off, Hmd2 not ready, or no frame slot this time).
	bool Record(const Frame& frame);
	// The flip of the frame Record began; call only after the GPU finished frame.commands
	void Submit();
	// Hmd2 off and the eye buffers freed; the GPU must be idle
	void Shutdown();
	// Hmd2 off, from any thread, before the process is replaced; the eye buffers stay (the GPU may
	// still be writing one) and go with the process
	void Stop();
}
