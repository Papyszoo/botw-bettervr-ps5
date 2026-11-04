// SPDX-License-Identifier: GPL-3.0-or-later
// BotW-BetterVR on PS5, M3: the Vulkan loader's part for BetterVR's layer. The PS5 has no loader:
// Cemu takes every Vulkan function from RADV's vkGetInstanceProcAddr (port/cemu/VulkanPS5.cpp,
// PS5_VulkanGetGlobalProc). That one now comes from here, and does what a loader does with one
// implicit layer, for the instance Breath of the Wild's renderer creates while BetterVR is wanted
// (BetterVR_PS5_LayerWanted, port/bettervr/VrPS5.cpp):
//
//   vkCreateInstance  the layer's, with the loader's link info in the chain (VK_LAYER_LINK_INFO, the
//                     next vkGetInstanceProcAddr being RADV's)
//   vkCreateDevice    the layer's, with its link info (RADV's vkGetInstanceProcAddr and
//                     vkGetDeviceProcAddr)
//   anything else     for that instance, the layer's vkGetInstanceProcAddr (it hands out its
//                     vkGetDeviceProcAddr, which every device function then comes through)
//
// Every other instance (another game's, the 3DS side's) takes RADV's functions as before, so
// nothing changes there. The layer is BetterVR's (vkroots), in BetterVR's object
// (port/bettervr/layer); vkroots keys its tables by the handles themselves, so it needs nothing of
// a loader's dispatch pointers.

#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include <vulkan/vk_layer.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

void PS5Cemu_LogLine(std::string_view line);
extern "C" bool BetterVR_PS5_LayerWanted();
// BetterVR's object: the layer's entry point for a loader
extern "C" VkResult VRLayer_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct);

namespace ps5bettervr
{
	namespace
	{
		PFN_vkGetInstanceProcAddr s_driver = nullptr; // RADV's
		PFN_vkGetInstanceProcAddr s_layerInstance = nullptr;
		PFN_vkGetDeviceProcAddr s_layerDevice = nullptr;
		std::mutex s_lock;
		VkInstance s_instance = VK_NULL_HANDLE; // the one instance created through the layer

		// What Cemu's calls reach, for the boot log (m5-botw-vr's first VR start had a green screen, and
		// no sign that BetterVR's hooks ever ran): its vkGetDeviceProcAddr, and its color clears, the
		// call that starts VR in BetterVR (hooking/framebuffer.cpp)
		PFN_vkGetDeviceProcAddr s_layerGetDeviceProcAddr = nullptr;
		PFN_vkCmdClearColorImage s_layerClear = nullptr;
		std::atomic<int> s_clears{0}, s_magicClears{0};
		std::atomic<bool> s_deviceProcLogged{false};

		bool Magic(const VkClearColorValue* color)
		{
			// BetterVR's graphic pack marks an eye's clear: green about 0.125 and blue about 0.98, or swapped
			const float g = color->float32[1], b = color->float32[2];
			return (g >= 0.12f && g <= 0.13f && b >= 0.97f && b <= 0.99f) || (b >= 0.12f && b <= 0.13f && g >= 0.97f && g <= 0.99f);
		}

		VKAPI_ATTR void VKAPI_CALL ClearSpy(VkCommandBuffer commands, VkImage image, VkImageLayout layout, const VkClearColorValue* color,
			uint32_t count, const VkImageSubresourceRange* ranges)
		{
			if (s_clears.fetch_add(1) == 0)
				PS5Cemu_LogLine("[vr] Cemu's first vkCmdClearColorImage, on its way to BetterVR's layer");
			if (color && Magic(color) && s_magicClears.fetch_add(1) < 3)
				PS5Cemu_LogLine("[vr] Cemu clears with BetterVR's eye colour (" + std::to_string(color->float32[0]) + ", " +
					std::to_string(color->float32[1]) + ", " + std::to_string(color->float32[2]) + ", " + std::to_string(color->float32[3]) +
					"), " + std::to_string(s_clears.load()) + " clears so far");
			s_layerClear(commands, image, layout, color, count, ranges);
		}

		VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* name)
		{
			if (!s_deviceProcLogged.exchange(true))
				PS5Cemu_LogLine(std::string("[vr] Cemu takes its device functions from BetterVR's layer (the first: ") + name + ")");
			const PFN_vkVoidFunction function = s_layerGetDeviceProcAddr(device, name);
			if (function && std::strcmp(name, "vkCmdClearColorImage") == 0)
			{
				s_layerClear = reinterpret_cast<PFN_vkCmdClearColorImage>(function);
				return reinterpret_cast<PFN_vkVoidFunction>(&ClearSpy);
			}
			return function;
		}

		bool Negotiate()
		{
			if (s_layerInstance)
				return true;
			VkNegotiateLayerInterface negotiate{};
			negotiate.sType = LAYER_NEGOTIATE_INTERFACE_STRUCT;
			negotiate.loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION;
			const VkResult result = VRLayer_NegotiateLoaderLayerInterfaceVersion(&negotiate);
			if (result != VK_SUCCESS || !negotiate.pfnGetInstanceProcAddr)
			{
				PS5Cemu_LogLine("[vr] BetterVR's Vulkan layer did not negotiate (" + std::to_string((int)result) + "): Cemu goes without it");
				return false;
			}
			s_layerInstance = negotiate.pfnGetInstanceProcAddr;
			s_layerDevice = negotiate.pfnGetDeviceProcAddr;
			return true;
		}

		VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* allocator, VkInstance* instance)
		{
			auto driverCreate = (PFN_vkCreateInstance)s_driver(VK_NULL_HANDLE, "vkCreateInstance");
			if (!BetterVR_PS5_LayerWanted())
				return driverCreate(info, allocator, instance);
			std::lock_guard lock(s_lock);
			if (s_instance != VK_NULL_HANDLE || !Negotiate())
				return driverCreate(info, allocator, instance);
			VkLayerInstanceLink link{};
			link.pfnNextGetInstanceProcAddr = s_driver;
			VkLayerInstanceCreateInfo chain{};
			chain.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
			chain.pNext = info->pNext;
			chain.function = VK_LAYER_LINK_INFO;
			chain.u.pLayerInfo = &link;
			VkInstanceCreateInfo withLayer = *info;
			withLayer.pNext = &chain;
			auto layerCreate = (PFN_vkCreateInstance)s_layerInstance(VK_NULL_HANDLE, "vkCreateInstance");
			const VkResult result = layerCreate(&withLayer, allocator, instance);
			if (result == VK_SUCCESS)
			{
				s_instance = *instance;
				PS5Cemu_LogLine("[vr] Cemu's Vulkan instance goes through BetterVR's layer");
			}
			else
				PS5Cemu_LogLine("[vr] BetterVR's layer did not create the instance (" + std::to_string((int)result) + ")");
			return result;
		}

		VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info, const VkAllocationCallbacks* allocator,
			VkDevice* device)
		{
			VkLayerDeviceLink link{};
			link.pfnNextGetInstanceProcAddr = s_driver;
			link.pfnNextGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)s_driver(s_instance, "vkGetDeviceProcAddr");
			VkLayerDeviceCreateInfo chain{};
			chain.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
			chain.pNext = info->pNext;
			chain.function = VK_LAYER_LINK_INFO;
			chain.u.pLayerInfo = &link;
			VkDeviceCreateInfo withLayer = *info;
			withLayer.pNext = &chain;
			auto layerCreate = (PFN_vkCreateDevice)s_layerInstance(s_instance, "vkCreateDevice");
			const VkResult result = layerCreate(physical, &withLayer, allocator, device);
			PS5Cemu_LogLine("[vr] Cemu's Vulkan device through BetterVR's layer: " + std::to_string((int)result));
			return result;
		}

		VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator)
		{
			auto layerDestroy = (PFN_vkDestroyInstance)s_layerInstance(instance, "vkDestroyInstance");
			{
				std::lock_guard lock(s_lock);
				if (instance == s_instance)
					s_instance = VK_NULL_HANDLE;
			}
			layerDestroy(instance, allocator);
		}

		VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* name)
		{
			if (std::strcmp(name, "vkGetInstanceProcAddr") == 0)
				return reinterpret_cast<PFN_vkVoidFunction>(&GetInstanceProcAddr);
			if (std::strcmp(name, "vkCreateInstance") == 0)
				return reinterpret_cast<PFN_vkVoidFunction>(&CreateInstance);
			bool layered;
			{
				std::lock_guard lock(s_lock);
				layered = instance != VK_NULL_HANDLE && instance == s_instance;
			}
			if (!layered)
			{
				if (instance != VK_NULL_HANDLE && std::strcmp(name, "vkGetDeviceProcAddr") == 0 && BetterVR_PS5_LayerWanted())
					PS5Cemu_LogLine("[vr] vkGetDeviceProcAddr asked of an instance without BetterVR's layer: Cemu's device would bypass it");
				return s_driver(instance, name);
			}
			if (std::strcmp(name, "vkCreateDevice") == 0)
				return reinterpret_cast<PFN_vkVoidFunction>(&CreateDevice);
			if (std::strcmp(name, "vkDestroyInstance") == 0)
				return reinterpret_cast<PFN_vkVoidFunction>(&DestroyInstance);
			if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
			{
				s_layerGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(s_layerInstance(instance, name));
				return s_layerGetDeviceProcAddr ? reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr) : nullptr;
			}
			return s_layerInstance(instance, name);
		}
	}

	// port/cemu/VulkanPS5.cpp: the vkGetInstanceProcAddr Cemu is given, over the driver's
	PFN_vkGetInstanceProcAddr Wrap(PFN_vkGetInstanceProcAddr driver)
	{
		s_driver = driver;
		return &GetInstanceProcAddr;
	}
}
