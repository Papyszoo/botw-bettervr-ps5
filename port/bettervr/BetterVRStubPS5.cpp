// SPDX-License-Identifier: GPL-3.0-or-later
// BotW-BetterVR on PS5, stage 1 of the BetterVR 1.0.0 port (docs/bettervr-ps5/UPDATE-1.0.md): the build
// on Crementif's Cemu fork has no BetterVR in it yet. These stand in for the four functions the
// 0.9.23 object (build/bettervr/bettervr.o, PS5CEMU_BETTERVR_LAYER) exports, so the app links and runs
// flat. Play in VR shows the TV's picture in the headset (port/bettervr/VrPS5.cpp, CEMU_PS5_NO_BETTERVR).

#include <vulkan/vk_layer.h>

extern "C"
{
	VkResult VRLayer_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface*)
	{
		return VK_ERROR_INITIALIZATION_FAILED; // VulkanLayerPS5.cpp logs it and goes without the layer
	}

	void BetterVR_PS5_Stop() {}

	void BetterVR_PS5_TvPresent() {}

	void BetterVR_PS5_MenuProbe(int) {}
}
