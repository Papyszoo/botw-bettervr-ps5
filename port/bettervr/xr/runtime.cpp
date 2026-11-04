// SPDX-License-Identifier: GPL-3.0-or-later
// BotW-BetterVR on PS5, M3: a small OpenXR runtime over hmd2 (third_party/hmd2, from photo-splat-vr),
// built into BetterVR's object (port/bettervr/layer). It implements what BetterVR's Linux backend
// calls, and nothing else (PLAN.md, M3, option A, the way PS5XR does for HL:A):
//
//   instance, system   one of each: "PlayStation VR2", Vulkan (XR_KHR_vulkan_enable). It also lists
//                      XR_KHR_composition_layer_depth, which BetterVR requires, and ignores the depth
//                      it is given: Hmd2 reprojects without depth.
//   session            on BetterVR's Vulkan binding (Cemu's device, queue family and queue): three
//                      host-visible eye buffers on that device, handed to hmd2::init as
//                      Config::external, as M2 does (port/bettervr/VrPS5.cpp)
//   spaces             VIEW, LOCAL and STAGE are hmd2's tracker space (+y up, -z forward, metres, as
//                      OpenXR's); VIEW follows the head. Action spaces locate nothing yet (M4).
//   views              xrLocateViews: hmd2's predicted head pose for the time, its eyes' offsets from
//                      the frame's own result (else half a 63 mm IPD each side), and its FOV tangents
//   swapchains         three Vulkan images each, on Cemu's device. A released colour image is left in
//                      COLOR_ATTACHMENT_OPTIMAL, as the spec has the application leave it.
//   frames             xrWaitFrame is hmd2::wait_slot + begin_frame (Hmd2's pacing, 60 frames a
//                      second, each shown for two of its 120 Hz refreshes). xrEndFrame composes the
//                      projection layer and the HUD quad into the frame's eye buffer in one compute
//                      pass (shaders/compose.comp), on the binding's queue, waits for it, and submits
//                      it with the head pose the submitted views were rendered from.
//   actions            (M4) the PS VR2 Sense controllers as the Oculus Touch profile BetterVR binds:
//                      left Square/Triangle = X/Y, right Cross/Circle = A/B, L1/R1 = squeeze,
//                      L3/R3 = thumbstick click, the stick, the trigger (the larger of its two bytes),
//                      Create = menu. Read with scePadVrControllerReadState (types 3 and 4, opened
//                      for Hmd2's user), posed by VrTracker2 (hmd2::register_controller /
//                      controller_pose) with velocities from successive poses; haptics through
//                      scePadSetVibration. The DualSense stays the game's GamePad (Cemu's), which
//                      BetterVR merges with these. Layout from photo-splat-vr's pad_input.h and
//                      HLA-PSVR2's psvr2.c (third_party/hmd2/reference); none of it seen on a console.
//
// The PS5 has one graphics queue, which Cemu's GPU thread, BetterVR's submit thread and this
// compositor share: every submission to it holds ps5xr::QueueMutex() (patches/bettervr 0002 for
// BetterVR's and Cemu's, through BetterVR's layer).
//
// The app ends a game with a LoadExec: BetterVR_PS5_Stop (app/emulator.cpp, before it) turns Hmd2 off
// from any thread, and every call after it answers XR_ERROR_SESSION_LOST.

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "hmd2.h"
#include "shaders.h"

extern "C" {
// libScePad (the app's pad.cpp has called scePadInit)
int scePadOpen(int user, int type, int index, const void* parameters);
int scePadGetHandle(int user, int type, int index);
int scePadClose(int handle);
int scePadVrControllerReadState(int handle, void* state);
int scePadSetVibration(int handle, const void* vibration);
int scePadSetVibrationMode(int handle, int mode);
// port/bettervr/VrPS5.cpp: a number from bettervr.json, or fallback
float BetterVR_PS5_Setting(const char* name, float fallback);
}

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// the port's (ps5/log.cpp): a line of the boot log
void PS5Cemu_LogLine(std::string_view line);
// RADV's, linked into the app: where the runtime's own Vulkan calls come from (not through BetterVR's layer)
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
extern "C" uint64_t sceKernelGetProcessTime(void);

namespace ps5xr
{
	std::mutex& QueueMutex()
	{
		static std::mutex mutex;
		return mutex;
	}
}

namespace
{
	constexpr uint32_t kEyeWidth = 1984; // per eye: photo-splat-vr's size, the one Hmd2 reprojects there
	constexpr uint32_t kEyeHeight = 2048;
	constexpr int kEyeBuffers = 3;
	constexpr int kSwapchainImages = 3;
	constexpr XrDuration kFramePeriod = 16'666'667; // ns: a frame every two of Hmd2's 120 Hz refreshes
	constexpr float kHalfIpd = 0.0315f;
	constexpr uint64_t kGpuTimeoutNs = 2'000'000'000ull;

	void Log(const char* format, ...)
	{
		char text[512];
		va_list args;
		va_start(args, format);
		std::vsnprintf(text, sizeof(text), format, args);
		va_end(args);
		PS5Cemu_LogLine(std::string("[vr-xr] ") + text);
	}

	void HmdLog(const char* line)
	{
		PS5Cemu_LogLine(std::string("[vr-xr] ") + line);
	}

	XrTime Now()
	{
		return (XrTime)sceKernelGetProcessTime() * 1000;
	}

	// -- poses -------------------------------------------------------------------------------
	struct Vec3
	{
		float x = 0, y = 0, z = 0;
	};
	struct Quat
	{
		float x = 0, y = 0, z = 0, w = 1;
	};
	struct Pose
	{
		Quat q;
		Vec3 p;
	};

	Quat Mul(const Quat& a, const Quat& b)
	{
		return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
	}
	Quat Conj(const Quat& q)
	{
		return {-q.x, -q.y, -q.z, q.w};
	}
	Vec3 Rotate(const Quat& q, const Vec3& v)
	{
		const Quat r = Mul(Mul(q, Quat{v.x, v.y, v.z, 0}), Conj(q));
		return {r.x, r.y, r.z};
	}
	Vec3 Add(const Vec3& a, const Vec3& b)
	{
		return {a.x + b.x, a.y + b.y, a.z + b.z};
	}
	Vec3 Sub(const Vec3& a, const Vec3& b)
	{
		return {a.x - b.x, a.y - b.y, a.z - b.z};
	}
	// a's pose, given in b's frame, in the frame b is given in
	Pose Compose(const Pose& b, const Pose& a)
	{
		return {Mul(b.q, a.q), Add(b.p, Rotate(b.q, a.p))};
	}
	Pose Inverse(const Pose& a)
	{
		const Quat q = Conj(a.q);
		const Vec3 p = Rotate(q, a.p);
		return {q, {-p.x, -p.y, -p.z}};
	}
	Pose FromXr(const XrPosef& x)
	{
		return {{x.orientation.x, x.orientation.y, x.orientation.z, x.orientation.w}, {x.position.x, x.position.y, x.position.z}};
	}
	XrPosef ToXr(const Pose& p)
	{
		return {{p.q.x, p.q.y, p.q.z, p.q.w}, {p.p.x, p.p.y, p.p.z}};
	}
	Quat AxisAngle(float x, float y, float z, float degrees)
	{
		const float half = degrees * 3.14159265f / 360.0f, s = std::sin(half);
		return {x * s, y * s, z * s, std::cos(half)};
	}
	Pose FromHmd(const hmd2::Pose& h)
	{
		return {{h.quat[0], h.quat[1], h.quat[2], h.quat[3]}, {h.pos[0], h.pos[1], h.pos[2]}};
	}
	hmd2::Pose ToHmd(const Pose& p)
	{
		return {{p.p.x, p.p.y, p.p.z}, {p.q.x, p.q.y, p.q.z, p.q.w}, true};
	}
}

// -- the handles' objects ----------------------------------------------------------------------
struct XrInstance_T
{
	bool depth = false;
};
struct XrActionSet_T
{
	std::string name;
	bool active = false; // in the last xrSyncActions' list
};

namespace
{
	// The Touch profile's inputs BetterVR binds, and the Sense controller's part for each
	enum class Component
	{
		X, Y, A, B, Menu, Squeeze, Trigger, Thumbstick, ThumbstickX, ThumbstickY, ThumbstickClick, GripPose, AimPose, Haptic
	};
	struct Binding
	{
		int hand; // 0 left, 1 right
		Component component;
	};
}

struct XrAction_T
{
	XrActionType type;
	XrActionSet_T* set = nullptr;
	std::vector<Binding> bindings;
	// per hand, for changedSinceLastSync and lastChangeTime
	float last[2][2] = {};
	XrTime changedAt[2] = {};
	bool changed[2] = {};
};

struct XrSwapchain_T
{
	VkFormat format = VK_FORMAT_UNDEFINED;
	uint32_t width = 0, height = 0;
	bool color = true;
	VkImage images[kSwapchainImages] = {};
	VkDeviceMemory memory[kSwapchainImages] = {};
	VkImageView views[kSwapchainImages] = {};
	uint32_t next = 0;	   // the next acquire
	int released = -1;	   // the last image released, the one a layer shows
};

struct XrSpace_T
{
	XrReferenceSpaceType type = XR_REFERENCE_SPACE_TYPE_STAGE;
	bool action = false; // an action space: a hand's grip or aim pose
	Pose offset;
	XrAction_T* poseAction = nullptr;
	int hand = -1; // the subaction path's hand, -1 for either
};

struct XrSession_T
{
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t family = 0;
	bool running = false;
	bool hmdUp = false;
	bool frameOpen = false; // xrWaitFrame began an Hmd2 frame that xrEndFrame has not submitted
	bool gpuFailed = false;

	VkBuffer eyeBuffers[kEyeBuffers] = {};
	VkDeviceMemory eyeMemory[kEyeBuffers] = {};
	void* eyeMapped[kEyeBuffers] = {};

	VkImage dummy = VK_NULL_HANDLE;
	VkDeviceMemory dummyMemory = VK_NULL_HANDLE;
	VkImageView dummyView = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	VkDescriptorSet set = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commands = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;

	uint64_t frames = 0, emptyFrames = 0, skipped = 0;
};

namespace
{
	// -- Vulkan, from RADV ------------------------------------------------------------------------
	struct Vk
	{
#define VK_FUNCTIONS(X)                                                                                                     \
	X(vkGetDeviceQueue) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkAllocateMemory)              \
	X(vkFreeMemory) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateBuffer) X(vkDestroyBuffer)     \
	X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkCreateSampler)                \
	X(vkDestroySampler) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout)            \
	X(vkDestroyPipelineLayout) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateComputePipelines)                 \
	X(vkDestroyPipeline) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets)                   \
	X(vkUpdateDescriptorSets) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers)                    \
	X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdPipelineBarrier) X(vkCmdBindPipeline)     \
	X(vkCmdBindDescriptorSets) X(vkCmdPushConstants) X(vkCmdDispatch) X(vkQueueSubmit) X(vkCreateFence) X(vkDestroyFence)  \
	X(vkWaitForFences) X(vkResetFences) X(vkDeviceWaitIdle)
#define VK_MEMBER(name) PFN_##name name = nullptr;
		VK_FUNCTIONS(VK_MEMBER)
		PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
		PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;

		bool Load(VkInstance instance, VkDevice device)
		{
			auto gdpa = (PFN_vkGetDeviceProcAddr)vk_icdGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");
			if (!gdpa)
				return false;
			bool all = true;
#define VK_LOAD(name)                                     \
	name = (PFN_##name)gdpa(device, #name);              \
	if (!name)                                          \
	{                                                   \
		Log("no %s in the driver", #name);            \
		all = false;                                    \
	}
			VK_FUNCTIONS(VK_LOAD)
			vkGetPhysicalDeviceMemoryProperties =
				(PFN_vkGetPhysicalDeviceMemoryProperties)vk_icdGetInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties");
			return all && vkGetPhysicalDeviceMemoryProperties;
		}
	} vk;

	// -- state ---------------------------------------------------------------------------------
	struct State
	{
		std::mutex lock; // every call: one session, one frame loop, and hmd2 is not thread-safe
		std::atomic<bool> stopped{false};
		XrInstance instance = XR_NULL_HANDLE;
		XrSession session = XR_NULL_HANDLE;
		std::vector<std::string> paths; // XrPath n is paths[n - 1]
		std::deque<XrEventDataBuffer> events;
		// -- input (M4) --
		XrPath touchProfile = XR_NULL_PATH, handPath[2] = {XR_NULL_PATH, XR_NULL_PATH};
		bool touchSuggested = false, attached = false;
		std::vector<XrAction_T*> actions;
		struct Hand
		{
			int handle = -1;
			bool registered = false, connected = false, reportedMissing = false;
			XrTime nextOpen = 0;
			uint32_t buttons = 0, rawLogged = 0xffffffffu;
			float stickX = 0, stickY = 0, trigger = 0;
			bool poseValid = false, velocityValid = false;
			Pose pose;
			Vec3 linear, angular;
			XrTime poseTime = 0;
			XrTime hapticUntil = 0;
			bool vibrating = false, vibrationLogged = false;
		} hands[2];
		float gripPitch = 0, aimPitch = 0;
		XrTime syncTime = 0;
		hmd2::Fov fov{1.0f, 1.0f, 1.0f, 1.0f};
		Vec3 eyeOffset[2] = {{-kHalfIpd, 0, 0}, {kHalfIpd, 0, 0}}; // in the head's frame
		// the head pose each pair of located views was computed from: xrEndFrame submits with the one
		// whose left eye is the submitted left view
		struct Located
		{
			Vec3 leftEye;
			Pose head;
		};
		Located located[32];
		int nextLocated = 0;
		Pose lastHead;
	} s;

	void QueueState(XrSessionState state)
	{
		XrEventDataBuffer buffer{};
		auto* event = reinterpret_cast<XrEventDataSessionStateChanged*>(&buffer);
		event->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
		event->session = s.session;
		event->state = state;
		event->time = Now();
		s.events.push_back(buffer);
	}

	void QueueProfileChanged()
	{
		if (!s.attached)
			return;
		XrEventDataBuffer buffer{};
		auto* event = reinterpret_cast<XrEventDataInteractionProfileChanged*>(&buffer);
		event->type = XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED;
		event->session = s.session;
		s.events.push_back(buffer);
	}

	// -- the Sense controllers (M4) ------------------------------------------------------------
	constexpr int kPadTypeSense[2] = {3, 4}; // scePad types: left, right (HLA-PSVR2 psvr2.c:461)
	constexpr size_t kSenseStateBytes = 0x68;
	constexpr size_t kSenseConnected = 0x40; // bit 0 (psvr2.c:517-525)
	constexpr size_t kSenseStickX = 0x04, kSenseStickY = 0x05, kSenseTriggerA = 0x08, kSenseTriggerB = 0x09;
	constexpr uint32_t kIntercepted = 0x80000000u; // the system menu has the pads
	constexpr uint32_t kCreate = 0x1, kL3 = 0x2, kR3 = 0x4, kOptions = 0x8, kL1 = 0x400, kR1 = 0x800, kTriangle = 0x1000,
					   kCircle = 0x2000, kCross = 0x4000, kSquare = 0x8000;
	constexpr XrDuration kPadRetry = 2'000'000'000; // as photo-splat-vr and HLA-PSVR2: every 2 s
	constexpr int kVibrationModeCompatible = 2;			 // the app's pad.cpp sets it for the DualSense's motors

	const char* HandName(int hand)
	{
		return hand == 0 ? "left" : "right";
	}

	float Axis(uint8_t raw) // -1..1, BetterVR applies its own dead zone
	{
		return std::max(-1.0f, std::min(1.0f, ((float)raw - 128.0f) / 127.0f));
	}

	void OpenHands()
	{
		const XrTime now = Now();
		const int user = hmd2::user_id();
		for (int i = 0; i < 2; i++)
		{
			auto& hand = s.hands[i];
			if (hand.handle >= 0 || now < hand.nextOpen)
				continue;
			hand.nextOpen = now + kPadRetry;
			int handle = scePadOpen(user, kPadTypeSense[i], 0, nullptr);
			if (handle < 0)
				handle = scePadGetHandle(user, kPadTypeSense[i], 0);
			if (handle < 0)
			{
				if (!hand.reportedMissing)
					Log("no %s Sense controller for user %d (scePadOpen -> 0x%08x); trying every 2 s", HandName(i), user, (unsigned)handle);
				hand.reportedMissing = true;
				continue;
			}
			hand.handle = handle;
			hand.reportedMissing = false;
			const int mode = scePadSetVibrationMode(handle, kVibrationModeCompatible);
			hand.registered = hmd2::register_controller(handle, i == 1);
			Log("%s Sense controller open (handle %d), VrTracker2 registration %s, vibration mode 0x%08x", HandName(i), handle,
				hand.registered ? "done" : "FAILED (no pose for this hand)", (unsigned)mode);
		}
	}

	void CloseHands()
	{
		for (auto& hand : s.hands)
		{
			if (hand.handle >= 0)
				scePadClose(hand.handle);
			hand = State::Hand{};
		}
	}

	// once a frame (xrSyncActions): buttons, stick, trigger and pose of each hand
	void ReadHands()
	{
		OpenHands();
		const XrTime now = Now();
		for (int i = 0; i < 2; i++)
		{
			auto& hand = s.hands[i];
			const bool wasConnected = hand.connected;
			hand.connected = false;
			hand.buttons = 0;
			hand.stickX = hand.stickY = hand.trigger = 0;
			hand.poseValid = false;
			if (hand.handle < 0)
				continue;
			alignas(16) uint8_t state[kSenseStateBytes] = {};
			const bool read = scePadVrControllerReadState(hand.handle, state) == 0;
			hand.connected = read && (state[kSenseConnected] & 1) != 0;
			if (hand.connected != wasConnected)
			{
				Log("%s Sense controller %s", HandName(i), hand.connected ? "connected" : "gone");
				QueueProfileChanged();
			}
			if (!hand.connected)
				continue;
			uint32_t buttons = 0;
			std::memcpy(&buttons, state, sizeof(buttons));
			if (buttons != hand.rawLogged)
			{
				hand.rawLogged = buttons;
				static int s_logged = 0;
				if (s_logged++ < 40)
					Log("%s Sense buttons 0x%08x stick %u,%u triggers %u,%u", HandName(i), buttons, state[kSenseStickX], state[kSenseStickY],
						state[kSenseTriggerA], state[kSenseTriggerB]);
			}
			if (buttons & kIntercepted)
				continue; // the system menu has it: nothing pressed here
			hand.buttons = buttons;
			hand.stickX = Axis(state[kSenseStickX]);
			hand.stickY = -Axis(state[kSenseStickY]); // its byte is down positive
			hand.trigger = (float)std::max(state[kSenseTriggerA], state[kSenseTriggerB]) / 255.0f;
			if (!hand.registered)
				continue;
			float pos[3], quat[4];
			if (!hmd2::controller_pose(hand.handle, pos, quat))
				continue;
			const Pose pose{{quat[0], quat[1], quat[2], quat[3]}, {pos[0], pos[1], pos[2]}};
			const double dt = hand.poseTime ? (double)(now - hand.poseTime) / 1e9 : 0.0;
			hand.velocityValid = false;
			if (dt > 0.001 && dt < 0.2)
			{
				hand.linear = {(float)((pose.p.x - hand.pose.p.x) / dt), (float)((pose.p.y - hand.pose.p.y) / dt),
					(float)((pose.p.z - hand.pose.p.z) / dt)};
				// the rotation from the last pose to this one, as an axis times its angle per second
				Quat d = Mul(pose.q, Conj(hand.pose.q));
				if (d.w < 0)
					d = {-d.x, -d.y, -d.z, -d.w};
				const float sinHalf = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
				const float angle = 2.0f * std::atan2(sinHalf, d.w);
				const float k = sinHalf > 1e-6f ? angle / sinHalf / (float)dt : 0.0f;
				hand.angular = {d.x * k, d.y * k, d.z * k};
				hand.velocityValid = true;
			}
			hand.pose = pose;
			hand.poseTime = now;
			hand.poseValid = true;
		}
	}

	// One bound input's value now: a button as 0 or 1, an axis as itself
	float Value(const State::Hand& hand, Component component)
	{
		const bool left = &hand == &s.hands[0];
		switch (component)
		{
		case Component::X: return left && (hand.buttons & kSquare) ? 1.0f : 0.0f;
		case Component::Y: return left && (hand.buttons & kTriangle) ? 1.0f : 0.0f;
		case Component::A: return !left && (hand.buttons & kCross) ? 1.0f : 0.0f;
		case Component::B: return !left && (hand.buttons & kCircle) ? 1.0f : 0.0f;
		case Component::Menu: return (hand.buttons & (left ? kCreate : kOptions)) ? 1.0f : 0.0f;
		case Component::Squeeze: return (hand.buttons & (left ? kL1 : kR1)) ? 1.0f : 0.0f;
		case Component::Trigger: return hand.trigger;
		case Component::ThumbstickX: return hand.stickX;
		case Component::ThumbstickY: return hand.stickY;
		case Component::ThumbstickClick: return (hand.buttons & (left ? kL3 : kR3)) ? 1.0f : 0.0f;
		default: return 0.0f;
		}
	}

	bool IsPose(Component c)
	{
		return c == Component::GripPose || c == Component::AimPose;
	}

	// An action's value for a hand (-1 both): [0] for a button or float, [0],[1] for a stick.
	// False when nothing bound to it is there to read.
	bool ActionValue(const XrAction_T* action, int which, float out[2])
	{
		out[0] = out[1] = 0;
		if (!action->set || !action->set->active)
			return false;
		bool any = false;
		for (const Binding& binding : action->bindings)
		{
			if (which >= 0 && binding.hand != which)
				continue;
			const auto& hand = s.hands[binding.hand];
			if (!hand.connected)
				continue;
			any = true;
			if (binding.component == Component::Thumbstick)
			{
				if (std::fabs(hand.stickX) + std::fabs(hand.stickY) > std::fabs(out[0]) + std::fabs(out[1]))
					out[0] = hand.stickX, out[1] = hand.stickY;
			}
			else if (!IsPose(binding.component) && binding.component != Component::Haptic)
			{
				const float v = Value(hand, binding.component);
				if (std::fabs(v) > std::fabs(out[0]))
					out[0] = v;
			}
		}
		return any;
	}

	int HandOf(XrPath path)
	{
		if (path == XR_NULL_PATH)
			return -1;
		return path == s.handPath[0] ? 0 : path == s.handPath[1] ? 1 : -2;
	}

	// "/user/hand/left/input/x/click" -> left, X
	bool ParseTouchPath(const std::string& path, Binding& binding)
	{
		static const struct
		{
			const char* name;
			Component component;
		} inputs[] = {{"x/click", Component::X}, {"x", Component::X}, {"y/click", Component::Y}, {"y", Component::Y},
			{"a/click", Component::A}, {"a", Component::A}, {"b/click", Component::B}, {"b", Component::B},
			{"menu/click", Component::Menu}, {"menu", Component::Menu}, {"system/click", Component::Menu},
			{"squeeze/value", Component::Squeeze}, {"squeeze/force", Component::Squeeze}, {"squeeze", Component::Squeeze},
			{"trigger/value", Component::Trigger}, {"trigger", Component::Trigger}, {"thumbstick", Component::Thumbstick},
			{"thumbstick/x", Component::ThumbstickX}, {"thumbstick/y", Component::ThumbstickY},
			{"thumbstick/click", Component::ThumbstickClick}, {"grip/pose", Component::GripPose}, {"grip", Component::GripPose},
			{"aim/pose", Component::AimPose}, {"aim", Component::AimPose}};
		for (int hand = 0; hand < 2; hand++)
		{
			const std::string prefix = std::string("/user/hand/") + HandName(hand) + "/";
			if (path.rfind(prefix, 0) != 0)
				continue;
			const std::string rest = path.substr(prefix.size());
			binding.hand = hand;
			if (rest == "output/haptic")
				return binding.component = Component::Haptic, true;
			if (rest.rfind("input/", 0) != 0)
				return false;
			const std::string input = rest.substr(6);
			for (const auto& entry : inputs)
				if (input == entry.name)
					return binding.component = entry.component, true;
			return false;
		}
		return false;
	}

	void SetVibration(int i, float amplitude)
	{
		auto& hand = s.hands[i];
		if (hand.handle < 0)
			return;
		const uint8_t level = (uint8_t)std::lround(std::max(0.0f, std::min(1.0f, amplitude)) * 255.0f);
		const uint8_t motors[2] = {level, level};
		const int result = scePadSetVibration(hand.handle, motors);
		if (!hand.vibrationLogged && level)
		{
			hand.vibrationLogged = true;
			Log("%s Sense controller vibration -> 0x%08x (first one; untested on this controller)", HandName(i), (unsigned)result);
		}
		hand.vibrating = level != 0;
	}

	// the head in tracker space at a time, with hmd2's prediction; the last one when it has none
	Pose HeadAt(XrTime time)
	{
		if (s.session == XR_NULL_HANDLE || !s.session->hmdUp)
			return s.lastHead;
		const hmd2::Pose head = hmd2::predicted_pose((uint64_t)(time / 1000));
		if (head.tracked)
			s.lastHead = FromHmd(head);
		return s.lastHead;
	}

	// a space's own pose in tracker space at a time
	Pose SpacePose(const XrSpace_T* space, XrTime time)
	{
		if (space->type == XR_REFERENCE_SPACE_TYPE_VIEW)
			return Compose(HeadAt(time), space->offset);
		return space->offset;
	}

	float Tan(float angle)
	{
		return std::tan(angle);
	}

	uint32_t MemoryType(const VkPhysicalDeviceMemoryProperties& props, uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred)
	{
		uint32_t fallback = UINT32_MAX;
		for (uint32_t i = 0; i < props.memoryTypeCount; i++)
		{
			const VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
			if (!(bits & (1u << i)) || (flags & required) != required)
				continue;
			if ((flags & preferred) == preferred)
				return i;
			if (fallback == UINT32_MAX)
				fallback = i;
		}
		return fallback;
	}

	bool Fail(const char* what, VkResult result)
	{
		Log("%s -> %d", what, (int)result);
		return false;
	}

	// one-time commands on the session's queue, waited for
	template<typename Record>
	bool RunOnce(XrSession_T* session, Record&& record)
	{
		vk.vkResetCommandBuffer(session->commands, 0);
		VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk.vkBeginCommandBuffer(session->commands, &begin);
		record(session->commands);
		vk.vkEndCommandBuffer(session->commands);
		vk.vkResetFences(session->device, 1, &session->fence);
		VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &session->commands;
		VkResult result;
		{
			std::lock_guard queue(ps5xr::QueueMutex());
			result = vk.vkQueueSubmit(session->queue, 1, &submit, session->fence);
		}
		if (result != VK_SUCCESS)
			return Fail("vkQueueSubmit", result);
		result = vk.vkWaitForFences(session->device, 1, &session->fence, VK_TRUE, kGpuTimeoutNs);
		if (result != VK_SUCCESS)
		{
			session->gpuFailed = true;
			return Fail("vkWaitForFences (2 s)", result);
		}
		return true;
	}

	void ImageBarrier(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
		VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
	{
		VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		barrier.srcAccessMask = srcAccess;
		barrier.dstAccessMask = dstAccess;
		barrier.oldLayout = from;
		barrier.newLayout = to;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = {aspect, 0, 1, 0, 1};
		vk.vkCmdPipelineBarrier(commands, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	bool MakeImage(XrSession_T* session, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image,
		VkDeviceMemory& memory, VkImageView* view)
	{
		VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = format;
		info.extent = {width, height, 1};
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = usage;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkResult result = vk.vkCreateImage(session->device, &info, nullptr, &image);
		if (result != VK_SUCCESS)
			return Fail("vkCreateImage", result);
		VkMemoryRequirements requirements{};
		vk.vkGetImageMemoryRequirements(session->device, image, &requirements);
		VkPhysicalDeviceMemoryProperties props{};
		vk.vkGetPhysicalDeviceMemoryProperties(session->physical, &props);
		VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocate.allocationSize = requirements.size;
		allocate.memoryTypeIndex = MemoryType(props, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
		result = vk.vkAllocateMemory(session->device, &allocate, nullptr, &memory);
		if (result != VK_SUCCESS)
			return Fail("vkAllocateMemory (image)", result);
		vk.vkBindImageMemory(session->device, image, memory, 0);
		if (view)
		{
			const bool depth = format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D16_UNORM;
			VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			viewInfo.image = image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = format;
			viewInfo.subresourceRange = {(VkImageAspectFlags)(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1};
			result = vk.vkCreateImageView(session->device, &viewInfo, nullptr, view);
			if (result != VK_SUCCESS)
				return Fail("vkCreateImageView", result);
		}
		return true;
	}

	// The eye buffers, the compositor's pipeline and its command buffer, then Hmd2
	bool StartSession(XrSession_T* session)
	{
		VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = session->family;
		VkResult result = vk.vkCreateCommandPool(session->device, &poolInfo, nullptr, &session->commandPool);
		if (result != VK_SUCCESS)
			return Fail("vkCreateCommandPool", result);
		VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocate.commandPool = session->commandPool;
		allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 1;
		result = vk.vkAllocateCommandBuffers(session->device, &allocate, &session->commands);
		if (result != VK_SUCCESS)
			return Fail("vkAllocateCommandBuffers", result);
		VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		result = vk.vkCreateFence(session->device, &fenceInfo, nullptr, &session->fence);
		if (result != VK_SUCCESS)
			return Fail("vkCreateFence", result);

		// the eye buffers: host-visible, coherent, device-local where there is such a type, mapped and
		// 256-byte aligned for Hmd2's descriptor (as port/bettervr/VrPS5.cpp, M2)
		VkPhysicalDeviceMemoryProperties props{};
		vk.vkGetPhysicalDeviceMemoryProperties(session->physical, &props);
		const VkDeviceSize bytes = (VkDeviceSize)2 * kEyeWidth * kEyeHeight * 4;
		for (int i = 0; i < kEyeBuffers; i++)
		{
			VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			info.size = bytes;
			info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			result = vk.vkCreateBuffer(session->device, &info, nullptr, &session->eyeBuffers[i]);
			if (result != VK_SUCCESS)
				return Fail("vkCreateBuffer", result);
			VkMemoryRequirements requirements{};
			vk.vkGetBufferMemoryRequirements(session->device, session->eyeBuffers[i], &requirements);
			const uint32_t type = MemoryType(props, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (type == UINT32_MAX)
				return Log("no host-visible coherent memory for an eye buffer"), false;
			VkMemoryAllocateInfo memoryInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
			memoryInfo.allocationSize = requirements.size;
			memoryInfo.memoryTypeIndex = type;
			result = vk.vkAllocateMemory(session->device, &memoryInfo, nullptr, &session->eyeMemory[i]);
			if (result != VK_SUCCESS)
				return Fail("vkAllocateMemory (eye buffer)", result);
			vk.vkBindBufferMemory(session->device, session->eyeBuffers[i], session->eyeMemory[i], 0);
			result = vk.vkMapMemory(session->device, session->eyeMemory[i], 0, VK_WHOLE_SIZE, 0, &session->eyeMapped[i]);
			if (result != VK_SUCCESS)
				return Fail("vkMapMemory", result);
			if (reinterpret_cast<uintptr_t>(session->eyeMapped[i]) & 0xffu)
				return Log("eye buffer %d is mapped at %p, not 256-byte aligned", i, session->eyeMapped[i]), false;
			std::memset(session->eyeMapped[i], 0, (size_t)bytes);
			Log("eye buffer %d: %ux%u RGBA8 at %p (memory type %u)", i, 2 * kEyeWidth, kEyeHeight, session->eyeMapped[i], type);
		}

		// a 1x1 image for the compositor's slots that have nothing this frame
		if (!MakeImage(session, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, session->dummy,
				session->dummyMemory, &session->dummyView))
			return false;

		VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxLod = 0.0f;
		result = vk.vkCreateSampler(session->device, &samplerInfo, nullptr, &session->sampler);
		if (result != VK_SUCCESS)
			return Fail("vkCreateSampler", result);

		const VkDescriptorSetLayoutBinding bindings[] = {
			{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
			{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
			{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		};
		VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		setInfo.bindingCount = 3;
		setInfo.pBindings = bindings;
		result = vk.vkCreateDescriptorSetLayout(session->device, &setInfo, nullptr, &session->setLayout);
		if (result != VK_SUCCESS)
			return Fail("vkCreateDescriptorSetLayout", result);
		const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 80};
		VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &session->setLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &push;
		result = vk.vkCreatePipelineLayout(session->device, &layoutInfo, nullptr, &session->pipelineLayout);
		if (result != VK_SUCCESS)
			return Fail("vkCreatePipelineLayout", result);
		VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		moduleInfo.codeSize = sizeof(ps5xr::shaders::k_compose_comp);
		moduleInfo.pCode = ps5xr::shaders::k_compose_comp;
		VkShaderModule module = VK_NULL_HANDLE;
		result = vk.vkCreateShaderModule(session->device, &moduleInfo, nullptr, &module);
		if (result != VK_SUCCESS)
			return Fail("vkCreateShaderModule", result);
		VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
		pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
		pipelineInfo.layout = session->pipelineLayout;
		result = vk.vkCreateComputePipelines(session->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &session->pipeline);
		vk.vkDestroyShaderModule(session->device, module, nullptr);
		if (result != VK_SUCCESS)
			return Fail("vkCreateComputePipelines", result);
		const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
		VkDescriptorPoolCreateInfo descriptorPoolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		descriptorPoolInfo.maxSets = 1;
		descriptorPoolInfo.poolSizeCount = 2;
		descriptorPoolInfo.pPoolSizes = sizes;
		result = vk.vkCreateDescriptorPool(session->device, &descriptorPoolInfo, nullptr, &session->pool);
		if (result != VK_SUCCESS)
			return Fail("vkCreateDescriptorPool", result);
		VkDescriptorSetAllocateInfo setAllocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
		setAllocate.descriptorPool = session->pool;
		setAllocate.descriptorSetCount = 1;
		setAllocate.pSetLayouts = &session->setLayout;
		result = vk.vkAllocateDescriptorSets(session->device, &setAllocate, &session->set);
		if (result != VK_SUCCESS)
			return Fail("vkAllocateDescriptorSets", result);

		if (!RunOnce(session, [&](VkCommandBuffer commands) {
				ImageBarrier(commands, session->dummy, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
					VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
			}))
			return false;

		hmd2::set_logger(HmdLog);
		hmd2::ExternalImage external[kEyeBuffers] = {};
		for (int i = 0; i < kEyeBuffers; i++)
			external[i] = {session->eyeMapped[i], (size_t)bytes};
		hmd2::Config config;
		config.external = external;
		config.external_count = kEyeBuffers;
		config.image_width = 2 * kEyeWidth;
		config.image_height = kEyeHeight;
		config.mirror = false; // the TV keeps Cemu's own picture
		if (!hmd2::init(config))
		{
			const hmd2::Error error = hmd2::error();
			Log("hmd2 did not start, at %s (rc 0x%08x); the hmd2 lines above say more", error.step && error.step[0] ? error.step : "an unknown step",
				(unsigned)error.rc);
			return false;
		}
		session->hmdUp = true;
		s.fov = hmd2::field_of_view();
		Log("headset up: %dx%d per eye, %d buffers, %d Hz output, user %d, export table %d, FOV tangents out %.3f in %.3f top %.3f bottom %.3f",
			hmd2::eye_size(), hmd2::eye_height(), hmd2::buffer_count(), hmd2::refresh_hz(), hmd2::user_id(), hmd2::firmware_table(), s.fov.out,
			s.fov.in, s.fov.top, s.fov.bottom);
		return true;
	}

	// Frees what StartSession made; the GPU must be past it. After a GPU timeout nothing is freed.
	void EndSessionObjects(XrSession_T* session)
	{
		if (session->hmdUp)
		{
			hmd2::shutdown();
			session->hmdUp = false;
		}
		if (session->gpuFailed || s.stopped || session->device == VK_NULL_HANDLE || !vk.vkDestroyBuffer)
			return;
		VkDevice device = session->device;
		for (int i = 0; i < kEyeBuffers; i++)
		{
			if (session->eyeMapped[i])
				vk.vkUnmapMemory(device, session->eyeMemory[i]);
			if (session->eyeBuffers[i])
				vk.vkDestroyBuffer(device, session->eyeBuffers[i], nullptr);
			if (session->eyeMemory[i])
				vk.vkFreeMemory(device, session->eyeMemory[i], nullptr);
		}
		if (session->dummyView)
			vk.vkDestroyImageView(device, session->dummyView, nullptr);
		if (session->dummy)
			vk.vkDestroyImage(device, session->dummy, nullptr);
		if (session->dummyMemory)
			vk.vkFreeMemory(device, session->dummyMemory, nullptr);
		if (session->sampler)
			vk.vkDestroySampler(device, session->sampler, nullptr);
		if (session->pool)
			vk.vkDestroyDescriptorPool(device, session->pool, nullptr);
		if (session->pipeline)
			vk.vkDestroyPipeline(device, session->pipeline, nullptr);
		if (session->pipelineLayout)
			vk.vkDestroyPipelineLayout(device, session->pipelineLayout, nullptr);
		if (session->setLayout)
			vk.vkDestroyDescriptorSetLayout(device, session->setLayout, nullptr);
		if (session->fence)
			vk.vkDestroyFence(device, session->fence, nullptr);
		if (session->commandPool)
			vk.vkDestroyCommandPool(device, session->commandPool, nullptr);
	}

	// Where a quad layer falls in one eye, as the eye's UVs, from the eye's pose and FOV: the
	// bounding box of its four corners' directions. False when a corner is behind the eye.
	bool QuadInEye(const XrCompositionLayerQuad& quad, const Pose& quadPose, const Pose& eye, const XrFovf& fov, float rect[4])
	{
		const Pose toEye = Inverse(eye);
		const float tl = Tan(fov.angleLeft), tr = Tan(fov.angleRight), tu = Tan(fov.angleUp), td = Tan(fov.angleDown);
		float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
		for (int corner = 0; corner < 4; corner++)
		{
			const Vec3 local{(corner & 1 ? 0.5f : -0.5f) * quad.size.width, (corner & 2 ? 0.5f : -0.5f) * quad.size.height, 0};
			const Vec3 world = Add(quadPose.p, Rotate(quadPose.q, local));
			const Vec3 inEye = Add(toEye.p, Rotate(toEye.q, world));
			if (inEye.z >= -0.01f)
				return false;
			const float x = inEye.x / -inEye.z, y = inEye.y / -inEye.z;
			const float u = (x - tl) / (tr - tl), v = (tu - y) / (tu - td);
			u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, v), v1 = std::max(v1, v);
		}
		rect[0] = u0, rect[1] = v0, rect[2] = u1, rect[3] = v1;
		return u1 > u0 && v1 > v0;
	}

	XrResult Lost()
	{
		return XR_ERROR_SESSION_LOST;
	}
}

// ==== the OpenXR functions ======================================================================
extern "C" {

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateApiLayerProperties(uint32_t capacity, uint32_t* count, XrApiLayerProperties*)
{
	*count = 0;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(const char* layer, uint32_t capacity, uint32_t* count, XrExtensionProperties* props)
{
	static const char* const names[] = {XR_KHR_VULKAN_ENABLE_EXTENSION_NAME, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME};
	static const uint32_t versions[] = {XR_KHR_vulkan_enable_SPEC_VERSION, XR_KHR_composition_layer_depth_SPEC_VERSION};
	*count = 2;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < 2)
		return XR_ERROR_SIZE_INSUFFICIENT;
	for (int i = 0; i < 2; i++)
	{
		std::snprintf(props[i].extensionName, XR_MAX_EXTENSION_NAME_SIZE, "%s", names[i]);
		props[i].extensionVersion = versions[i];
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo* info, XrInstance* instance)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return XR_ERROR_RUNTIME_FAILURE;
	if (s.instance != XR_NULL_HANDLE)
		return XR_ERROR_LIMIT_REACHED;
	for (uint32_t i = 0; i < info->enabledExtensionCount; i++)
	{
		const char* name = info->enabledExtensionNames[i];
		if (std::strcmp(name, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME) != 0 && std::strcmp(name, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME) != 0)
		{
			Log("xrCreateInstance: extension %s is not offered", name);
			return XR_ERROR_EXTENSION_NOT_PRESENT;
		}
	}
	s.instance = new XrInstance_T{};
	*instance = s.instance;
	Log("instance for %s (%s)", info->applicationInfo.applicationName, info->applicationInfo.engineName);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance)
{
	std::lock_guard lock(s.lock);
	if (instance != s.instance)
		return XR_ERROR_HANDLE_INVALID;
	delete s.instance;
	s.instance = XR_NULL_HANDLE;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProperties(XrInstance, XrInstanceProperties* props)
{
	props->runtimeVersion = XR_MAKE_VERSION(0, 3, 0);
	std::snprintf(props->runtimeName, XR_MAX_RUNTIME_NAME_SIZE, "PS5-ECAM hmd2 (BotW-BetterVR on PS5)");
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrResultToString(XrInstance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE])
{
	std::snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XrResult %d", (int)value);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrStructureTypeToString(XrInstance, XrStructureType value, char buffer[XR_MAX_STRUCTURE_NAME_SIZE])
{
	std::snprintf(buffer, XR_MAX_STRUCTURE_NAME_SIZE, "XrStructureType %d", (int)value);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent(XrInstance, XrEventDataBuffer* event)
{
	std::lock_guard lock(s.lock);
	if (s.events.empty())
		return XR_EVENT_UNAVAILABLE;
	*event = s.events.front();
	s.events.pop_front();
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetSystem(XrInstance, const XrSystemGetInfo* info, XrSystemId* system)
{
	if (info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY)
		return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
	*system = 1;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* props)
{
	props->systemId = 1;
	props->vendorId = 0x054c; // Sony
	std::snprintf(props->systemName, XR_MAX_SYSTEM_NAME_SIZE, "PlayStation VR2");
	props->graphicsProperties = {kEyeWidth, kEyeHeight, 1};
	props->trackingProperties = {XR_TRUE, XR_TRUE};
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacity, uint32_t* count,
	XrEnvironmentBlendMode* modes)
{
	*count = 1;
	if (capacity >= 1)
		modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurations(XrInstance, XrSystemId, uint32_t capacity, uint32_t* count, XrViewConfigurationType* types)
{
	*count = 1;
	if (capacity >= 1)
		types[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetViewConfigurationProperties(XrInstance, XrSystemId, XrViewConfigurationType type, XrViewConfigurationProperties* props)
{
	if (type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	props->viewConfigurationType = type;
	props->fovMutable = XR_FALSE;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType type, uint32_t capacity, uint32_t* count,
	XrViewConfigurationView* views)
{
	if (type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	*count = 2;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < 2)
		return XR_ERROR_SIZE_INSUFFICIENT;
	for (int i = 0; i < 2; i++)
	{
		views[i].recommendedImageRectWidth = views[i].maxImageRectWidth = kEyeWidth;
		views[i].recommendedImageRectHeight = views[i].maxImageRectHeight = kEyeHeight;
		views[i].recommendedSwapchainSampleCount = views[i].maxSwapchainSampleCount = 1;
	}
	return XR_SUCCESS;
}

// -- XR_KHR_vulkan_enable ------------------------------------------------------------------------
XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanGraphicsRequirementsKHR(XrInstance, XrSystemId, XrGraphicsRequirementsVulkanKHR* requirements)
{
	requirements->minApiVersionSupported = XR_MAKE_VERSION(1, 0, 0);
	requirements->maxApiVersionSupported = XR_MAKE_VERSION(1, 4, 0);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanGraphicsDeviceKHR(XrInstance, XrSystemId, VkInstance instance, VkPhysicalDevice* physical)
{
	auto enumerate = (PFN_vkEnumeratePhysicalDevices)vk_icdGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
	uint32_t count = 1;
	*physical = VK_NULL_HANDLE;
	if (!enumerate || (enumerate(instance, &count, physical) < 0) || *physical == VK_NULL_HANDLE)
		return XR_ERROR_RUNTIME_FAILURE;
	return XR_SUCCESS;
}

static XrResult NoExtensions(uint32_t capacity, uint32_t* count, char* buffer)
{
	*count = 1;
	if (capacity >= 1)
		buffer[0] = '\0';
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanInstanceExtensionsKHR(XrInstance, XrSystemId, uint32_t capacity, uint32_t* count, char* buffer)
{
	return NoExtensions(capacity, count, buffer);
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanDeviceExtensionsKHR(XrInstance, XrSystemId, uint32_t capacity, uint32_t* count, char* buffer)
{
	return NoExtensions(capacity, count, buffer);
}

// -- session ---------------------------------------------------------------------------------------
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSession(XrInstance, const XrSessionCreateInfo* info, XrSession* out)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return Lost();
	if (s.session != XR_NULL_HANDLE)
		return XR_ERROR_LIMIT_REACHED;
	const XrGraphicsBindingVulkanKHR* binding = nullptr;
	for (auto* next = static_cast<const XrBaseInStructure*>(info->next); next; next = next->next)
		if (next->type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR)
			binding = reinterpret_cast<const XrGraphicsBindingVulkanKHR*>(next);
	if (!binding)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	auto* session = new XrSession_T{};
	session->instance = binding->instance;
	session->physical = binding->physicalDevice;
	session->device = binding->device;
	session->family = binding->queueFamilyIndex;
	s.session = session;
	if (!vk.Load(binding->instance, binding->device))
	{
		Log("the driver lacks a function the compositor needs");
		s.session = XR_NULL_HANDLE;
		delete session;
		return XR_ERROR_RUNTIME_FAILURE;
	}
	vk.vkGetDeviceQueue(session->device, binding->queueFamilyIndex, binding->queueIndex, &session->queue);
	Log("session on Cemu's device %p, queue family %u index %u", (void*)binding->device, binding->queueFamilyIndex, binding->queueIndex);
	if (!StartSession(session))
	{
		EndSessionObjects(session);
		s.session = XR_NULL_HANDLE;
		delete session;
		return XR_ERROR_RUNTIME_FAILURE;
	}
	s.gripPitch = BetterVR_PS5_Setting("gripPitch", 0.0f);
	s.aimPitch = BetterVR_PS5_Setting("aimPitch", 0.0f);
	Log("controllers: grip pitch %.1f deg, aim pitch %.1f deg (bettervr.json's gripPitch, aimPitch)", s.gripPitch, s.aimPitch);
	OpenHands();
	*out = session;
	QueueState(XR_SESSION_STATE_IDLE);
	QueueState(XR_SESSION_STATE_READY);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySession(XrSession session)
{
	std::lock_guard lock(s.lock);
	if (session != s.session)
		return XR_ERROR_HANDLE_INVALID;
	if (!session->gpuFailed && !s.stopped && vk.vkDeviceWaitIdle)
	{
		std::lock_guard queue(ps5xr::QueueMutex());
		vk.vkDeviceWaitIdle(session->device);
	}
	CloseHands();
	s.attached = false;
	EndSessionObjects(session);
	Log("session ended after %llu frames (%llu without layers, %llu without an Hmd2 frame)", (unsigned long long)session->frames,
		(unsigned long long)session->emptyFrames, (unsigned long long)session->skipped);
	s.session = XR_NULL_HANDLE;
	delete session;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession(XrSession session, const XrSessionBeginInfo*)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return Lost();
	if (session->running)
		return XR_ERROR_SESSION_RUNNING;
	session->running = true;
	QueueState(XR_SESSION_STATE_SYNCHRONIZED);
	QueueState(XR_SESSION_STATE_VISIBLE);
	QueueState(XR_SESSION_STATE_FOCUSED);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(XrSession session)
{
	std::lock_guard lock(s.lock);
	if (!session->running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	session->running = false;
	QueueState(XR_SESSION_STATE_IDLE);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrRequestExitSession(XrSession session)
{
	std::lock_guard lock(s.lock);
	if (!session->running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	QueueState(XR_SESSION_STATE_STOPPING);
	return XR_SUCCESS;
}

// -- spaces ----------------------------------------------------------------------------------------
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateReferenceSpaces(XrSession, uint32_t capacity, uint32_t* count, XrReferenceSpaceType* types)
{
	static const XrReferenceSpaceType all[] = {XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL, XR_REFERENCE_SPACE_TYPE_STAGE};
	*count = 3;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < 3)
		return XR_ERROR_SIZE_INSUFFICIENT;
	std::memcpy(types, all, sizeof(all));
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo* info, XrSpace* space)
{
	if (info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_VIEW && info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL &&
		info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE)
		return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
	*space = new XrSpace_T{info->referenceSpaceType, false, FromXr(info->poseInReferenceSpace)};
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetReferenceSpaceBoundsRect(XrSession, XrReferenceSpaceType, XrExtent2Df* bounds)
{
	*bounds = {0, 0};
	return XR_SPACE_BOUNDS_UNAVAILABLE;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession, const XrActionSpaceCreateInfo* info, XrSpace* space)
{
	std::lock_guard lock(s.lock);
	auto* created = new XrSpace_T{XR_REFERENCE_SPACE_TYPE_STAGE, true, FromXr(info->poseInActionSpace)};
	created->poseAction = info->action;
	created->hand = HandOf(info->subactionPath);
	*space = created;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace space)
{
	delete space;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* location)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return Lost();
	for (auto* next = static_cast<XrBaseOutStructure*>(location->next); next; next = next->next)
		if (next->type == XR_TYPE_SPACE_VELOCITY)
			reinterpret_cast<XrSpaceVelocity*>(next)->velocityFlags = 0;
	if (base->action)
	{
		location->locationFlags = 0;
		location->pose = {{0, 0, 0, 1}, {0, 0, 0}};
		return XR_SUCCESS;
	}
	Pose spacePose;
	if (space->action)
	{
		// the hand's tracked pose, the grip or aim tilt (bettervr.json), then the space's own offset
		const XrAction_T* action = space->poseAction;
		const int which = space->hand;
		const State::Hand* hand = nullptr;
		bool aim = false;
		if (action && action->set && action->set->active)
			for (const Binding& binding : action->bindings)
				if (IsPose(binding.component) && (which < 0 || binding.hand == which) && s.hands[binding.hand].poseValid)
				{
					hand = &s.hands[binding.hand];
					aim = binding.component == Component::AimPose;
					break;
				}
		if (!hand)
		{
			location->locationFlags = 0;
			location->pose = {{0, 0, 0, 1}, {0, 0, 0}};
			return XR_SUCCESS;
		}
		const Pose tilt{AxisAngle(1, 0, 0, aim ? s.aimPitch : s.gripPitch), {}};
		spacePose = Compose(Compose(hand->pose, tilt), space->offset);
		const Pose toBase = Inverse(SpacePose(base, time));
		for (auto* next = static_cast<XrBaseOutStructure*>(location->next); next; next = next->next)
			if (next->type == XR_TYPE_SPACE_VELOCITY && hand->velocityValid)
			{
				auto* velocity = reinterpret_cast<XrSpaceVelocity*>(next);
				const Vec3 linear = Rotate(toBase.q, hand->linear), angular = Rotate(toBase.q, hand->angular);
				velocity->linearVelocity = {linear.x, linear.y, linear.z};
				velocity->angularVelocity = {angular.x, angular.y, angular.z};
				velocity->velocityFlags = XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
			}
	}
	else
		spacePose = SpacePose(space, time);
	const Pose pose = Compose(Inverse(SpacePose(base, time)), spacePose);
	location->pose = ToXr(pose);
	location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT |
		XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(XrSession session, const XrViewLocateInfo* info, XrViewState* state, uint32_t capacity, uint32_t* count,
	XrView* views)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return Lost();
	*count = 2;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < 2)
		return XR_ERROR_SIZE_INSUFFICIENT;
	const Pose head = HeadAt(info->displayTime);
	const Pose base = SpacePose(info->space, info->displayTime);
	const Pose toBase = Inverse(base);
	const float out = std::atan(s.fov.out), in = std::atan(s.fov.in), up = std::atan(s.fov.top), down = std::atan(s.fov.bottom);
	for (int eye = 0; eye < 2; eye++)
	{
		const Pose eyePose{head.q, Add(head.p, Rotate(head.q, s.eyeOffset[eye]))};
		views[eye].pose = ToXr(Compose(toBase, eyePose));
		// the outer edge is the left one for the left eye and the right one for the right eye
		views[eye].fov = eye == 0 ? XrFovf{-out, in, up, -down} : XrFovf{-in, out, up, -down};
		if (eye == 0)
		{
			s.located[s.nextLocated] = {eyePose.p, head};
			s.nextLocated = (s.nextLocated + 1) % 32;
		}
	}
	state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT |
		XR_VIEW_STATE_POSITION_TRACKED_BIT;
	return XR_SUCCESS;
}

// -- swapchains ------------------------------------------------------------------------------------
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainFormats(XrSession, uint32_t capacity, uint32_t* count, int64_t* formats)
{
	static const int64_t all[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT};
	*count = 4;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < 4)
		return XR_ERROR_SIZE_INSUFFICIENT;
	std::memcpy(formats, all, sizeof(all));
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* out)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return Lost();
	const VkFormat format = (VkFormat)info->format;
	const bool color = format != VK_FORMAT_D32_SFLOAT;
	if (format != VK_FORMAT_R8G8B8A8_SRGB && format != VK_FORMAT_B8G8R8A8_SRGB && format != VK_FORMAT_R8G8B8A8_UNORM && color)
		return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	usage |= color ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if (info->usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT)
		usage |= VK_IMAGE_USAGE_STORAGE_BIT;
	auto* swapchain = new XrSwapchain_T{};
	swapchain->format = format;
	swapchain->width = info->width;
	swapchain->height = info->height;
	swapchain->color = color;
	for (int i = 0; i < kSwapchainImages; i++)
		if (!MakeImage(session, info->width, info->height, format, usage, swapchain->images[i], swapchain->memory[i], &swapchain->views[i]))
			return XR_ERROR_RUNTIME_FAILURE;
	// the layout the application finds them in, and leaves them in at release
	const bool ok = RunOnce(session, [&](VkCommandBuffer commands) {
		for (int i = 0; i < kSwapchainImages; i++)
			ImageBarrier(commands, swapchain->images[i], color ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
				color ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, 0,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	});
	if (!ok)
		return XR_ERROR_RUNTIME_FAILURE;
	Log("swapchain %ux%u format %d (%s)", info->width, info->height, (int)format, color ? "colour" : "depth, not shown");
	*out = swapchain;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain swapchain)
{
	std::lock_guard lock(s.lock);
	if (s.session && !s.session->gpuFailed && !s.stopped)
	{
		{
			std::lock_guard queue(ps5xr::QueueMutex());
			vk.vkDeviceWaitIdle(s.session->device);
		}
		for (int i = 0; i < kSwapchainImages; i++)
		{
			if (swapchain->views[i])
				vk.vkDestroyImageView(s.session->device, swapchain->views[i], nullptr);
			if (swapchain->images[i])
				vk.vkDestroyImage(s.session->device, swapchain->images[i], nullptr);
			if (swapchain->memory[i])
				vk.vkFreeMemory(s.session->device, swapchain->memory[i], nullptr);
		}
	}
	delete swapchain;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* images)
{
	*count = kSwapchainImages;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < kSwapchainImages)
		return XR_ERROR_SIZE_INSUFFICIENT;
	auto* vkImages = reinterpret_cast<XrSwapchainImageVulkanKHR*>(images);
	for (int i = 0; i < kSwapchainImages; i++)
		vkImages[i].image = swapchain->images[i];
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index)
{
	std::lock_guard lock(s.lock);
	*index = swapchain->next;
	swapchain->next = (swapchain->next + 1) % kSwapchainImages;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo*)
{
	// xrEndFrame waits for its own reads before it returns, so an acquired image is always free
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*)
{
	std::lock_guard lock(s.lock);
	swapchain->released = (int)((swapchain->next + kSwapchainImages - 1) % kSwapchainImages);
	return XR_SUCCESS;
}

// -- frames ----------------------------------------------------------------------------------------
XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(XrSession session, const XrFrameWaitInfo*, XrFrameState* state)
{
	// hmd2's pacing naps here: not under the lock, so poses can be located meanwhile
	if (s.stopped)
		return Lost();
	hmd2::wait_slot();
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return Lost();
	if (!session->frameOpen)
		session->frameOpen = hmd2::begin_frame();
	state->predictedDisplayPeriod = kFramePeriod;
	if (!session->frameOpen)
	{
		if (session->skipped++ % 600 == 0)
			Log("no Hmd2 frame (BeginFrame rc 0x%08x), %llu so far", (unsigned)hmd2::error().rc, (unsigned long long)session->skipped);
		state->predictedDisplayTime = Now() + kFramePeriod;
		state->shouldRender = XR_FALSE;
		return XR_SUCCESS;
	}
	state->predictedDisplayTime = (XrTime)hmd2::display_time_us() * 1000;
	state->shouldRender = XR_TRUE;
	// the eyes' offsets from the head, from this frame's tracker result, when it has a plausible one
	const hmd2::Poses poses = hmd2::predicted_poses();
	if (poses.head.tracked && poses.left.tracked && poses.right.tracked)
	{
		const Pose head = FromHmd(poses.head), toHead = Inverse(head);
		s.eyeOffset[0] = Rotate(toHead.q, Sub(FromHmd(poses.left).p, head.p));
		s.eyeOffset[1] = Rotate(toHead.q, Sub(FromHmd(poses.right).p, head.p));
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(XrSession, const XrFrameBeginInfo*)
{
	return s.stopped ? Lost() : XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(XrSession session, const XrFrameEndInfo* info)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return Lost();
	if (!session->frameOpen || session->gpuFailed)
		return XR_SUCCESS;

	const XrCompositionLayerProjection* projection = nullptr;
	const XrCompositionLayerQuad* quad = nullptr;
	for (uint32_t i = 0; i < info->layerCount; i++)
	{
		if (info->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && !projection)
			projection = reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[i]);
		else if (info->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_QUAD && !quad)
			quad = reinterpret_cast<const XrCompositionLayerQuad*>(info->layers[i]);
	}

	// the pose to submit: the head pose the submitted left view was located from
	Pose renderHead = HeadAt(info->displayTime);
	struct
	{
		int32_t eyeSize[2];
		uint32_t flags;
		uint32_t pad;
		float source[2][4];
		float hudRect[2][4];
	} params{{(int32_t)kEyeWidth, (int32_t)kEyeHeight}, 0, 0, {}, {}};
	VkImageView eyeViews[2] = {session->dummyView, session->dummyView};
	VkImage eyeImages[2] = {};
	Pose eyePoses[2];
	XrFovf eyeFov[2] = {};
	if (projection && projection->viewCount >= 2)
	{
		const Pose base = SpacePose(projection->space, info->displayTime);
		for (int eye = 0; eye < 2; eye++)
		{
			const XrCompositionLayerProjectionView& view = projection->views[eye];
			const XrSwapchain_T* swapchain = view.subImage.swapchain;
			eyePoses[eye] = Compose(base, FromXr(view.pose));
			eyeFov[eye] = view.fov;
			if (!swapchain || swapchain->released < 0 || !swapchain->color)
				continue;
			eyeViews[eye] = swapchain->views[swapchain->released];
			eyeImages[eye] = swapchain->images[swapchain->released];
			const XrRect2Di& r = view.subImage.imageRect;
			params.source[eye][0] = (float)r.offset.x / swapchain->width;
			params.source[eye][1] = (float)r.offset.y / swapchain->height;
			params.source[eye][2] = (float)(r.offset.x + r.extent.width) / swapchain->width;
			params.source[eye][3] = (float)(r.offset.y + r.extent.height) / swapchain->height;
			params.flags |= 1u << eye;
		}
		for (const auto& located : s.located)
			if (std::fabs(located.leftEye.x - eyePoses[0].p.x) < 1e-5f && std::fabs(located.leftEye.y - eyePoses[0].p.y) < 1e-5f &&
				std::fabs(located.leftEye.z - eyePoses[0].p.z) < 1e-5f)
			{
				renderHead = located.head;
				break;
			}
	}
	else
	{
		// no projection layer this frame: the HUD alone, placed with the eyes as they are now
		const Pose head = HeadAt(info->displayTime);
		const float out = std::atan(s.fov.out), in = std::atan(s.fov.in), up = std::atan(s.fov.top), down = std::atan(s.fov.bottom);
		for (int eye = 0; eye < 2; eye++)
		{
			eyePoses[eye] = {head.q, Add(head.p, Rotate(head.q, s.eyeOffset[eye]))};
			eyeFov[eye] = eye == 0 ? XrFovf{-out, in, up, -down} : XrFovf{-in, out, up, -down};
		}
	}
	VkImageView hudView = session->dummyView;
	VkImage hudImage = VK_NULL_HANDLE;
	if (quad && quad->subImage.swapchain && quad->subImage.swapchain->released >= 0 && quad->subImage.swapchain->color)
	{
		const XrSwapchain_T* swapchain = quad->subImage.swapchain;
		const Pose quadPose = Compose(SpacePose(quad->space, info->displayTime), FromXr(quad->pose));
		bool any = false;
		for (int eye = 0; eye < 2; eye++)
		{
			if (!QuadInEye(*quad, quadPose, eyePoses[eye], eyeFov[eye], params.hudRect[eye]))
				params.hudRect[eye][0] = params.hudRect[eye][1] = 2.0f, params.hudRect[eye][2] = params.hudRect[eye][3] = 3.0f;
			else
				any = true;
		}
		if (any)
		{
			hudView = swapchain->views[swapchain->released];
			hudImage = swapchain->images[swapchain->released];
			params.flags |= 4u;
			if (quad->layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT)
				params.flags |= 8u;
		}
	}
	if ((params.flags & 7u) == 0)
		session->emptyFrames++;

	const hmd2::EyeBuffer eye = hmd2::acquire();
	const int index = eye.index >= 0 && eye.index < kEyeBuffers ? eye.index : 0;
	VkDescriptorImageInfo images[3] = {{session->sampler, eyeViews[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
		{session->sampler, eyeViews[1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {session->sampler, hudView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
	VkDescriptorBufferInfo buffer{session->eyeBuffers[index], 0, VK_WHOLE_SIZE};
	VkWriteDescriptorSet writes[3] = {};
	for (auto& write : writes)
	{
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = session->set;
	}
	writes[0].dstBinding = 0, writes[0].descriptorCount = 2, writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, writes[0].pImageInfo = images;
	writes[1].dstBinding = 1, writes[1].descriptorCount = 1, writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, writes[1].pImageInfo = images + 2;
	writes[2].dstBinding = 2, writes[2].descriptorCount = 1, writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, writes[2].pBufferInfo = &buffer;
	vk.vkUpdateDescriptorSets(session->device, 3, writes, 0, nullptr);

	// the images the application wrote, readable here and given back in its layout after
	VkImage sampled[3] = {eyeImages[0], eyeImages[1], hudImage};
	if (sampled[1] == sampled[0])
		sampled[1] = VK_NULL_HANDLE;
	const bool ok = RunOnce(session, [&](VkCommandBuffer commands) {
		for (VkImage image : sampled)
			if (image)
				ImageBarrier(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		vk.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, session->pipeline);
		vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, session->pipelineLayout, 0, 1, &session->set, 0, nullptr);
		vk.vkCmdPushConstants(commands, session->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
		vk.vkCmdDispatch(commands, (2 * kEyeWidth + 7) / 8, (kEyeHeight + 7) / 8, 1);
		for (VkImage image : sampled)
			if (image)
				ImageBarrier(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_ACCESS_SHADER_READ_BIT, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
		// the GPU wrote the buffer: visible to Hmd2 before the flip (HLA-PSVR2's barrier, as M2's)
		VkBufferMemoryBarrier toHost{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
		toHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
		toHost.srcQueueFamilyIndex = toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toHost.buffer = session->eyeBuffers[index];
		toHost.size = VK_WHOLE_SIZE;
		vk.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
			nullptr, 1, &toHost, 0, nullptr);
	});
	if (!ok)
	{
		Log("the compositor's GPU work failed; the headset stops here and nothing is freed");
		return XR_ERROR_RUNTIME_FAILURE;
	}
	session->frameOpen = false;
	hmd2::submit(ToHmd(renderHead));
	if (++session->frames % 600 == 0)
	{
		const hmd2::PaceStats pace = hmd2::take_pace();
		const hmd2::FlipInfo flip = hmd2::flip_status();
		Log("%llu frames: nap %.2f ms, BeginFrame %.2f ms, ahead %.2f ms, step %.2f ms; flip shown %llu pending %d; repeated %d, queue full %d, "
			"missed %d, errors %d, without layers %llu, skipped %llu, worn %d",
			(unsigned long long)session->frames, pace.nap_ms, pace.call_ms, pace.ahead_ms, pace.step_ms, (unsigned long long)flip.shown, flip.pending,
			hmd2::repeated_frames(), hmd2::queue_full_waits(), hmd2::missed_frames(), hmd2::frame_errors(), (unsigned long long)session->emptyFrames,
			(unsigned long long)session->skipped, hmd2::worn() ? 1 : 0);
	}
	return XR_SUCCESS;
}

// -- paths and actions -------------------------------------------------------------------------------
XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance, const char* string, XrPath* path)
{
	std::lock_guard lock(s.lock);
	for (size_t i = 0; i < s.paths.size(); i++)
		if (s.paths[i] == string)
			return *path = i + 1, XR_SUCCESS;
	s.paths.emplace_back(string);
	*path = s.paths.size();
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrPathToString(XrInstance, XrPath path, uint32_t capacity, uint32_t* count, char* buffer)
{
	std::lock_guard lock(s.lock);
	if (path == XR_NULL_PATH || path > s.paths.size())
		return XR_ERROR_PATH_INVALID;
	const std::string& text = s.paths[path - 1];
	*count = (uint32_t)text.size() + 1;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < *count)
		return XR_ERROR_SIZE_INSUFFICIENT;
	std::memcpy(buffer, text.c_str(), *count);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance, const XrActionSetCreateInfo* info, XrActionSet* set)
{
	*set = new XrActionSet_T{info->actionSetName};
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyActionSet(XrActionSet set)
{
	std::lock_guard lock(s.lock);
	for (XrAction_T* action : s.actions)
		if (action->set == set)
			action->set = nullptr;
	delete set;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet set, const XrActionCreateInfo* info, XrAction* action)
{
	std::lock_guard lock(s.lock);
	auto* created = new XrAction_T{info->actionType, set};
	s.actions.push_back(created);
	*action = created;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyAction(XrAction action)
{
	std::lock_guard lock(s.lock);
	s.actions.erase(std::remove(s.actions.begin(), s.actions.end(), action), s.actions.end());
	delete action;
	return XR_SUCCESS;
}

// The Oculus Touch profile's bindings are kept (the Sense controllers stand in for it); every other
// profile's are accepted and not used
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance, const XrInteractionProfileSuggestedBinding* suggested)
{
	std::lock_guard lock(s.lock);
	if (suggested->interactionProfile == XR_NULL_PATH || suggested->interactionProfile > s.paths.size())
		return XR_ERROR_PATH_INVALID;
	if (s.paths[suggested->interactionProfile - 1] != "/interaction_profiles/oculus/touch_controller")
		return XR_SUCCESS;
	s.touchProfile = suggested->interactionProfile;
	s.touchSuggested = true;
	int kept = 0, unknown = 0;
	for (uint32_t i = 0; i < suggested->countSuggestedBindings; i++)
	{
		const XrActionSuggestedBinding& suggestion = suggested->suggestedBindings[i];
		Binding binding{};
		if (suggestion.binding == XR_NULL_PATH || suggestion.binding > s.paths.size() || !ParseTouchPath(s.paths[suggestion.binding - 1], binding))
		{
			unknown++;
			continue;
		}
		suggestion.action->bindings.push_back(binding);
		kept++;
	}
	Log("Touch profile: %d bindings onto the Sense controllers, %d not mapped", kept, unknown);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession, const XrSessionActionSetsAttachInfo*)
{
	std::lock_guard lock(s.lock);
	s.attached = true;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetCurrentInteractionProfile(XrSession, XrPath topLevel, XrInteractionProfileState* state)
{
	std::lock_guard lock(s.lock);
	const int hand = HandOf(topLevel);
	state->interactionProfile = XR_NULL_PATH;
	if (hand >= 0 && s.touchSuggested && s.hands[hand].handle >= 0)
		state->interactionProfile = s.touchProfile;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession, const XrActionsSyncInfo* info)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return Lost();
	if (s.handPath[0] == XR_NULL_PATH)
	{
		// the hands' paths, once: BetterVR names them before it syncs
		for (int i = 0; i < 2; i++)
		{
			const std::string name = std::string("/user/hand/") + HandName(i);
			for (size_t p = 0; p < s.paths.size(); p++)
				if (s.paths[p] == name)
					s.handPath[i] = p + 1;
		}
	}
	for (XrAction_T* action : s.actions)
		if (action->set)
			action->set->active = false;
	for (uint32_t i = 0; i < info->countActiveActionSets; i++)
		info->activeActionSets[i].actionSet->active = true;
	ReadHands();
	s.syncTime = Now();
	for (int i = 0; i < 2; i++)
	{
		auto& hand = s.hands[i];
		if (hand.vibrating && s.syncTime >= hand.hapticUntil)
			SetVibration(i, 0.0f);
	}
	for (XrAction_T* action : s.actions)
		for (int hand = 0; hand < 2; hand++)
		{
			float value[2];
			ActionValue(action, hand, value);
			action->changed[hand] = value[0] != action->last[hand][0] || value[1] != action->last[hand][1];
			if (action->changed[hand])
				action->changedAt[hand] = s.syncTime;
			action->last[hand][0] = value[0], action->last[hand][1] = value[1];
		}
	return XR_SUCCESS;
}

namespace
{
	// an action's state for xrGetActionState*: active, value, changed since the last sync, when
	struct Read
	{
		bool active = false;
		float value[2] = {};
		bool changed = false;
		XrTime changedAt = 0;
	};

	Read ReadAction(const XrActionStateGetInfo* info)
	{
		Read read;
		const XrAction_T* action = info->action;
		const int which = HandOf(info->subactionPath);
		if (which == -2)
			return read;
		read.active = ActionValue(action, which, read.value);
		for (int hand = 0; hand < 2; hand++)
			if ((which < 0 || which == hand) && action->changed[hand])
			{
				read.changed = true;
				read.changedAt = std::max(read.changedAt, action->changedAt[hand]);
			}
		return read;
	}
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession, const XrActionStateGetInfo* info, XrActionStateBoolean* state)
{
	std::lock_guard lock(s.lock);
	const Read read = ReadAction(info);
	state->isActive = read.active;
	state->currentState = read.value[0] > 0.5f;
	state->changedSinceLastSync = read.changed;
	state->lastChangeTime = read.changedAt;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession, const XrActionStateGetInfo* info, XrActionStateFloat* state)
{
	std::lock_guard lock(s.lock);
	const Read read = ReadAction(info);
	state->isActive = read.active;
	state->currentState = read.value[0];
	state->changedSinceLastSync = read.changed;
	state->lastChangeTime = read.changedAt;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(XrSession, const XrActionStateGetInfo* info, XrActionStateVector2f* state)
{
	std::lock_guard lock(s.lock);
	const Read read = ReadAction(info);
	state->isActive = read.active;
	state->currentState = {read.value[0], read.value[1]};
	state->changedSinceLastSync = read.changed;
	state->lastChangeTime = read.changedAt;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(XrSession, const XrActionStateGetInfo* info, XrActionStatePose* state)
{
	std::lock_guard lock(s.lock);
	const XrAction_T* action = info->action;
	const int which = HandOf(info->subactionPath);
	state->isActive = XR_FALSE;
	if (which == -2 || !action->set || !action->set->active)
		return XR_SUCCESS;
	for (const Binding& binding : action->bindings)
		if (IsPose(binding.component) && (which < 0 || binding.hand == which) && s.hands[binding.hand].connected &&
			s.hands[binding.hand].registered)
			state->isActive = XR_TRUE;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateBoundSourcesForAction(XrSession, const XrBoundSourcesForActionEnumerateInfo*, uint32_t, uint32_t* count, XrPath*)
{
	*count = 0;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetInputSourceLocalizedName(XrSession, const XrInputSourceLocalizedNameGetInfo*, uint32_t capacity, uint32_t* count,
	char* buffer)
{
	*count = 1;
	if (capacity >= 1)
		buffer[0] = '\0';
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(XrSession, const XrHapticActionInfo* info, const XrHapticBaseHeader* header)
{
	std::lock_guard lock(s.lock);
	if (!header || header->type != XR_TYPE_HAPTIC_VIBRATION)
		return XR_SUCCESS;
	const auto* vibration = reinterpret_cast<const XrHapticVibration*>(header);
	const int which = HandOf(info->subactionPath);
	// XR_MIN_HAPTIC_DURATION (-1) asks for the shortest the device does: a frame
	const XrDuration duration = vibration->duration <= 0 ? kFramePeriod : std::min<XrDuration>(vibration->duration, 2'000'000'000);
	for (const Binding& binding : info->action->bindings)
		if (binding.component == Component::Haptic && (which < 0 || binding.hand == which))
		{
			s.hands[binding.hand].hapticUntil = Now() + duration;
			SetVibration(binding.hand, vibration->amplitude);
		}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrStopHapticFeedback(XrSession, const XrHapticActionInfo* info)
{
	std::lock_guard lock(s.lock);
	const int which = HandOf(info->subactionPath);
	for (const Binding& binding : info->action->bindings)
		if (binding.component == Component::Haptic && (which < 0 || binding.hand == which))
			SetVibration(binding.hand, 0.0f);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* function)
{
#define XR_ENTRY(fn)                                       \
	if (std::strcmp(name, #fn) == 0)                       \
	{                                                      \
		*function = reinterpret_cast<PFN_xrVoidFunction>(&fn); \
		return XR_SUCCESS;                                 \
	}
	XR_ENTRY(xrGetInstanceProcAddr) XR_ENTRY(xrEnumerateApiLayerProperties) XR_ENTRY(xrEnumerateInstanceExtensionProperties)
	XR_ENTRY(xrCreateInstance) XR_ENTRY(xrDestroyInstance) XR_ENTRY(xrGetInstanceProperties) XR_ENTRY(xrResultToString)
	XR_ENTRY(xrStructureTypeToString) XR_ENTRY(xrPollEvent) XR_ENTRY(xrGetSystem) XR_ENTRY(xrGetSystemProperties)
	XR_ENTRY(xrEnumerateEnvironmentBlendModes) XR_ENTRY(xrEnumerateViewConfigurations) XR_ENTRY(xrGetViewConfigurationProperties)
	XR_ENTRY(xrEnumerateViewConfigurationViews) XR_ENTRY(xrGetVulkanGraphicsRequirementsKHR) XR_ENTRY(xrGetVulkanGraphicsDeviceKHR)
	XR_ENTRY(xrGetVulkanInstanceExtensionsKHR) XR_ENTRY(xrGetVulkanDeviceExtensionsKHR) XR_ENTRY(xrCreateSession) XR_ENTRY(xrDestroySession)
	XR_ENTRY(xrBeginSession) XR_ENTRY(xrEndSession) XR_ENTRY(xrRequestExitSession) XR_ENTRY(xrEnumerateReferenceSpaces)
	XR_ENTRY(xrCreateReferenceSpace) XR_ENTRY(xrGetReferenceSpaceBoundsRect) XR_ENTRY(xrCreateActionSpace) XR_ENTRY(xrDestroySpace)
	XR_ENTRY(xrLocateSpace) XR_ENTRY(xrLocateViews) XR_ENTRY(xrEnumerateSwapchainFormats) XR_ENTRY(xrCreateSwapchain)
	XR_ENTRY(xrDestroySwapchain) XR_ENTRY(xrEnumerateSwapchainImages) XR_ENTRY(xrAcquireSwapchainImage) XR_ENTRY(xrWaitSwapchainImage)
	XR_ENTRY(xrReleaseSwapchainImage) XR_ENTRY(xrWaitFrame) XR_ENTRY(xrBeginFrame) XR_ENTRY(xrEndFrame) XR_ENTRY(xrStringToPath)
	XR_ENTRY(xrPathToString) XR_ENTRY(xrCreateActionSet) XR_ENTRY(xrDestroyActionSet) XR_ENTRY(xrCreateAction) XR_ENTRY(xrDestroyAction)
	XR_ENTRY(xrSuggestInteractionProfileBindings) XR_ENTRY(xrAttachSessionActionSets) XR_ENTRY(xrGetCurrentInteractionProfile)
	XR_ENTRY(xrSyncActions) XR_ENTRY(xrGetActionStateBoolean) XR_ENTRY(xrGetActionStateFloat) XR_ENTRY(xrGetActionStateVector2f)
	XR_ENTRY(xrGetActionStatePose) XR_ENTRY(xrEnumerateBoundSourcesForAction) XR_ENTRY(xrGetInputSourceLocalizedName)
	XR_ENTRY(xrApplyHapticFeedback) XR_ENTRY(xrStopHapticFeedback)
#undef XR_ENTRY
	*function = nullptr;
	return XR_ERROR_FUNCTION_UNSUPPORTED;
}

// app/emulator.cpp, before every LoadExec (PS5Cemu_VrStop): Hmd2 off, from any thread. A title that
// ends with VrTracker2 live faults the tracker (third_party/hmd2/hmd2.h).
void BetterVR_PS5_Stop()
{
	std::lock_guard lock(s.lock);
	if (s.stopped.exchange(true))
		return;
	for (int i = 0; i < 2; i++)
		if (s.hands[i].vibrating)
			SetVibration(i, 0.0f);
	if (s.session && s.session->hmdUp)
	{
		hmd2::shutdown();
		s.session->hmdUp = false;
		Log("headset off after %llu frames, before the app is replaced", (unsigned long long)s.session->frames);
	}
}

} // extern "C"
