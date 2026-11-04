// SPDX-License-Identifier: GPL-3.0-or-later
#include "VrPS5.h"
#include "../app/paths.h"
#include "../app/own_status.h"
#include "hmd2.h"

#include <fmt/format.h>
#include <rapidjson/document.h>

#include <cstdio>
#include <filesystem>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

// Cemu's (Cafe/CafeSystem.h, which needs Cemu's precompiled header): the game being started
namespace CafeSystem { uint64_t GetForegroundTitleId(); }
// the port's (ps5/log.cpp): a line of the boot log
void PS5Cemu_LogLine(std::string_view line);

namespace ps5vr
{
	namespace
	{
		// photo-splat-vr's eye size on the console (its vr_mode.cpp), the size Hmd2 reprojects
		constexpr uint32_t kEyeWidth = 1984;
		constexpr uint32_t kEyeHeight = 2048;
		constexpr int kBuffers = 3;
		// the TV's picture across this much of each eye's width, centred, its own shape kept
		constexpr float kScreenWidth = 0.6f;
		constexpr uint32_t kLogEvery = 600; // headset frames between pacing lines

		struct State
		{
			bool checked = false; // bettervr.json read
			bool enabled = false; // VR asked for and not failed
			bool up = false;	  // Hmd2 running, the buffers made
			bool open = false;	  // a frame Record began, not submitted yet
			VkDevice device = VK_NULL_HANDLE;
			VkBuffer buffers[kBuffers] = {};
			VkDeviceMemory memory[kBuffers] = {};
			void* mapped[kBuffers] = {};
			VkImage scaled = VK_NULL_HANDLE; // one eye's picture, scaled, before it goes into both halves
			VkDeviceMemory scaledMemory = VK_NULL_HANDLE;
			VkFormat scaledFormat = VK_FORMAT_UNDEFINED;
			bool scaledDefined = false;
			int buffer = 0; // the one this frame writes
			uint64_t frames = 0, skipped = 0;
		} s;
		std::mutex s_lock;

		void Log(const std::string& line)
		{
			PS5Cemu_LogLine("[vr] " + line);
		}

		void HmdLog(const char* line)
		{
			PS5Cemu_LogLine(std::string("[vr] ") + line);
		}

		const char* Name(VkResult result)
		{
			switch (result)
			{
			case VK_SUCCESS: return "VK_SUCCESS";
			case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
			case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
			case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
			case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
			default: return "a VkResult";
			}
		}

		std::string Error(const char* call, VkResult result)
		{
			return fmt::format("{} -> {} ({})", call, Name(result), (int)result);
		}

		enum class Mode
		{
			Off,
			Flat,	 // M2: the TV's picture on a screen in the headset (this file)
			BetterVR // M3: Breath of the Wild in stereo through BetterVR (port/bettervr/layer, xr)
		};

		bool IsBreathOfTheWild(uint64_t titleId)
		{
			return titleId == 0x00050000101C9300ull || titleId == 0x00050000101C9400ull || titleId == 0x00050000101C9500ull;
		}

		bool ReadSwitch(bool& betterVR);

		// bettervr.json: {"vr": true} turns VR on; Breath of the Wild then goes through BetterVR, unless
		// "bettervr": false asks for the flat screen; every other game gets the flat screen
		Mode ReadMode()
		{
			bool betterVR = true;
			const bool on = ReadSwitch(betterVR);
			if (!on)
				return Mode::Off;
			const uint64_t titleId = CafeSystem::GetForegroundTitleId();
			if (IsBreathOfTheWild(titleId) && betterVR)
			{
				Log(fmt::format("{:016x} is Breath of the Wild: BetterVR in stereo", titleId));
				return Mode::BetterVR;
			}
			Log(fmt::format("{:016x}: the TV's picture on a screen in the headset{}", titleId,
				IsBreathOfTheWild(titleId) ? " (\"bettervr\": false)" : ""));
			return Mode::Flat;
		}

		bool ReadSwitch(bool& betterVR)
		{
			const std::string path = ps5paths::Root() + "/bettervr.json";
			std::FILE* file = std::fopen(path.c_str(), "rb");
			if (!file)
				return false;
			std::string text;
			char chunk[4096];
			for (size_t got; (got = std::fread(chunk, 1, sizeof(chunk), file)) > 0;)
				text.append(chunk, got);
			std::fclose(file);
			rapidjson::Document json;
			json.Parse(text.c_str());
			if (json.HasParseError() || !json.IsObject())
			{
				Log(fmt::format("{} is not a JSON object: VR stays off", path));
				return false;
			}
			const bool on = json.HasMember("vr") && json["vr"].IsBool() && json["vr"].GetBool();
			if (json.HasMember("bettervr") && json["bettervr"].IsBool())
				betterVR = json["bettervr"].GetBool();
			Log(fmt::format("{}: VR {}", path, on ? "on (frame generation and ReShade off while it is)" : "off"));
			return on;
		}

		uint32_t MemoryType(const VkPhysicalDeviceMemoryProperties& properties, uint32_t bits, VkMemoryPropertyFlags required,
			VkMemoryPropertyFlags preferred)
		{
			uint32_t fallback = UINT32_MAX;
			for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
			{
				const VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
				if (!(bits & (1u << i)) || (flags & required) != required)
					continue;
				if ((flags & preferred) == preferred)
					return i;
				if (fallback == UINT32_MAX)
					fallback = i;
			}
			return fallback;
		}

		bool SrgbFormat(VkFormat format)
		{
			switch (format)
			{
			case VK_FORMAT_R8G8B8A8_SRGB:
			case VK_FORMAT_B8G8R8A8_SRGB:
			case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
				return true;
			default:
				return false;
			}
		}

		// what Start made, freed; the GPU must not be using it
		void Destroy()
		{
			if (s.device == VK_NULL_HANDLE)
				return;
			for (int i = 0; i < kBuffers; i++)
			{
				if (s.mapped[i])
					vkUnmapMemory(s.device, s.memory[i]);
				if (s.buffers[i])
					vkDestroyBuffer(s.device, s.buffers[i], nullptr);
				if (s.memory[i])
					vkFreeMemory(s.device, s.memory[i], nullptr);
				s.buffers[i] = VK_NULL_HANDLE;
				s.memory[i] = VK_NULL_HANDLE;
				s.mapped[i] = nullptr;
			}
			if (s.scaled)
				vkDestroyImage(s.device, s.scaled, nullptr);
			if (s.scaledMemory)
				vkFreeMemory(s.device, s.scaledMemory, nullptr);
			s.scaled = VK_NULL_HANDLE;
			s.scaledMemory = VK_NULL_HANDLE;
			s.scaledDefined = false;
			s.device = VK_NULL_HANDLE;
		}

		// The eye buffers (third_party/hmd2/reference/eye_buffers.cpp's): host-visible and coherent,
		// device-local where there is such a type, mapped, 256-byte aligned for Hmd2's descriptor
		bool MakeBuffers(const VkPhysicalDeviceMemoryProperties& properties, std::string& error)
		{
			const VkDeviceSize bytes = (VkDeviceSize)2 * kEyeWidth * kEyeHeight * 4;
			for (int i = 0; i < kBuffers; i++)
			{
				VkBufferCreateInfo info{};
				info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				info.size = bytes;
				info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
				info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				VkResult result = vkCreateBuffer(s.device, &info, nullptr, &s.buffers[i]);
				if (result != VK_SUCCESS)
					return error = Error("vkCreateBuffer", result), false;
				VkMemoryRequirements requirements{};
				vkGetBufferMemoryRequirements(s.device, s.buffers[i], &requirements);
				const uint32_t type = MemoryType(properties, requirements.memoryTypeBits,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				if (type == UINT32_MAX)
					return error = "no host-visible coherent memory type for an eye buffer", false;
				VkMemoryAllocateInfo allocate{};
				allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				allocate.allocationSize = requirements.size;
				allocate.memoryTypeIndex = type;
				result = vkAllocateMemory(s.device, &allocate, nullptr, &s.memory[i]);
				if (result != VK_SUCCESS)
					return error = Error("vkAllocateMemory", result), false;
				result = vkBindBufferMemory(s.device, s.buffers[i], s.memory[i], 0);
				if (result != VK_SUCCESS)
					return error = Error("vkBindBufferMemory", result), false;
				result = vkMapMemory(s.device, s.memory[i], 0, VK_WHOLE_SIZE, 0, &s.mapped[i]);
				if (result != VK_SUCCESS)
					return error = Error("vkMapMemory", result), false;
				if (reinterpret_cast<uintptr_t>(s.mapped[i]) & 0xffu)
					return error = fmt::format("eye buffer {} is mapped at {}, not 256-byte aligned", i, s.mapped[i]), false;
				std::memset(s.mapped[i], 0, (size_t)bytes);
				Log(fmt::format("eye buffer {}: {}x{} RGBA8 at {} ({} bytes, memory type {})", i, 2 * kEyeWidth, kEyeHeight, s.mapped[i],
					(unsigned long long)bytes, type));
			}
			return true;
		}

		// One eye's picture, which the TV's picture is scaled into and both halves are copied from. sRGB
		// when the swapchain is, so the bytes Hmd2 samples stay as the TV's are encoded.
		bool MakeScaled(const VkPhysicalDeviceMemoryProperties& properties, VkFormat tvFormat, std::string& error)
		{
			s.scaledFormat = SrgbFormat(tvFormat) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
			VkImageCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			info.imageType = VK_IMAGE_TYPE_2D;
			info.format = s.scaledFormat;
			info.extent = {kEyeWidth, kEyeHeight, 1};
			info.mipLevels = 1;
			info.arrayLayers = 1;
			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.tiling = VK_IMAGE_TILING_OPTIMAL;
			info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkResult result = vkCreateImage(s.device, &info, nullptr, &s.scaled);
			if (result != VK_SUCCESS)
				return error = Error("vkCreateImage", result), false;
			VkMemoryRequirements requirements{};
			vkGetImageMemoryRequirements(s.device, s.scaled, &requirements);
			const uint32_t type = MemoryType(properties, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
			if (type == UINT32_MAX)
				return error = "no memory type for the scaled picture", false;
			VkMemoryAllocateInfo allocate{};
			allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocate.allocationSize = requirements.size;
			allocate.memoryTypeIndex = type;
			result = vkAllocateMemory(s.device, &allocate, nullptr, &s.scaledMemory);
			if (result != VK_SUCCESS)
				return error = Error("vkAllocateMemory", result), false;
			result = vkBindImageMemory(s.device, s.scaled, s.scaledMemory, 0);
			if (result != VK_SUCCESS)
				return error = Error("vkBindImageMemory", result), false;
			return true;
		}

		bool Start(const Frame& frame)
		{
			s.device = frame.device;
			VkPhysicalDeviceMemoryProperties properties{};
			vkGetPhysicalDeviceMemoryProperties(frame.physical, &properties);
			std::string error;
			if (!MakeBuffers(properties, error) || !MakeScaled(properties, frame.format, error))
			{
				Log("VR off: " + error);
				Destroy();
				return false;
			}
			hmd2::set_logger(HmdLog);
			hmd2::ExternalImage external[kBuffers] = {};
			for (int i = 0; i < kBuffers; i++)
				external[i] = {s.mapped[i], (size_t)2 * kEyeWidth * kEyeHeight * 4};
			hmd2::Config config;
			config.external = external;
			config.external_count = kBuffers;
			config.image_width = 2 * kEyeWidth;
			config.image_height = kEyeHeight;
			config.mirror = false; // the TV keeps Cemu's own picture
			if (!hmd2::init(config))
			{
				const hmd2::Error failed = hmd2::error();
				Log(fmt::format("VR off: hmd2 did not start, at {} (rc {:#010x}); the hmd2 lines above say more",
					failed.step && failed.step[0] ? failed.step : "an unknown step", (uint32_t)failed.rc));
				Destroy();
				return false;
			}
			const hmd2::Fov fov = hmd2::field_of_view();
			Log(fmt::format("headset up: {}x{} per eye, {} buffers, {} Hz output, user {}, export table {}, FOV tangents out {:.3f} in {:.3f} "
				"top {:.3f} bottom {:.3f}; the TV's {}x{} picture (format {}) across {:.0f}% of each eye",
				hmd2::eye_size(), hmd2::eye_height(), hmd2::buffer_count(), hmd2::refresh_hz(), hmd2::user_id(), hmd2::firmware_table(), fov.out,
				fov.in, fov.top, fov.bottom, frame.extent.width, frame.extent.height, (int)frame.format, kScreenWidth * 100));
			s.up = true;
			return true;
		}

		// Enabled, under the lock
		Mode s_mode = Mode::Off; // ReadMode's, once a game has asked

		// Enabled, under the lock: M2's flat screen
		bool Wanted()
		{
			if (!s.checked)
			{
				s.checked = true;
				s_mode = ReadMode();
				s.enabled = s_mode == Mode::Flat;
			}
			return s.enabled;
		}

		void ImageBarrier(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
			VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
		{
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.srcAccessMask = srcAccess;
			barrier.dstAccessMask = dstAccess;
			barrier.oldLayout = from;
			barrier.newLayout = to;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdPipelineBarrier(commands, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

		// The TV's picture scaled into the middle of one eye, black around it, then that eye into both
		// halves of the side-by-side buffer
		void RecordCopy(const Frame& frame, VkBuffer buffer)
		{
			VkCommandBuffer commands = frame.commands;
			ImageBarrier(commands, frame.image, frame.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			// the frame before was copied out of it (its commands finished before its flip)
			ImageBarrier(commands, s.scaled, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			const VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
			const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdClearColorImage(commands, s.scaled, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
			ImageBarrier(commands, s.scaled, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			const uint32_t tvWidth = frame.extent.width ? frame.extent.width : 1;
			const uint32_t tvHeight = frame.extent.height ? frame.extent.height : 1;
			int32_t width = (int32_t)(kEyeWidth * kScreenWidth);
			int32_t height = (int32_t)((uint64_t)width * tvHeight / tvWidth);
			if (height > (int32_t)kEyeHeight)
			{
				height = kEyeHeight;
				width = (int32_t)((uint64_t)height * tvWidth / tvHeight);
			}
			const int32_t x = ((int32_t)kEyeWidth - width) / 2, y = ((int32_t)kEyeHeight - height) / 2;
			VkImageBlit blit{};
			blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			blit.srcOffsets[1] = {(int32_t)tvWidth, (int32_t)tvHeight, 1};
			blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			blit.dstOffsets[0] = {x, y, 0};
			blit.dstOffsets[1] = {x + width, y + height, 1};
			vkCmdBlitImage(commands, frame.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.scaled, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
				&blit, VK_FILTER_LINEAR);

			ImageBarrier(commands, frame.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame.layout, VK_ACCESS_TRANSFER_READ_BIT, 0,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
			ImageBarrier(commands, s.scaled, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			VkBufferImageCopy regions[2]{};
			for (int eye = 0; eye < 2; eye++)
			{
				regions[eye].bufferOffset = (VkDeviceSize)eye * kEyeWidth * 4;
				regions[eye].bufferRowLength = 2 * kEyeWidth;
				regions[eye].bufferImageHeight = kEyeHeight;
				regions[eye].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
				regions[eye].imageExtent = {kEyeWidth, kEyeHeight, 1};
			}
			vkCmdCopyImageToBuffer(commands, s.scaled, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 2, regions);

			// the GPU wrote the buffer: visible to Hmd2 before the flip (HLA-PSVR2's barrier, as
			// eye_buffers.cpp's record_eye_copy)
			VkBufferMemoryBarrier toHost{};
			toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
			toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toHost.buffer = buffer;
			toHost.size = VK_WHOLE_SIZE;
			vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
				nullptr, 1, &toHost, 0, nullptr);
		}
	}

	bool Enabled()
	{
		std::lock_guard lock(s_lock);
		Wanted();
		return s_mode != Mode::Off; // frame generation and ReShade off in either mode
	}

	bool Record(const Frame& frame)
	{
		std::lock_guard lock(s_lock);
		if (!Wanted() || s.open)
			return false;
		if (!s.up && !Start(frame))
		{
			s.enabled = false;
			return false;
		}
		if (frame.device != s.device)
			return false;
		if (!hmd2::wait_slot())
		{
			Log(fmt::format("VR off: hmd2 stopped (rc {:#010x}) after {} frames", (uint32_t)hmd2::error().rc, s.frames));
			s.enabled = false;
			return false;
		}
		if (!hmd2::begin_frame())
		{
			if (s.skipped++ % kLogEvery == 0)
				Log(fmt::format("no headset frame this time: BeginFrame (rc {:#010x}), {} so far", (uint32_t)hmd2::error().rc, s.skipped));
			return false;
		}
		const hmd2::EyeBuffer eye = hmd2::acquire();
		s.buffer = eye.index >= 0 && eye.index < kBuffers ? eye.index : 0;
		RecordCopy(frame, s.buffers[s.buffer]);
		s.open = true;
		return true;
	}

	void Submit()
	{
		std::lock_guard lock(s_lock);
		if (!s.open || !s.up)
			return;
		s.open = false;
		hmd2::submit();
		if (++s.frames % kLogEvery == 0)
		{
			const hmd2::PaceStats pace = hmd2::take_pace();
			const hmd2::FlipInfo flip = hmd2::flip_status();
			Log(fmt::format("{} headset frames: nap {:.2f} ms, BeginFrame {:.2f} ms, ahead {:.2f} ms, step {:.2f} ms; flip shown {} pending {}; "
				"submitted {}, repeated {}, queue full {}, missed {}, errors {}, skipped {}, worn {}",
				s.frames, pace.nap_ms, pace.call_ms, pace.ahead_ms, pace.step_ms, (unsigned long long)flip.shown, flip.pending,
				hmd2::frames_submitted(), hmd2::repeated_frames(), hmd2::queue_full_waits(), hmd2::missed_frames(), hmd2::frame_errors(),
				s.skipped, hmd2::worn() ? 1 : 0));
		}
	}

	void Shutdown()
	{
		std::lock_guard lock(s_lock);
		if (!s.up)
			return;
		// Hmd2 first: reprojection samples the buffers until VR mode is off and they are unregistered
		hmd2::shutdown();
		Destroy();
		s.up = false;
		s.open = false;
		Log(fmt::format("headset off after {} frames", s.frames));
	}

	void Stop()
	{
		std::lock_guard lock(s_lock);
		s.checked = true;
		s.enabled = false;
		if (!s.up)
			return;
		hmd2::shutdown();
		s.up = false;
		s.open = false;
		Log(fmt::format("headset off after {} frames, before the app is replaced", s.frames));
	}
}

// BetterVR's layer (patches/bettervr: IsLayerActiveForProcess) and the Vulkan loader's stand-in
// (VulkanLayerPS5.cpp): whether Breath of the Wild goes through BetterVR
extern "C" bool BetterVR_PS5_LayerWanted()
{
	std::lock_guard lock(ps5vr::s_lock);
	ps5vr::Wanted();
	return ps5vr::s_mode == ps5vr::Mode::BetterVR;
}

// app/emulator.cpp, before Cemu loads its graphic packs: BetterVR's pack (the app's
// assets/bettervr, tools/package.sh) in graphicPacks/bettervr while bettervr.json asks for BetterVR,
// and gone from there otherwise. It is on by default for Breath of the Wild, and its patches call
// BetterVR's hooks, which only exist while BetterVR runs: left there, it would break the game flat.
void PS5Cemu_BetterVRPrepareGraphicPack()
{
	namespace fs = std::filesystem;
	bool betterVR = true;
	const bool wanted = ps5vr::ReadSwitch(betterVR) && betterVR;
	const fs::path source = fs::path(ps5paths::AppDir()) / "assets" / "bettervr" / "BreathOfTheWild_BetterVR";
	const fs::path target = fs::path(ps5paths::CemuRoot()) / "graphicPacks" / "bettervr";
	std::error_code ec;
	if (!wanted)
	{
		if (ps5files::OwnStatus(target, ec).type() == fs::file_type::directory)
		{
			fs::remove_all(target, ec);
			ps5vr::Log(fmt::format("BetterVR's graphic pack taken out of {} (BetterVR is off){}", target.string(), ec ? ": " + ec.message() : ""));
		}
		return;
	}
	if (ps5files::OwnStatus(source / "rules.txt", ec).type() != fs::file_type::regular)
	{
		ps5vr::Log(fmt::format("BetterVR's graphic pack is not in the app ({}): Breath of the Wild will not start in stereo", source.string()));
		return;
	}
	// copied again each start, so a new app's pack replaces the last one (35 small files)
	fs::remove_all(target, ec);
	fs::create_directories(target, ec);
	fs::copy(source, target / "BreathOfTheWild_BetterVR", fs::copy_options::recursive, ec);
	ps5vr::Log(fmt::format("BetterVR's graphic pack in {}{}", (target / "BreathOfTheWild_BetterVR").string(), ec ? " failed: " + ec.message() : ""));
}

// Cemu's export for BetterVR, which Cemu defines in src/main.cpp: the PS5's entry point is
// port/main_ps5.cpp, so it is here, as Cemu has it
extern "C" uint64_t gameMeta_getTitleId()
{
	return CafeSystem::GetForegroundTitleId();
}

// BetterVR's own settings (its ImGui ini, patches/bettervr): beside bettervr.json, as the app's working
// directory is not writable
extern "C" const char* BetterVR_PS5_SettingsPath()
{
	static const std::string path = ps5paths::Root() + "/bettervr_settings.ini";
	return path.c_str();
}

// port/bettervr/xr/runtime.cpp, in BetterVR's object
extern "C" void BetterVR_PS5_Stop();

// app/emulator.cpp, before every LoadExec: the headset off, whichever mode had it
void PS5Cemu_VrStop()
{
	ps5vr::Stop();
	BetterVR_PS5_Stop();
}
