// SPDX-License-Identifier: GPL-3.0-or-later
// BotW-BetterVR on PS5, M3: a small OpenXR runtime over hmd2 (third_party/hmd2, from photo-splat-vr),
// built into BetterVR's module (port/bettervr/module.cmake). It implements what BetterVR's Linux backend
// calls, and nothing else (PLAN.md, M3, option A, the way PS5XR does for HL:A):
//
//   instance, system   one of each: "PlayStation VR2", Vulkan (XR_KHR_vulkan_enable). It does not list
//                      XR_KHR_composition_layer_depth (BetterVR 1.0.0 then keeps its depth in a local
//                      attachment and makes no depth swapchains), and ignores the depth it is given
//                      when asked to take it: Hmd2 reprojects without depth.
//   session            on BetterVR's Vulkan binding (Cemu's device, queue family and queue): three
//                      host-visible eye buffers on that device, handed to hmd2::init as
//                      Config::external, as M2 does (port/bettervr/VrPS5.cpp)
//   spaces             VIEW, LOCAL and STAGE are hmd2's tracker space (+y up, -z forward, metres, as
//                      OpenXR's); VIEW follows the head. Options held for 1 s on the right Sense or the
//                      DualSense recenters the view: one seat (a yaw and an x/z offset, never a height)
//                      moves the head and both hands, so the app and the game need no change.
//   views              xrLocateViews: hmd2's predicted head pose for the time, its eyes' offsets from
//                      the frame's own result (else half a 63 mm IPD each side), and its FOV tangents
//   swapchains         three Vulkan images each (one 2D layer: 1.0.0's stereo is two swapchains), on Cemu's device. A released colour image is left in
//                      COLOR_ATTACHMENT_OPTIMAL, as the spec has the application leave it.
//   frames             xrWaitFrame is hmd2::wait_slot + begin_frame (Hmd2's pacing, 60 frames a
//                      second, each shown for two of its 120 Hz refreshes). xrEndFrame composes the
//                      projection layer and the HUD quad into the frame's eye buffer in one compute
//                      pass (shaders/compose.comp), on the binding's queue, waits for it, and submits
//                      it with the head pose the submitted views were rendered from; with asyncCompose
//                      (the default; bettervr.json can turn it off) the frame pump's next xrWaitFrame
//                      does the wait and the submit, and Cemu's GPU thread goes on to the next frame.
//                      A mono/cutscene frame (no stereo render: a rune or map screen, a cutscene) is
//                      the 2D picture alone in front of the player, black around it, never an eye picture stretched
//                      across both eyes. Hmd2's social screen shows that view on the TV when
//                      bettervr.json's tv is on (default).
//   actions            (M4) the PS VR2 Sense controllers as the Oculus Touch profile BetterVR binds:
//                      left Square/Triangle = X/Y, right Cross/Circle = A/B, L1/R1 = squeeze,
//                      L3/R3 = thumbstick click, the stick, the trigger (the larger of its two bytes),
//                      Create = menu. Read with scePadVrControllerReadState (types 3 and 4, opened
//                      for Hmd2's user), posed by VrTracker2 (hmd2::register_controller /
//                      controller_pose) with velocities from successive poses; haptics through
//                      hmd2::sense_set_vibration (libScePad's Sense exports). The DualSense stays the game's GamePad (Cemu's), which
//                      BetterVR merges with these. The adaptive triggers are here too: the bow's draw on R2 and the
//                      shield's guard on L2, through libScePad's scePadSetTriggerEffect (the DualSense) and its
//                      Sense trigger exports (the experiment behind bettervr.json's senseTriggers). Layout from
//                      photo-splat-vr's pad_input.h and HLA-PSVR2's psvr2.c (third_party/hmd2/reference); none of it
//                      seen on a console.
//
// The PS5 has one graphics queue, which Cemu's GPU thread, BetterVR's submit thread and this
// compositor share: every submission to it holds ps5xr::QueueMutex() (patches/bettervr 0002 for
// BetterVR's and Cemu's, through BetterVR's layer).
//
// The app ends a game with a LoadExec: BetterVR_PS5_Stop (app/emulator.cpp, before it) turns Hmd2 off
// from any thread, and every call after it answers XR_ERROR_SESSION_LOST.

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "hmd2.h"
#include "shaders.h"
#include "triggers.h"

extern "C" {
// libScePad (the app's pad.cpp has called scePadInit)
int scePadOpen(int user, int type, int index, const void* parameters);
int scePadGetHandle(int user, int type, int index);
int scePadClose(int handle);
int scePadVrControllerReadState(int handle, void* state);
// the DualSense's sample, for the recenter's Options (port/ps5/pad.h's ps5pad::Data)
int scePadReadState(int handle, void* data);
// port/ps5/pad.cpp: player 1's open DualSense handle, for the adaptive triggers (-1: none)
int32_t PS5Cemu_PadHandle(int player);
// port/bettervr/VrPS5.cpp: a number from bettervr.json, or fallback
float BetterVR_PS5_Setting(const char* name, float fallback);
// the same file: write one of bettervr.json's values back (the menu's Sense trigger rows)
int BetterVR_PS5_WriteSetting(const char* name, float value);
// port/bettervr/VrPS5.cpp: bettervr.json's "profile" (true/false or 1/0), read once
bool BetterVR_PS5_Profile();
// port/cemu/PS5PadController.cpp: the DualSense while a VR session runs (Cemu's own rumble output is held back,
// so the game's pattern and BetterVR's mixer never fire the pad twice). The levels are 0..1, large motor then small.
void PS5Pad_SetExternalRumble(bool on);
void PS5Pad_ExternalRumble(float large, float small);
}

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__PROSPERO__)
// patches/cemu 0055: the GPU thread's swap and VR frame end, summed until the [frame] line takes
// them, and the GPU's own busy time (patch 0028). They live in Cemu, which the module links with.
extern std::atomic<uint64_t> g_ps5FrameSwapNs;
extern std::atomic<uint64_t> g_ps5FrameVrEndNs;
extern std::atomic<uint32_t> g_ps5FrameProfileSamples;
extern std::atomic<uint64_t> g_ps5GpuBusyNs;
extern bool g_ps5GpuTimestamps;
// patches/cemu 0057: the 5 s [draws] line's counters, added by the Vulkan renderer's draw path
extern std::atomic<uint64_t> g_ps5DrawsTotal;
extern std::atomic<uint64_t> g_ps5DrawsMultiview;
extern std::atomic<uint64_t> g_ps5DrawsReplay;
extern std::atomic<uint64_t> g_ps5RecordNs;
extern std::atomic<uint64_t> g_ps5ReplayNs;
// patches/cemu 0058: the same line's draw path counts, and with bettervr.json's profile the stereo pass's phases in
// TSC ticks (Cemu's _rdtscFrequency a second): [0] the sequences' setup, [1] the draws, within them [2] uniforms,
// [3] indices, [4] vertex and uniform buffers, [5] the pipeline and descriptor sets
extern std::atomic<uint64_t> g_ps5DrawSequences;
extern std::atomic<uint64_t> g_ps5DescriptorSetsMade;
extern std::atomic<uint64_t> g_ps5PipelinesMade;
extern std::atomic<uint64_t> g_ps5DrawPhaseTicks[6];
extern std::atomic<uint64_t> g_ps5IndexDecodes;
extern std::atomic<uint64_t> g_ps5IndexBytes;
// patches/cemu 0059: bettervr.json's indexCache, the kept index lists used again and found changed, and the bytes
// they hold
extern std::atomic<uint64_t> g_ps5IndexReused;
extern std::atomic<uint64_t> g_ps5IndexChanged;
extern std::atomic<uint64_t> g_ps5IndexKeptBytes;
// patches/cemu 0061: bettervr.json's textureMemo, the texture units answered from the memo instead of the view
// lookup cache
extern std::atomic<uint64_t> g_ps5TextureUnitsReused;
// patches/cemu 0062: bettervr.json's passCounts, the render passes begun and the command buffers submitted
extern std::atomic<uint64_t> g_ps5PassesBegun;
extern std::atomic<uint64_t> g_ps5Submits;
extern uint64_t _rdtscFrequency;
#endif

// the port's (ps5/log.cpp): a line of the boot log
void PS5Cemu_LogLine(std::string_view line);
// RADV's, linked into the app: where the runtime's own Vulkan calls come from (not through BetterVR's layer)
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
extern "C" uint64_t sceKernelGetProcessTime(void);
// port/bettervr/VrPS5.cpp: the app goes back to the library with the reason
extern "C" void BetterVR_PS5_VrFailed(const char* what);

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
	// the compose shader's push constants: the eye size, the flags, the two source rects, the HUD's
	// two homographies and the first output pixel of the dispatch (see compose.comp)
	constexpr uint32_t kComposeParamsBytes = 128;
	constexpr XrDuration kFramePeriod = 16'666'667; // ns: a frame every two of Hmd2's 120 Hz refreshes
	constexpr float kHalfIpd = 0.0315f;
	constexpr uint64_t kGpuTimeoutNs = 2'000'000'000ull;
	// a compose that has not finished this long after its first 2 s timeout ends the VR run (the VR-failed path)
	constexpr uint64_t kGpuGiveUpUs = 30'000'000ull;

	// How the compositor draws a frame. Stereo is both eyes' pictures with the HUD quad over them, the
	// frame BetterVR renders in play. MonoQuad is the 2D picture alone in front of the player with black
	// around it: a mono/cutscene frame (no stereo render, a black-bar or third-person cutscene, a rune or
	// map screen), which must not be a stale or mono eye picture stretched across both eyes.
	enum class ComposeMode
	{
		Unknown,
		Stereo,
		MonoQuad,
	};

	void Log(const char* format, ...)
	{
		char text[512];
		va_list args;
		va_start(args, format);
		std::vsnprintf(text, sizeof(text), format, args);
		va_end(args);
		PS5Cemu_LogLine(std::string("[vr-xr] ") + text);
	}

	// The Vibration page's lines (its test buttons, the devices' state changes): "[vib] ..." in the boot log,
	// the same tag BetterVR's own side uses, so one search finds all of them.
	void VibLog(const char* format, ...)
	{
		char text[512];
		va_list args;
		va_start(args, format);
		std::vsnprintf(text, sizeof(text), format, args);
		va_end(args);
		PS5Cemu_LogLine(std::string("[vib] ") + text);
	}

	void HmdLog(const char* line)
	{
		PS5Cemu_LogLine(std::string("[vr-xr] ") + line);
	}

	// The recenter's own lines: "[recenter] ..." in the boot log, one a hold, with the yaw and the offset
	void RecenterLog(const char* format, ...)
	{
		char text[512];
		va_list args;
		va_start(args, format);
		std::vsnprintf(text, sizeof(text), format, args);
		va_end(args);
		PS5Cemu_LogLine(std::string("[recenter] ") + text);
	}

	// The in-game menu's probe (BetterVR_PS5_MenuProbe): how often the headset's frame loop turns, whole-run
	// counts that the menu's open and close lines subtract. Atomics: the loop's threads, Cemu's GPU thread
	// and the app's render thread each touch them.
	// (hmd2's own frames_submitted counts the submits)
	std::atomic<uint64_t> s_waitFrames{0}, s_endFrames{0}, s_tvPresents{0};
	std::atomic<bool> s_live{false}; // Hmd2 is up for a session: s.session->hmdUp, readable without s.lock
	std::atomic<uint64_t> s_menuOpenedAt{0}, s_menuLoggedAt{0}; // sceKernelGetProcessTime, us; 0 closed
	std::atomic<uint64_t> s_menuAtOpen[4] = {};					// the counters when it opened

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
	// The shortest rotation that turns `from` into `to` (both directions, not necessarily unit).
	Quat FromTo(const Vec3& from, const Vec3& to)
	{
		const float d = from.x * to.x + from.y * to.y + from.z * to.z;
		Vec3 axis{from.y * to.z - from.z * to.y, from.z * to.x - from.x * to.z, from.x * to.y - from.y * to.x};
		if (d < -0.999999f)
		{
			// 180 degrees: any axis perpendicular to `from` will do
			Vec3 any = std::fabs(from.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
			axis = {from.y * any.z - from.z * any.y, from.z * any.x - from.x * any.z, from.x * any.y - from.y * any.x};
			const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
			return length > 0.0f ? Quat{axis.x / length, axis.y / length, axis.z / length, 0.0f} : Quat{};
		}
		const Quat q{axis.x, axis.y, axis.z, 1.0f + d};
		const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
		return length > 0.0f ? Quat{q.x / length, q.y / length, q.z / length, q.w / length} : Quat{};
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
	bool gaze = false;
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
		X, Y, A, B, Menu, Squeeze, Trigger, Thumbstick, ThumbstickX, ThumbstickY, ThumbstickClick, GripPose, AimPose, GazePose, Haptic
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
	// sRGB swapchain images as UNORM, so the compositor reads the bytes the application wrote
	// without the sampler's sRGB decode; VK_FORMAT_UNDEFINED when the format is not sRGB
	VkImageView sampleViews[kSwapchainImages] = {};
	uint32_t next = 0;	   // the next acquire
	int released = -1;	   // the last image released, the one a layer shows
};

struct XrSpace_T
{
	XrReferenceSpaceType type = XR_REFERENCE_SPACE_TYPE_STAGE;
	bool action = false; // an action space: a hand's grip or aim pose
	bool gaze = false;   // an action space for XR_EXT_eye_gaze_interaction's gaze pose
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
	// the HUD's mip chain: a copy of the HUD image at each level, made each frame so the compose
	// minifies ~2.4x through a level instead of one bilinear tap (see EnsureHudMips)
	VkImage hudMip = VK_NULL_HANDLE;
	VkDeviceMemory hudMipMemory = VK_NULL_HANDLE;
	VkImageView hudMipView = VK_NULL_HANDLE;
	uint32_t hudMipWidth = 0, hudMipHeight = 0, hudMipLevels = 0;
	VkFormat hudMipFormat = VK_FORMAT_UNDEFINED;
	bool hudMipsSupported = false;
	VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	VkDescriptorSet set = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commands = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;
	// xrEndFrame's own command buffer and fence: its wait runs without s.lock, so no other call's one-time commands may share them
	VkCommandBuffer composeCommands = VK_NULL_HANDLE;
	VkFence composeFence = VK_NULL_HANDLE;
	bool composePending = false; // the compose was submitted and its fence has not been seen signalled
	bool composeBusy = false;    // a FinishCompose is waiting on composeFence without s.lock
	// the submitted compose's frame still has to wait for its fence and go to Hmd2 (FinishCompose), with the
	// render pose and compose flags xrEndFrame gave it
	bool composeFinishDue = false;
	Pose composeHead{};
	uint32_t composeFlags = 0;
	uint64_t gpuStallSince = 0;  // sceKernelGetProcessTime of the first timeout of the stall in progress, 0 when none
	uint64_t gpuTimeouts = 0, gpuStuckSkips = 0, gpuRecovered = 0;

	uint64_t frames = 0, emptyFrames = 0, skipped = 0;
	// xrWaitFrame calls that found the previous frame still open (the frame pump runs one ahead), and xrEndFrame
	// calls that found no frame open (nothing is submitted for them)
	uint64_t waitsAhead = 0, endsWithoutFrame = 0;
	uint64_t lateBegins = 0; // xrEndFrame calls that found no open frame and began one themselves
	uint64_t lastFrameLogAt = 0; // sceKernelGetProcessTime of the last 600-frame line
	// frames whose render pose was found among the located views, and frames where it was not
	uint64_t poseFound = 0, poseLost = 0;
	// frames whose re-shown pair kept the render pose it was published with (poseKeep)
	uint64_t poseKept = 0;
	uint64_t monoOnQuad = 0, planeQuads = 0; // frames whose mono picture rode the HUD quad, frames whose quad was drawn as a plane
	// the 1x compose's eye copy has said so once (see xrEndFrame's copy path)
	bool eyeCopyLogged = false;
	// frames Hmd2's TV mirror took, and frames it was asked for but did not take
	uint64_t tvShown = 0, tvSkipped = 0;
	// the 5 s [draws] line: when its window began, xrEndFrame's own time and the compose fence waits
	// in the window (xrEndFrame's, or the frame pump's with asyncCompose), and how many frames the
	// window had. Draw counts and the stereo/replay times come from Cemu's counters (patches/cemu 0057,
	// 0058), taken and cleared at each line.
	uint64_t drawLogAt = 0, drawLogFrames = 0, drawLogEndUs = 0, drawLogFenceUs = 0;
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
	X(vkCmdBindDescriptorSets) X(vkCmdPushConstants) X(vkCmdDispatch) X(vkCmdCopyImage) X(vkCmdBlitImage)            \
	X(vkCmdCopyImageToBuffer)                                                                                          \
	X(vkQueueSubmit) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(vkDeviceWaitIdle)
#define VK_MEMBER(name) PFN_##name name = nullptr;
		VK_FUNCTIONS(VK_MEMBER)
		PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
		PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties = nullptr;
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
			vkGetPhysicalDeviceFormatProperties =
				(PFN_vkGetPhysicalDeviceFormatProperties)vk_icdGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFormatProperties");
			return all && vkGetPhysicalDeviceMemoryProperties && vkGetPhysicalDeviceFormatProperties;
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
		XrPath touchProfile = XR_NULL_PATH;
		XrPath gazeProfile = XR_NULL_PATH; // XR_EXT_eye_gaze_interaction's profile, when suggested
		Pose handLocated[2]; // the hand spaces' last located poses, in their base space, for the frame log
		bool touchSuggested = false, attached = false;
		bool padTakenOver = false; // the DualSense's rumble is the mixer's (PS5Pad_SetExternalRumble) while this session runs
		bool gazeSuggested = false;
		std::vector<XrAction_T*> actions;
		struct Hand
		{
			int handle = -1;
			bool owned = false; // opened here, not the launcher's handle (the menus open them first)
			bool registered = false, connected = false, reportedMissing = false;
			XrTime nextOpen = 0;
			uint32_t buttons = 0, rawLogged = 0xffffffffu;
			float stickX = 0, stickY = 0, trigger = 0;
			bool poseValid = false;
			// hmd2's controller_pose has no velocity: successive poses derive it. Three samples give
			// the light two-interval average and dt comes from the pose timestamps, not the read times.
			struct Sample
			{
				Pose pose;
				XrTime time = 0;
			};
			static constexpr int kVelocitySamples = 3;
			Sample samples[kVelocitySamples];
			int sampleCount = 0, nextSample = 0;
			Pose pose;
			Vec3 linear, angular;
			XrTime hapticUntil = 0;
			bool vibrating = false, poseLogged = false;
			int vibrationsLogged = 0;
			int lastLevel = -1; // the level libScePad last took on this handle; -1: none, or the call failed
		} hands[2];
		float gripYaw = 17, gripPitch = 32, gripRoll = 30, aimPitch = 0; // degrees: the grip and aim poses' turn, bettervr.json's keys
		// How a frame is composed, fixed (1.0.0 has no use for the 0.9.23 mod's presentation switches): a mono
		// picture (identical views) rides the HUD quad with black around it, the HUD quad is drawn as the plane it
		// is, and a re-shown pair (BetterVR's submit thread shows the last one again) keeps the render pose it was
		// published with while the room still holds it
		static constexpr bool monoQuad = true, quadPlane = true, poseKeep = true;
		// the mode the last composed frame used, so a switch between stereo and mono-quad is logged once
		ComposeMode composeMode = ComposeMode::Unknown;
		// The render pose of the picture the submit thread is showing again: the left view it was
		// rendered from and the tracker's head pose it was located with (not the seated one, which is
		// the game's frame), so a pair re-shown beyond the ring still reprojects from where it was rendered
		// instead of from the live head.
		bool shownKnown = false, poseKeepLogged = false;
		Vec3 shownEye{};
		Pose shownHead{}; // the tracker's pose of the pair the compositor shows again, for Hmd2
		uint64_t shownAtFrame = 0;
		bool reproject = true; // bettervr.json's "reproject": 0 submits every frame as rendered from the display time's pose
		// bettervr.json's "asyncCompose": the frame pump's xrWaitFrame, not xrEndFrame on Cemu's GPU thread, waits for a
		// frame's compose and submits it to Hmd2 (FinishCompose); on by default, "asyncCompose": 0 turns it off
		bool asyncCompose = true;
		std::condition_variable composeIdle; // signalled when a FinishCompose's fence wait ends
		bool tv = true; // bettervr.json's "tv": 0 keeps the TV dark while VR runs
		// bettervr.json's "profile": one [frame] line every 10 s, splitting the GPU thread's swap, the
		// VR frame end, the compose fence and hmd2's submit; off by default, nothing is timed when off
		bool profile = false;
		uint64_t profileAt = 0; // sceKernelGetProcessTime of the last [frame] line, us
		uint64_t profileFenceNs = 0, profileSubmitNs = 0;
		uint64_t profileFrames = 0;
		uint64_t profileGpuAt = 0; // g_ps5GpuBusyNs at the last line
		// adaptive triggers: the Sense controllers' effects (bettervr.json's "senseTriggers"); the DualSense's are
		// the setting in BetterVR's own menu
		bool senseTriggers = false;
		// the VR start's milestones (the log lines "VR start n/5"): when the session began, the swapchains made, and
		// whether the first submitted frame and the first one with a stereo picture are logged
		uint64_t sessionStartedAt = 0; // sceKernelGetProcessTime, us
		int swapchainsMade = 0;
		bool firstFrameLogged = false, firstStereoLogged = false;
		XrTime syncTime = 0;
		hmd2::Fov fov{1.0f, 1.0f, 1.0f, 1.0f};
		Vec3 eyeOffset[2] = {{-kHalfIpd, 0, 0}, {kHalfIpd, 0, 0}}; // in the head's frame
		// the head pose each pair of located views was computed from: xrEndFrame submits with the one
		// whose left eye is the submitted left view. `trackerHead` is the tracker's own pose, which is what
		// Hmd2 reprojects from: the seat belongs to the game's frame, not to the room the headset measures.
		struct Located
		{
			Vec3 leftEye;
			Pose trackerHead;
		};
		Located located[256]; // BetterVR's pose thread locates at 250 Hz: 32 held only 128 ms
		int nextLocated = 0;
		Pose lastHead;
		// -- the view's recenter (Options held 1 s) --
		// The seat is the tracker pose the view's forward and horizontal origin were moved to: a turn about y
		// and an x/z offset, never a height, so BetterVR's seated stage height is unchanged. Compose(seat, pose)
		// is where a tracker pose appears once seated, for the head and both hands; the spaces' own offsets stay
		// as they are, which is what moves the world under them.
		Pose seat;
		// A seat's line waits for the panel it must name: the hold's input, the head's yaw before the seat, the
		// seat's offset, the head's position, and how many frames the line has waited for a 2D panel.
		struct SeatReport
		{
			bool pending = false;
			const char* source = "";
			float before = 0; // the tracker head's yaw, degrees, before the seat
			Vec3 offset{};	  // the seat's x/z, metres
			Vec3 head{};	  // the tracker head's position the seat was made from
			uint32_t waited = 0;
		} seatReport;
		// The recenter PollOptions asked for: xrWaitFrame applies it at the display time of the frame it hands
		// out, so the pose is the one the player is looking at when the hold reaches its second, not the CPU's.
		bool recenterRequested = false;
		const char* recenterSource = "";
		// One more per seat, which BetterVR's 2D panel reads (patches/bettervr 0030): a recenter turns the world
		// under the head, so a panel whose gaze was locked before it must be placed again.
		std::atomic<uint32_t> recenterGeneration{0};
		// one Options input's hold: which second it started at, and the hold already spent
		struct OptionsHold
		{
			bool down = false, ignored = false, fired = false; // ignored: it began as the touchpad's chord
			XrTime at = 0;
		} senseOptions, padOptions;
		// whether the press became the recenter, one per input: BetterVR's controls.cpp reads it on the release
		// (BetterVR_PS5_OptionsRecentered), so this clock alone decides between the recenter and the game's pause
		std::atomic<bool> senseOptionsRecentered{false}, padOptionsRecentered{false};
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

	// hmd2 reports the Sense pose (a position and orientation at a display time), not a velocity, so
	// the hand's velocity is the change between successive samples: the linear part from the
	// positions, the angular part from the quaternion delta, dt from the samples' own timestamps.
	// At most three samples (two intervals) are averaged, and a span longer than 100 ms drops the
	// oldest, so a hitch reads as the newest pair rather than as a very slow swing.
	void SampleHand(State::Hand& hand, const Pose& pose, XrTime time)
	{
		const int newest = (hand.nextSample + State::Hand::kVelocitySamples - 1) % State::Hand::kVelocitySamples;
		if (hand.sampleCount > 0 && time < hand.samples[newest].time)
			return; // a stale timestamp: keep the newer samples
		hand.pose = pose;
		hand.poseValid = true;
		if (hand.sampleCount > 0 && time == hand.samples[newest].time)
			hand.samples[newest].pose = pose; // one predicted frame read twice: no new interval
		else
		{
			hand.samples[hand.nextSample] = {pose, time};
			hand.nextSample = (hand.nextSample + 1) % State::Hand::kVelocitySamples;
			hand.sampleCount = std::min(hand.sampleCount + 1, State::Hand::kVelocitySamples);
		}
		hand.linear = {};
		hand.angular = {};
		if (hand.sampleCount < 2)
			return;

		int count = hand.sampleCount;
		int oldest = (hand.nextSample + State::Hand::kVelocitySamples - count) % State::Hand::kVelocitySamples;
		const int latest = (hand.nextSample + State::Hand::kVelocitySamples - 1) % State::Hand::kVelocitySamples;
		while (count > 2 && hand.samples[latest].time - hand.samples[oldest].time > 100'000'000)
		{
			oldest = (oldest + 1) % State::Hand::kVelocitySamples;
			count--;
		}
		const State::Hand::Sample& from = hand.samples[oldest];
		const State::Hand::Sample& to = hand.samples[latest];
		const double dt = (double)(to.time - from.time) / 1e9;
		if (dt <= 0.001 || dt > 0.2)
			return;
		hand.linear = {(float)((to.pose.p.x - from.pose.p.x) / dt), (float)((to.pose.p.y - from.pose.p.y) / dt),
			(float)((to.pose.p.z - from.pose.p.z) / dt)};
		// the rotation from the oldest pose to the newest, as an axis times its angle per second
		Quat d = Mul(to.pose.q, Conj(from.pose.q));
		if (d.w < 0)
			d = {-d.x, -d.y, -d.z, -d.w};
		const float sinHalf = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
		const float angle = 2.0f * std::atan2(sinHalf, d.w);
		const float k = sinHalf > 1e-6f ? angle / sinHalf / (float)dt : 0.0f;
		hand.angular = {d.x * k, d.y * k, d.z * k};
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
	// the DualSense's own bits (port/ps5/pad.h): its touchpad's click arms the app's chord, and the system menu's bit
	// means nothing there is down
	constexpr uint32_t kPadTouchpad = 0x00100000, kPadIntercepted = 0x80000000;
	constexpr XrDuration kPadRetry = 2'000'000'000; // as photo-splat-vr and HLA-PSVR2: every 2 s
	constexpr int kVibrationModeCompatible = 2;			 // the mode Ghost-VR sets on the Sense before its first vibration

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
			// the launcher's menus open the pads before VR: take that handle rather than opening the
			// pad a second time, and leave it open for the menus when VR stops
			const int found = scePadGetHandle(user, kPadTypeSense[i], 0);
			int opened = -1;
			int handle = found;
			const bool owned = handle < 0;
			if (handle < 0)
				opened = handle = scePadOpen(user, kPadTypeSense[i], 0, nullptr);
			if (handle < 0)
			{
				if (!hand.reportedMissing)
					Log("no %s Sense controller for user %d (scePadGetHandle -> 0x%08x, scePadOpen -> 0x%08x); trying every 2 s",
						HandName(i), user, (unsigned)found, (unsigned)opened);
				hand.reportedMissing = true;
				continue;
			}
			hand.handle = handle;
			hand.lastLevel = -1;
			hand.owned = owned;
			hand.reportedMissing = false;
			const int mode = hmd2::sense_set_vibration_mode(handle, kVibrationModeCompatible);
			hand.registered = hmd2::register_controller(handle, i == 1);
			Log("%s Sense controller open (handle %d), VrTracker2 registration %s", HandName(i), handle,
				hand.registered ? "done" : "FAILED (no pose for this hand)");
			Log("%s Sense controller vibration mode -> 0x%08x", HandName(i), (unsigned)mode);
		}
	}

	void CloseHands()
	{
		for (auto& hand : s.hands)
		{
			if (hand.handle >= 0 && hand.owned)
				scePadClose(hand.handle);
			hand = State::Hand{};
		}
	}

	// once a frame (xrSyncActions): buttons, stick, trigger and pose of each hand
	// Whether Options on the right Sense is down, as of the last sync: BetterVR's controls.cpp reads it
	// (BetterVR_PS5_OptionsDown), as the Touch profile gives that button no action
	std::atomic<bool> s_optionsDown{false};

	void ReadHands()
	{
		OpenHands();
		// stored when the sync is read, whichever way the loop leaves: a hand that is gone or intercepted is not down
		struct Publish
		{
			bool optionsDown = false;
			~Publish() { s_optionsDown.store(optionsDown, std::memory_order_release); }
		} publish;
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
			if (i == 1 && (buttons & kOptions) != 0)
				publish.optionsDown = true;
			hand.buttons = buttons;
			hand.stickX = Axis(state[kSenseStickX]);
			hand.stickY = -Axis(state[kSenseStickY]); // its byte is down positive
			hand.trigger = (float)std::max(state[kSenseTriggerA], state[kSenseTriggerB]) / 255.0f;
			if (!hand.registered)
				continue;
			float pos[3], quat[4];
			const bool posed = hmd2::controller_pose(hand.handle, pos, quat);
			// diagnostics, not a fix: whether VrTracker2 gives the hand a pose (unverified on hardware), once
			// when that changes (at most 20 lines), with the pose and the head's, to read the tracker's origin
			if (posed != hand.poseLogged)
			{
				hand.poseLogged = posed;
				static int s_poseLogs = 0;
				if (s_poseLogs++ < 20)
				{
					if (posed)
						Log("%s Sense pose from VrTracker2: pos %.3f %.3f %.3f quat %.3f %.3f %.3f %.3f (head %.3f %.3f %.3f)", HandName(i), pos[0], pos[1],
							pos[2], quat[0], quat[1], quat[2], quat[3], s.lastHead.p.x, s.lastHead.p.y, s.lastHead.p.z);
					else
						Log("%s Sense pose: VrTracker2 gives none (registered %d)", HandName(i), hand.registered ? 1 : 0);
				}
			}
			if (!posed)
				continue;
			const Pose pose{{quat[0], quat[1], quat[2], quat[3]}, {pos[0], pos[1], pos[2]}};
			// controller_pose asks the tracker at Hmd2's display time, so that is the pose's timestamp
			const uint64_t display = hmd2::display_time_us();
			SampleHand(hand, pose, display != 0 ? (XrTime)display * 1000 : now);
		}
	}

	// -- the view's recenter (Options held 1 s) ----------------------------------------------------
	// The DualSense's button word, through the handle its triggers use; false when there is no pad or no sample.
	// Its 120 bytes are libScePad's ScePadData, of which the first word and the connected flag at 0x4c are read
	// (port/ps5/pad.h).
	bool PadButtons(uint32_t& buttons)
	{
		const int handle = PS5Cemu_PadHandle(0);
		if (handle < 0)
			return false;
		struct PadState
		{
			uint32_t buttons;
			uint8_t reserved[72];
			int32_t connected; // 0x4c
			uint8_t rest[40];
		};
		static_assert(sizeof(PadState) == 120);
		PadState state{};
		if (scePadReadState(handle, &state) < 0 || !state.connected)
			return false;
		buttons = state.buttons;
		return true;
	}

	// The view's new seat: the head's forward becomes -z and its horizontal position the origin, at its height
	// (y is kept: BetterVR's stage offset is the height's own, so a seated calibration stays valid). One seat for
	// the head and both hands, below the app and the game. The line is written by xrEndFrame, when it can also
	// say where the 2D panel ended up.
	void Recenter(const Pose& head, const char* source)
	{
		const Vec3 forward = Rotate(head.q, {0, 0, -1});
		const float yaw = std::atan2(forward.x, -forward.z) * (180.0f / 3.14159265f);
		// a turn about +y by the heading itself, not its negative: it is what brings that forward to -z
		const Quat turn = AxisAngle(0, 1, 0, yaw);
		const Vec3 offset = Rotate(turn, head.p);
		s.seat = {turn, {-offset.x, 0.0f, -offset.z}};
		s.seatReport = {true, source, yaw, s.seat.p, head.p, 0};
		s.recenterGeneration.fetch_add(1, std::memory_order_release);
	}

	// The recenter a hold asked for, at the display time of the frame being handed out: the seat's pose is the
	// one that frame's views are located from, so the world forward and the 2D panel end up on that pose's yaw.
	void ApplyRecenter(XrTime displayTime)
	{
		if (!s.recenterRequested)
			return;
		s.recenterRequested = false;
		if (s.session == XR_NULL_HANDLE || !s.session->hmdUp)
			return;
		const hmd2::Pose sample = hmd2::predicted_pose((uint64_t)(displayTime / 1000));
		if (!sample.tracked)
		{
			RecenterLog("%s: the headset has no pose, nothing moved", s.recenterSource);
			return;
		}
		Recenter(FromHmd(sample), s.recenterSource);
	}

	// One Options input's hold: a press that began with the touchpad clicked is the app's chord (its second opens
	// the app's own menu), a release before the second is the game's pause (BetterVR's), and a second held is the
	// recenter, once a hold.
	void PollOptions(State::OptionsHold& hold, std::atomic<bool>& recentered, bool down, bool chord, const char* source)
	{
		const XrTime now = Now();
		if (down && !hold.down)
		{
			hold.at = now;
			hold.fired = false;
			hold.ignored = chord;
		}
		hold.down = down;
		if (chord && down)
			hold.ignored = true; // the touchpad joined the press: this hold is the chord's, not a recenter
		if (!down || hold.ignored || hold.fired || now - hold.at < 1'000'000'000)
		{
			recentered.store(hold.fired, std::memory_order_release);
			return;
		}
		hold.fired = true;
		recentered.store(true, std::memory_order_release);
		if (s.session == XR_NULL_HANDLE || !s.session->hmdUp)
			return;
		// the seat is not made here: xrWaitFrame makes it at the display time of the frame it hands out
		s.recenterSource = source;
		s.recenterRequested = true;
	}

	void PollRecenter()
	{
		// the right Sense's Options: the Touch profile gives it no action, so it is read from its state word here
		const State::Hand& right = s.hands[1];
		PollOptions(s.senseOptions, s.senseOptionsRecentered, right.connected && (right.buttons & kOptions) != 0, false, "right Sense");
		// the DualSense's Options; the touchpad's click is the app's chord and never a recenter
		uint32_t buttons = 0;
		const bool read = PadButtons(buttons);
		const bool chord = read && (buttons & kPadTouchpad) != 0;
		PollOptions(s.padOptions, s.padOptionsRecentered, read && (buttons & (kOptions | kPadIntercepted)) == kOptions, chord, "DualSense");
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
		return c == Component::GripPose || c == Component::AimPose || c == Component::GazePose;
	}

	bool IsGazeAction(const XrAction_T* action)
	{
		for (const Binding& binding : action->bindings)
			if (binding.component == Component::GazePose)
				return true;
		return false;
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
			if (binding.hand < 0 || binding.hand >= 2)
				continue; // the eye-gaze pose has no hand value
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
		// the hand's path, from the table: the hand paths are cached only at the first xrSyncActions,
		// which is after xrCreateActionSpace, so asking the cache there gave every space -2 (both hands)
		if (path != XR_NULL_PATH && path <= s.paths.size())
		{
			const std::string& name = s.paths[path - 1];
			if (name == "/user/hand/left")
				return 0;
			if (name == "/user/hand/right")
				return 1;
			if (name == "/user/eyes_ext")
				return -1; // gaze: not a hand, and its pose is the eye-gaze action
		}
		return -2;
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

	// The one place that sets a Sense controller's vibration (hmd2's libScePad lookup is behind it). The first
	// three calls with a level and every call that fails are in the log, with the level and libScePad's answer.
	void SetVibration(int i, float amplitude)
	{
		auto& hand = s.hands[i];
		if (hand.handle < 0)
			return;
		const uint8_t level = (uint8_t)std::lround(std::max(0.0f, std::min(1.0f, amplitude)) * 255.0f);
		if (hand.lastLevel == level)
			return; // the same level again (1.0.0 repeats its 30 ms pulses): libScePad is not called
		const int result = hmd2::sense_set_vibration(hand.handle, level);
		hand.lastLevel = result == 0 ? level : -1;
		if (level && (hand.vibrationsLogged < 3 || result != 0))
		{
			static int s_failures = 0;
			if (result == 0 || s_failures++ < 20)
				Log("%s Sense controller vibration level %u -> 0x%08x (%s)", HandName(i), (unsigned)level, (unsigned)result,
					hand.vibrationsLogged < 3 ? "one of the first" : "failed");
			hand.vibrationsLogged++;
		}
		hand.vibrating = level != 0;
	}

	// -- adaptive triggers ------------------------------------------------------------------------------------
	// The bridge is BetterVR_PS5_AdaptiveTriggers below (patches/bettervr's controls.cpp, once an input frame): the
	// bow's live draw on R2 and the shield's guard on L2. The DualSense's setter is a libScePad import under its NID
	// (third_party/hmd2/hmd2.cpp), and its effect defaults on; the Sense controllers take the same 0x78 parameter on
	// libScePad's both-hands setter, behind bettervr.json's "senseTriggers". A refused effect is not marked sent: the
	// same value goes out again 5 s later, and a refusal only concerns that payload, never the other device or any
	// later call.
	struct TriggerSent
	{
		bool valid = false;    // the last accepted param is in param (a refusal clears it)
		bool accepted = false; // an effect may be on the device: the off has not been sent since the last effect
		ps5trig::Param param{};
		uint64_t at = 0;      // sceKernelGetProcessTime, us
		uint64_t retryAt = 0; // after a refusal, nothing is sent before this time
	};
	TriggerSent s_triggerSent[2]; // 0 the DualSense, 1 the two Sense controllers
	// the Vibration page's Triggers test: the step last pressed (1-11, 0 before the first) and when its hold ends
	struct TriggerTest
	{
		int step = 0;
		uint64_t until = 0; // 0 when no step is held
	};
	TriggerTest s_triggerTest;
	constexpr uint64_t kTriggerIntervalUs = 50000; // firmware effect updates are slow: at most 20 a second
	constexpr uint64_t kTriggerRetryUs = 5000000;  // a refused effect is tried again 5 s later (AstroBotVR2's rule)
	constexpr uint64_t kTriggerTestHoldUs = 15000000; // a test step holds 15 s or until the next press

	void DescribeTrigger(const ps5trig::Command& command, char* out, size_t size)
	{
		switch (command.mode)
		{
		case ps5trig::kFeedback: std::snprintf(out, size, "feedback %u %u", (unsigned)command.data[0], (unsigned)command.data[1]); break;
		case ps5trig::kWeapon: std::snprintf(out, size, "weapon %u-%u %u", (unsigned)command.data[0], (unsigned)command.data[1], (unsigned)command.data[2]); break;
		case ps5trig::kSlope: std::snprintf(out, size, "slope %u-%u %u-%u", (unsigned)command.data[0], (unsigned)command.data[1], (unsigned)command.data[2], (unsigned)command.data[3]); break;
		case ps5trig::kVibration: std::snprintf(out, size, "vibration %u %u %u", (unsigned)command.data[0], (unsigned)command.data[1], (unsigned)command.data[2]); break;
		case ps5trig::kMultiFeedback: std::snprintf(out, size, "multi feedback"); break;
		case ps5trig::kMultiVibration: std::snprintf(out, size, "multi vibration"); break;
		default: std::snprintf(out, size, "off"); break;
		}
	}

	bool TriggerDue(const TriggerSent& sent, const ps5trig::Param& param, uint64_t now)
	{
		if (now < sent.retryAt && !(ps5trig::IsOff(param) && sent.accepted))
			return false; // libScePad refused the last one: wait before the next attempt, but an off still clears an effect
		if (sent.valid && ps5trig::Same(sent.param, param))
			return false;
		if (ps5trig::IsOff(param) && !sent.valid)
			return true; // the first call clears anything a game that crashed left on the trigger
		// an off pair always goes out at once; an effect at most 20 times a second, the newest value next frame
		return ps5trig::IsOff(param) || now - sent.at >= kTriggerIntervalUs;
	}

	void SendDualSenseTriggers(const ps5trig::Param& param, uint64_t now)
	{
		TriggerSent& sent = s_triggerSent[0];
		if (!TriggerDue(sent, param, now))
			return;
		const int handle = PS5Cemu_PadHandle(0);
		if (handle < 0)
			return;
		const int result = hmd2::pad_set_trigger_effect(handle, &param);
		char l2[48], r2[48];
		DescribeTrigger(param.l2, l2, sizeof l2);
		DescribeTrigger(param.r2, r2, sizeof r2);
		Log("[trig] DualSense (handle %d) L2 %s, R2 %s -> 0x%08x", handle, l2, r2, (unsigned)result);
		if (result != 0)
		{
			// not sent: the same effect must go out again, and a refusal is not retried at 20 Hz
			sent.valid = false;
			if (ps5trig::IsOff(param))
				sent.accepted = false; // an off was attempted; an effect is cleared on the way out regardless
			sent.retryAt = now + kTriggerRetryUs;
			return;
		}
		sent.param = param;
		sent.valid = true;
		sent.accepted = !ps5trig::IsOff(param);
		sent.at = now;
		sent.retryAt = 0;
	}

	void SendSenseTriggers(const ps5trig::Param& param, uint64_t now)
	{
		if (!s.senseTriggers || s.hands[0].handle < 0 || s.hands[1].handle < 0)
			return;
		TriggerSent& sent = s_triggerSent[1];
		if (!TriggerDue(sent, param, now))
			return;
		const int result = hmd2::sense_set_trigger_effects(s.hands[0].handle, s.hands[1].handle, &param);
		char l2[48], r2[48];
		DescribeTrigger(param.l2, l2, sizeof l2);
		DescribeTrigger(param.r2, r2, sizeof r2);
		Log("[trig] Sense both hands (0x78) L2 %s, R2 %s -> 0x%08x", l2, r2, (unsigned)result);
		if (result != 0)
		{
			// not sent: the same effect goes out again 5 s later. The refusal is about this payload, not about the
			// path: the next effect and every later call is still made.
			sent.valid = false;
			if (ps5trig::IsOff(param))
				sent.accepted = false; // an off was attempted; an effect is cleared on the way out regardless
			sent.retryAt = now + kTriggerRetryUs;
			return;
		}
		sent.param = param;
		sent.valid = true;
		sent.accepted = !ps5trig::IsOff(param);
		sent.at = now;
		sent.retryAt = 0;
	}

	// The Vibration page's Triggers test: one fixed payload a press, sent to the DualSense and to the Sense
	// controllers whatever the Sense Triggers row says, with one [trig test] line a device. A refused step keeps
	// the other device and every later step going.
	void LogTestTrigger(int step, const ps5trig::TestPayload& payload, const char* device, int result)
	{
		Log("[trig test] step %d/11 %s rc=0x%08x (%s)", step, payload.name, (unsigned)result, device);
	}

	void SendTestTriggerLocked(int step, const ps5trig::TestPayload& payload)
	{
		const ps5trig::Param param = ps5trig::TestParam(payload);
		const int dualSense = PS5Cemu_PadHandle(0);
		const int padResult = dualSense >= 0 ? hmd2::pad_set_trigger_effect(dualSense, &param) : hmd2::kPadTriggerUnavailable;
		LogTestTrigger(step, payload, "DualSense", padResult);
		if (padResult == 0)
			s_triggerSent[0].accepted = payload.mode != ps5trig::kOff;
		const int left = s.hands[0].handle, right = s.hands[1].handle;
		const int senseResult = left >= 0 && right >= 0 ? hmd2::sense_set_trigger_effects(left, right, &param) : hmd2::kPadTriggerUnavailable;
		LogTestTrigger(step, payload, "Sense both hands", senseResult);
		if (senseResult == 0)
			s_triggerSent[1].accepted = payload.mode != ps5trig::kOff;
	}

	// Both devices off and nothing held: the game's own effect goes out again on the next call. The step stays,
	// so the button's next press continues the sequence (step 11 ends it, and the press after starts over).
	void EndTriggerTestLocked(int step)
	{
		SendTestTriggerLocked(step, ps5trig::kTestPayloads[ps5trig::kTestPayloadCount - 1]);
		s_triggerSent[0] = {};
		s_triggerSent[1] = {};
		s_triggerTest.step = step;
		s_triggerTest.until = 0;
	}

	// Always send off: the effect lives in the controller's firmware and outlives the game. The DualSense is cleared
	// whatever it last took; the Sense controllers when an effect of theirs may still be on (the game's or the
	// Vibration page's test). s.lock is held.
	void ResetTriggersLocked(const char* why)
	{
		const ps5trig::Param off = ps5trig::Both(ps5trig::Off(), ps5trig::Off());
		const int dualSense = PS5Cemu_PadHandle(0);
		if (dualSense >= 0)
		{
			const int result = hmd2::pad_set_trigger_effect(dualSense, &off);
			Log("[trig] DualSense off (%s) -> 0x%08x", why, (unsigned)result);
		}
		if (s_triggerSent[1].accepted && s.hands[0].handle >= 0 && s.hands[1].handle >= 0)
		{
			const int result = hmd2::sense_set_trigger_effects(s.hands[0].handle, s.hands[1].handle, &off);
			Log("[trig] Sense off (%s), both hands -> 0x%08x", why, (unsigned)result);
		}
		s_triggerSent[0] = {};
		s_triggerSent[1] = {};
		s_triggerTest = {};
	}

	// the head in tracker space at a time, with hmd2's prediction; the last one when it has none
	Pose TrackerHeadAt(XrTime time)
	{
		if (s.session == XR_NULL_HANDLE || !s.session->hmdUp)
			return s.lastHead;
		const hmd2::Pose head = hmd2::predicted_pose((uint64_t)(time / 1000));
		if (head.tracked)
			s.lastHead = FromHmd(head);
		return s.lastHead;
	}

	// the head where the view shows it: the tracker's pose, seated by the last recenter
	Pose HeadAt(XrTime time)
	{
		return Compose(s.seat, TrackerHeadAt(time));
	}

	// a space's own pose at a time: the pose the app gave it, in the reference frame's own coordinates, or the
	// seated head for VIEW
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

	// xrEndFrame's compose: recorded and submitted here, waited for by the caller without s.lock (see xrEndFrame)
	template<typename Record>
	bool SubmitCompose(XrSession_T* session, Record&& record)
	{
		vk.vkResetCommandBuffer(session->composeCommands, 0);
		VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk.vkBeginCommandBuffer(session->composeCommands, &begin);
		record(session->composeCommands);
		vk.vkEndCommandBuffer(session->composeCommands);
		vk.vkResetFences(session->device, 1, &session->composeFence);
		VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &session->composeCommands;
		VkResult result;
		{
			std::lock_guard queue(ps5xr::QueueMutex());
			result = vk.vkQueueSubmit(session->queue, 1, &submit, session->composeFence);
		}
		if (result != VK_SUCCESS)
			return Fail("vkQueueSubmit (compose)", result);
		session->composePending = true;
		return true;
	}

	// The compose's fence is signalled, or has timed out again, or failed. Signalled: the stall (if there was one) is over.
	// Timed out: the frame stays open and xrEndFrame tries again on the next frames, until kGpuGiveUpUs after the first timeout.
	// Returns false when the run is given up (gpuFailed set; the caller leaves s.lock and takes the VR-failed path).
	bool ComposeFenceResult(XrSession_T* session, VkResult result)
	{
		if (result == VK_SUCCESS)
		{
			session->composePending = false;
			if (session->gpuStallSince)
			{
				session->gpuRecovered++;
				Log("the compositor's GPU work finished again after %llu ms (%llu timeouts so far)",
					(unsigned long long)((sceKernelGetProcessTime() - session->gpuStallSince) / 1000), (unsigned long long)session->gpuTimeouts);
				session->gpuStallSince = 0;
			}
			return true;
		}
		const uint64_t now = sceKernelGetProcessTime();
		if (result == VK_TIMEOUT)
		{
			if (!session->gpuStallSince)
				session->gpuStallSince = now;
			if (session->gpuTimeouts++ % 10 == 0)
				Log("the compositor's GPU work has not finished (vkWaitForFences, %llu timeouts so far, stalled %llu ms); the frame is retried",
					(unsigned long long)session->gpuTimeouts, (unsigned long long)((now - session->gpuStallSince) / 1000));
			if (now - session->gpuStallSince < kGpuGiveUpUs)
				return true;
			Log("the compositor's GPU work did not finish in %llu s: VR stops here", (unsigned long long)(kGpuGiveUpUs / 1000000));
		}
		else
			Log("vkWaitForFences (compose) -> %d: VR stops here", (int)result);
		session->gpuFailed = true;
		return false;
	}

	// One xrEndFrame's time, and the 5 s line the draw path's counters add up to: per-frame draws
	// through multiview and the right-eye replay, and the GPU thread's record, replay, compose wait
	// and xrEndFrame times. The counters are per window, not per run.
	void FrameEndedByDraws(XrSession_T* session, uint64_t started)
	{
		const uint64_t now = sceKernelGetProcessTime();
		session->drawLogEndUs += now - started;
		session->drawLogFrames++;
		if (!session->drawLogAt)
			session->drawLogAt = now;
		if (now - session->drawLogAt < 5'000'000 || session->drawLogFrames == 0)
			return;
#if defined(__PROSPERO__)
		const double frames = (double)session->drawLogFrames;
		const double recordMs = (double)g_ps5RecordNs.exchange(0, std::memory_order_relaxed) / 1e6 / frames;
		Log("[draws] per frame: multiview %.1f, replay %.1f, total %.1f | record %.2f ms | replay %.2f ms | compose %.2f ms | xr end %.2f ms (over %llu frames, %.1f s)"
			" | sequences %.1f, index lists %.1f (%.0f KB), sets made %.2f, pipelines made %.2f"
			" | kept index lists used again %.1f, changed %.1f (%.0f KB held)"
			" | texture units reused %.1f | render passes begun %.1f, submits %.1f",
			(double)g_ps5DrawsMultiview.exchange(0, std::memory_order_relaxed) / frames,
			(double)g_ps5DrawsReplay.exchange(0, std::memory_order_relaxed) / frames,
			(double)g_ps5DrawsTotal.exchange(0, std::memory_order_relaxed) / frames, recordMs,
			(double)g_ps5ReplayNs.exchange(0, std::memory_order_relaxed) / 1e6 / frames,
			(double)session->drawLogFenceUs / 1000.0 / frames, (double)session->drawLogEndUs / 1000.0 / frames,
			(unsigned long long)session->drawLogFrames, (double)(now - session->drawLogAt) / 1e6,
			(double)g_ps5DrawSequences.exchange(0, std::memory_order_relaxed) / frames,
			(double)g_ps5IndexDecodes.exchange(0, std::memory_order_relaxed) / frames,
			(double)g_ps5IndexBytes.exchange(0, std::memory_order_relaxed) / 1024.0 / frames,
			(double)g_ps5DescriptorSetsMade.exchange(0, std::memory_order_relaxed) / frames,
			(double)g_ps5PipelinesMade.exchange(0, std::memory_order_relaxed) / frames,
			(double)g_ps5IndexReused.exchange(0, std::memory_order_relaxed) / frames,
			(double)g_ps5IndexChanged.exchange(0, std::memory_order_relaxed) / frames,
			(double)g_ps5IndexKeptBytes.load(std::memory_order_relaxed) / 1024.0,
			(double)g_ps5TextureUnitsReused.exchange(0, std::memory_order_relaxed) / frames,
			(double)g_ps5PassesBegun.exchange(0, std::memory_order_relaxed) / frames,
			(double)g_ps5Submits.exchange(0, std::memory_order_relaxed) / frames);
		// bettervr.json's profile: where the record's time went. The rest of the record, between the draws, is the
		// command stream's own work: register writes, texture copies and clears, queries and the like.
		double phaseMs[6];
		for (int i = 0; i < 6; i++)
			phaseMs[i] = _rdtscFrequency ? (double)g_ps5DrawPhaseTicks[i].exchange(0, std::memory_order_relaxed) * 1000.0 / (double)_rdtscFrequency / frames : 0.0;
		if (s.profile)
			Log("[draws] record split per frame: sequence setup %.2f ms, draws %.2f ms (uniforms %.2f, indices %.2f, buffers %.2f, pipeline and sets %.2f, "
				"binds and the draw %.2f), between draws %.2f ms",
				phaseMs[0], phaseMs[1], phaseMs[2], phaseMs[3], phaseMs[4], phaseMs[5],
				std::max(0.0, phaseMs[1] - phaseMs[2] - phaseMs[3] - phaseMs[4] - phaseMs[5]),
				std::max(0.0, recordMs - phaseMs[0] - phaseMs[1]));
#endif
		session->drawLogAt = now;
		session->drawLogFrames = 0;
		session->drawLogEndUs = 0;
		session->drawLogFenceUs = 0;
	}

	void ImageBarrier(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
		VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
		uint32_t baseLevel = 0, uint32_t levels = 1, uint32_t layers = 1)
	{
		VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		barrier.srcAccessMask = srcAccess;
		barrier.dstAccessMask = dstAccess;
		barrier.oldLayout = from;
		barrier.newLayout = to;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = {aspect, baseLevel, levels, 0, layers};
		vk.vkCmdPipelineBarrier(commands, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	bool MakeImage(XrSession_T* session, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image,
		VkDeviceMemory& memory, VkImageView* view, uint32_t mipLevels = 1)
	{
		VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT; // an sRGB image sampled through a UNORM view
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = format;
		info.extent = {width, height, 1};
		info.mipLevels = mipLevels;
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
			viewInfo.subresourceRange = {(VkImageAspectFlags)(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, mipLevels, 0, 1};
			result = vk.vkCreateImageView(session->device, &viewInfo, nullptr, view);
			if (result != VK_SUCCESS)
				return Fail("vkCreateImageView", result);
		}
		return true;
	}

	void DestroyHudMips(XrSession_T* session)
	{
		if (session->hudMipView)
			vk.vkDestroyImageView(session->device, session->hudMipView, nullptr);
		if (session->hudMip)
			vk.vkDestroyImage(session->device, session->hudMip, nullptr);
		if (session->hudMipMemory)
			vk.vkFreeMemory(session->device, session->hudMipMemory, nullptr);
		session->hudMipView = VK_NULL_HANDLE;
		session->hudMip = VK_NULL_HANDLE;
		session->hudMipMemory = VK_NULL_HANDLE;
		session->hudMipWidth = session->hudMipHeight = session->hudMipLevels = 0;
		session->hudMipFormat = VK_FORMAT_UNDEFINED;
	}

	// The HUD is a 2000x2040 image on a quad of about 836x487 eye pixels: a 2.4x minification, which
	// one bilinear tap at level 0 turns into crawling edges. Two levels, the most the compose's LOD
	// clamp (1.0) reads, let textureGrad take the half-resolution level without averaging glyph detail
	// away; levels 2+ of a full chain were what made v0.6's first build blurrier than v0.5.0. Only the
	// levels used are built each frame (level 0's copy plus one blit).
	bool EnsureHudMips(XrSession_T* session, const XrSwapchain_T* hud)
	{
		if (session->hudMip && session->hudMipWidth == hud->width && session->hudMipHeight == hud->height && session->hudMipFormat == hud->format)
			return session->hudMipsSupported;
		DestroyHudMips(session);
		if (!vk.vkCmdCopyImage || !vk.vkCmdBlitImage || !vk.vkGetPhysicalDeviceFormatProperties)
			return false;
		VkFormatProperties properties{};
		vk.vkGetPhysicalDeviceFormatProperties(session->physical, hud->format, &properties);
		const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
		if ((properties.optimalTilingFeatures & needed) != needed)
		{
			static bool logged = false;
			if (!logged)
				Log("the HUD format %d has no linear blit: the HUD keeps its one bilinear tap (mip chain off)", (int)hud->format);
			logged = true;
			return false;
		}
		uint32_t levels = 1;
		while ((1u << levels) < std::max(hud->width, hud->height))
			levels++;
		levels = std::min(levels, 2u); // the compose clamps LOD to 1.0: levels 0 and 1 are all it reads
		if (!MakeImage(session, hud->width, hud->height, hud->format,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, session->hudMip, session->hudMipMemory,
				&session->hudMipView, levels))
		{
			DestroyHudMips(session);
			return false;
		}
		session->hudMipWidth = hud->width;
		session->hudMipHeight = hud->height;
		session->hudMipLevels = levels;
		session->hudMipFormat = hud->format;
		session->hudMipsSupported = true;
		Log("HUD mip chain: %ux%u, %u levels, format %d, made each frame for the compose", hud->width, hud->height, levels, (int)hud->format);
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
		result = vk.vkAllocateCommandBuffers(session->device, &allocate, &session->composeCommands);
		if (result != VK_SUCCESS)
			return Fail("vkAllocateCommandBuffers (compose)", result);
		VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		result = vk.vkCreateFence(session->device, &fenceInfo, nullptr, &session->fence);
		if (result != VK_SUCCESS)
			return Fail("vkCreateFence", result);
		result = vk.vkCreateFence(session->device, &fenceInfo, nullptr, &session->composeFence);
		if (result != VK_SUCCESS)
			return Fail("vkCreateFence (compose)", result);

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
		// linear between mip levels: the HUD is sampled with textureGrad, the eye images at LOD 0.
		// maxLod 1.0 caps the HUD at its half-resolution level, as v0.5.0's one tap kept level 0:
		// it keeps the edge-shimmer gain without reaching the levels that blurred menu text.
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxLod = 1.0f;
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
		const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, kComposeParamsBytes};
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
		config.mirror = s.tv; // Hmd2's social screen: the headset's view on the TV
		if (!hmd2::init(config))
		{
			const hmd2::Error error = hmd2::error();
			Log("hmd2 did not start, at %s (rc 0x%08x); the hmd2 lines above say more", error.step && error.step[0] ? error.step : "an unknown step",
				(unsigned)error.rc);
			return false;
		}
		session->hmdUp = true;
		s_live = true;
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
			s_live = false;
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
		DestroyHudMips(session);
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
		if (session->composeFence)
			vk.vkDestroyFence(device, session->composeFence, nullptr);
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

	float Dot(const Vec3& a, const Vec3& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	// The quad's bounding box as the compose shader's mapping (a box is a plane facing the eye).
	void QuadFromRect(const float rect[4], float out[9])
	{
		const float w = rect[2] - rect[0], h = rect[3] - rect[1];
		const float rows[9] = {1.0f / w, 0, -rect[0] / w, 0, 1.0f / h, -rect[1] / h, 0, 0, 1};
		std::copy(rows, rows + 9, out);
	}

	// The quad as the plane it is, seen from one eye: eye UV (u, v) to quad UV by a homography, so
	// a quad that is tilted, rolled or off-axis keeps its shape instead of the upright bounding
	// box of QuadInEye. A pixel's ray is d = (tl + (tr-tl)u, tu + (td-tu)v, -1) in the eye; it hits
	// the plane (normal n, through the centre C) at t = n.C / n.d, and the quad UV is
	// h = 0.5 + (t d - C) . axis. Multiplying by n.d makes both numerators and the divisor linear
	// in d, hence in (u, v, 1). False when the plane is edge-on or behind the eye.
	bool QuadPlaneInEye(const XrCompositionLayerQuad& quad, const Pose& quadPose, const Pose& eye, const XrFovf& fov, float out[9])
	{
		const Pose toEye = Inverse(eye);
		const Vec3 centre = Add(toEye.p, Rotate(toEye.q, quadPose.p));
		const Quat q = Mul(toEye.q, quadPose.q);
		Vec3 n = Rotate(q, Vec3{0, 0, 1});
		float nc = Dot(n, centre);
		if (nc < 0)
			n = {-n.x, -n.y, -n.z}, nc = -nc;
		if (!(nc > 1e-3f) || !(quad.size.width > 0) || !(quad.size.height > 0))
			return false;
		const Vec3 right = Rotate(q, Vec3{1.0f / quad.size.width, 0, 0}), down = Rotate(q, Vec3{0, -1.0f / quad.size.height, 0});
		const float tl = Tan(fov.angleLeft), tr = Tan(fov.angleRight), tu = Tan(fov.angleUp), td = Tan(fov.angleDown);
		const auto row = [&](const Vec3& v, float* r) {
			r[0] = v.x * (tr - tl), r[1] = v.y * (td - tu), r[2] = v.x * tl + v.y * tu - v.z;
		};
		const auto numerator = [&](const Vec3& axis, float* r) {
			const float ca = Dot(centre, axis);
			row({0.5f * n.x + nc * axis.x - ca * n.x, 0.5f * n.y + nc * axis.y - ca * n.y, 0.5f * n.z + nc * axis.z - ca * n.z}, r);
		};
		numerator(right, out);
		numerator(down, out + 3);
		row(n, out + 6);
		return true;
	}

	// The part of one eye a quad covers, in that eye's pixels: the eye's unit square clipped by the
	// quad's homography (its four edges, and the half-space in front of the eye), then widened to
	// whole pixels. False when the quad is nowhere in this eye. A pixel or two conservative, so the
	// compose's HUD-only dispatch can never miss one the shader would blend the HUD into.
	bool QuadBox(const float q[9], int32_t width, int32_t height, int32_t box[4])
	{
		struct Point
		{
			float u, v;
		};
		// the shader's inQuad as five half-planes a*u + b*v + c >= 0: nx >= 0, den - nx >= 0,
		// ny >= 0, den - ny >= 0, and den >= 0 (the quad in front of the eye)
		const float planes[5][3] = {{q[0], q[1], q[2]},
			{q[6] - q[0], q[7] - q[1], q[8] - q[2]},
			{q[3], q[4], q[5]},
			{q[6] - q[3], q[7] - q[4], q[8] - q[5]},
			{q[6], q[7], q[8]}};
		Point polygon[16] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
		int count = 4;
		for (const auto& plane : planes)
		{
			Point clipped[16];
			int kept = 0;
			for (int i = 0; i < count; i++)
			{
				const Point& a = polygon[i];
				const Point& b = polygon[(i + 1) % count];
				const float da = plane[0] * a.u + plane[1] * a.v + plane[2];
				const float db = plane[0] * b.u + plane[1] * b.v + plane[2];
				if (da >= 0.0f)
					clipped[kept++] = a;
				if ((da >= 0.0f) != (db >= 0.0f))
				{
					const float t = da / (da - db);
					clipped[kept++] = {a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t};
				}
			}
			std::copy(clipped, clipped + kept, polygon);
			count = kept;
			if (!count)
				return false;
		}
		float u0 = 1.0f, v0 = 1.0f, u1 = 0.0f, v1 = 0.0f;
		for (int i = 0; i < count; i++)
			u0 = std::min(u0, polygon[i].u), u1 = std::max(u1, polygon[i].u),
			v0 = std::min(v0, polygon[i].v), v1 = std::max(v1, polygon[i].v);
		box[0] = std::clamp<int32_t>((int32_t)std::floor(u0 * width), 0, width);
		box[1] = std::clamp<int32_t>((int32_t)std::floor(v0 * height), 0, height);
		box[2] = std::clamp<int32_t>((int32_t)std::ceil(u1 * width), 0, width);
		box[3] = std::clamp<int32_t>((int32_t)std::ceil(v1 * height), 0, height);
		return box[2] > box[0] && box[3] > box[1];
	}

	XrResult Lost()
	{
		return XR_ERROR_SESSION_LOST;
	}
}

// ==== the Vibration page's other devices ========================================================
// BetterVR's mixer (rendering/rumble.h) calls the two sinks below every 60 Hz tick with 0..1. The headset's level
// is not an amplitude: it is its motor's speed in Hz (10..25, 1..9 run at 10, 0 stops; PSVR2Toolkit's
// psvr2tk_capi.cpp says so of the same motor's USB command). An eccentric motor's force grows with speed squared,
// so every level goes through HeadsetHz() and 0..1 becomes a force. A level is sent when it changes, and a
// continuous hold is capped at 2 s like AstroBotVR2 so the forehead never buzzes for long. A Damage Taken hit
// (BetterVR_PS5_HeadsetPulse) is a 60 ms kick at 25 Hz, then the mapped level held for the hit. The Vibration
// page's test buttons override the level for 400 ms; the headset's stays full 25, the one motor speed the player
// has already felt. The pad's level is 0..255, sent to port/cemu/PS5PadController.cpp; BetterVR's page is its
// strength.
namespace
{
	constexpr int kHeadsetTestLevel = 25;
	constexpr int kPadTestLevel = 204;
	constexpr XrTime kHeadsetHoldCap = 2'000'000'000;
	constexpr XrTime kDeviceTestHold = 400'000'000;
	constexpr XrTime kHeadsetKick = 60'000'000;

	// The headset's one motor takes a speed, not an amplitude. Below 0.08 the motor would only tick, so it is off.
	int HeadsetHz(float amplitude)
	{
		const float a = std::clamp(amplitude, 0.0f, 1.0f);
		if (a < 0.08f)
			return 0;
		return std::clamp((int)std::lround(25.0f * std::sqrt(a)), 10, 25);
	}

	struct DeviceSinkState
	{
		int wanted = 0;      // the mixer's last level (headset 0..25 Hz, pad large 0..255)
		int wantedSmall = 0; // the pad's small motor (0..255); the headset has one
		int sent = -1;       // what the device last took; -1 none yet
		int sentSmall = -1;
		bool capped = false;
		int cappedLevel = -1; // the level the 2 s cap cut, so it stays off until the level changes
		XrTime sameAt = 0;
		XrTime testUntil = 0;
		// a Damage Taken hit: the kick at 25 Hz, then the mapped level held until pulseUntil
		int pulseHz = 0;
		XrTime pulseKickUntil = 0;
		XrTime pulseUntil = 0;
	};
	DeviceSinkState s_headset, s_pad;

	int SendHeadsetLevel(int level)
	{
		static int s_failures = 0;
		const int rc = hmd2::set_vibration(level);
		// hmd2 logs the bind and each failed call; the runtime names what it asked for the first few times
		if (level && rc != 0 && s_failures++ < 10)
			VibLog("headset level %d failed: 0x%08x", level, (unsigned)rc);
		return rc;
	}

	int ApplyHeadset()
	{
		const XrTime now = Now();
		const bool testing = now < s_headset.testUntil;
		int wanted = testing ? kHeadsetTestLevel : s_headset.wanted;
		if (!testing)
		{
			// the pulse outlasts the mixer's own envelope: the hit's mapped level is held, not its falling tail
			if (now < s_headset.pulseKickUntil)
				wanted = std::max(wanted, kHeadsetTestLevel);
			else if (now < s_headset.pulseUntil)
				wanted = std::max(wanted, s_headset.pulseHz);
		}
		if (!testing && s_headset.capped && wanted == s_headset.cappedLevel)
			return 0; // the hold was cut at 2 s: stay off until the level changes, not 2 s on and 16 ms off
		if (wanted == 0 && s_headset.sent <= 0)
		{
			// nothing to stop: VR start must not call the motor with zero; the first event or the page's test
			// button is its first call
			s_headset.sent = 0;
			s_headset.capped = false;
			s_headset.cappedLevel = -1;
			return 0;
		}
		if (wanted == s_headset.sent)
		{
			if (wanted == 0)
			{
				s_headset.capped = false;
				s_headset.cappedLevel = -1; // a new rise may buzz again
			}
			else if (!testing && !s_headset.capped && now - s_headset.sameAt >= kHeadsetHoldCap)
			{
				SendHeadsetLevel(0);
				s_headset.sent = 0;
				s_headset.capped = true;
				s_headset.cappedLevel = wanted;
				VibLog("headset held at %d for 2 s: off until the level changes", wanted);
			}
			return 0;
		}
		const int rc = SendHeadsetLevel(wanted);
		if (rc == 0)
		{
			s_headset.sent = wanted;
			s_headset.sameAt = now;
			s_headset.capped = false;
			s_headset.cappedLevel = -1;
		}
		else
			s_headset.sent = -1;
		return rc;
	}

	void ApplyPad()
	{
		const XrTime now = Now();
		const bool testing = now < s_pad.testUntil;
		const int wantedLarge = testing ? kPadTestLevel : s_pad.wanted;
		const int wantedSmall = testing ? kPadTestLevel : s_pad.wantedSmall;
		if (wantedLarge == s_pad.sent && wantedSmall == s_pad.sentSmall)
			return;
		PS5Pad_ExternalRumble(wantedLarge / 255.0f, wantedSmall / 255.0f);
		s_pad.sent = wantedLarge;
		s_pad.sentSmall = wantedSmall;
	}
}

extern "C" void BetterVR_PS5_HeadsetVibration(float amplitude)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return;
	s_headset.wanted = HeadsetHz(amplitude);
	ApplyHeadset();
}

// Damage Taken (BetterVR's player.cpp): one hit is a 60 ms kick at 25 Hz, then the level its strength maps to,
// held for ms. The mixer's own falling envelope keeps the hands; the headset would only get weaker with it.
extern "C" int BetterVR_PS5_HeadsetPulse(float strength, int ms)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return 0;
	const float a = std::clamp(strength, 0.0f, 1.0f);
	const int hz = HeadsetHz(a);
	const XrTime now = Now();
	if (hz == 0)
	{
		VibLog("headset pulse off (strength %.2f, below 0.08)", a);
		return 0;
	}
	s_headset.pulseHz = hz;
	s_headset.pulseKickUntil = now + kHeadsetKick;
	s_headset.pulseUntil = now + (XrTime)std::max(ms, 1) * 1'000'000;
	// a hit is a new event: the 2 s cap cut a continuous hold, not this pulse, and its hold starts again
	s_headset.sameAt = now;
	s_headset.capped = false;
	s_headset.cappedLevel = -1;
	VibLog("headset pulse %d Hz for %d ms (kick %d Hz for 60 ms, strength %.2f)", hz, ms, kHeadsetTestLevel, a);
	ApplyHeadset();
	return hz;
}

extern "C" void BetterVR_PS5_PadVibration(float large, float small)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return;
	s_pad.wanted = (int)std::lround(std::clamp(large, 0.0f, 1.0f) * 255.0f);
	s_pad.wantedSmall = (int)std::lround(std::clamp(small, 0.0f, 1.0f) * 255.0f);
	ApplyPad();
}

extern "C" void BetterVR_PS5_TestHeadset()
{
	std::lock_guard lock(s.lock);
	const XrTime now = Now();
	const int rc = hmd2::set_vibration(kHeadsetTestLevel);
	s_headset.sent = rc == 0 ? kHeadsetTestLevel : -1;
	s_headset.sameAt = now;
	s_headset.capped = false;
	s_headset.cappedLevel = -1;
	s_headset.testUntil = rc == 0 ? now + kDeviceTestHold : now;
	VibLog("test headset: level %d -> 0x%08x (%s)", kHeadsetTestLevel, (unsigned)rc,
		rc == 0 ? "took it, 400 ms" : rc == hmd2::kHmd2VibrationUnavailable ? "sceHmd2SetVibration is not bound on this firmware" : "refused");
}

extern "C" void BetterVR_PS5_TestPad()
{
	std::lock_guard lock(s.lock);
	if (!s.padTakenOver)
	{
		VibLog("test pad: no VR session is driving the DualSense");
		return;
	}
	PS5Pad_ExternalRumble(kPadTestLevel / 255.0f, kPadTestLevel / 255.0f);
	s_pad.sent = kPadTestLevel;
	s_pad.sentSmall = kPadTestLevel;
	s_pad.testUntil = Now() + kDeviceTestHold;
	VibLog("test pad: level %d for 400 ms on the DualSense (player 1)", kPadTestLevel);
}

// The Sense triggers' rows in BetterVR's menu (rendering/imgui_menus.cpp). The values stay in
// bettervr.json like the keys the track started with; a change is live and the writer keeps the
// file's other keys.
extern "C" int BetterVR_PS5_SenseTriggersEnabled()
{
	std::lock_guard lock(s.lock);
	return s.senseTriggers ? 1 : 0;
}

// The Vibration page's Triggers test (rendering/imgui_menus.cpp): advances one step and returns it for the
// button's label (0 when there is no VR session, 1 after 11). See the test state above.
extern "C" int BetterVR_PS5_TestTriggers()
{
	std::lock_guard lock(s.lock);
	if (s.stopped || s.session == XR_NULL_HANDLE)
		return 0;
	const int step = s_triggerTest.step >= (int)ps5trig::kTestPayloadCount ? 1 : s_triggerTest.step + 1;
	if (step == (int)ps5trig::kTestPayloadCount)
	{
		EndTriggerTestLocked(step); // the last step is all off and ends the test
		return step;
	}
	SendTestTriggerLocked(step, ps5trig::kTestPayloads[step - 1]);
	s_triggerTest.step = step;
	s_triggerTest.until = sceKernelGetProcessTime() + kTriggerTestHoldUs;
	return step;
}

// BetterVR's menu draws its eye rows (Eye Aim Assist, Eye Magnesis, Eye Rune Pick) only while the runtime can
// offer eye tracking. gaze_available() is true once the gaze table bound and its stream started, and false again
// when the stream did not take, so a firmware without the table draws none of them.
extern "C" int BetterVR_PS5_EyeGazeAvailable()
{
	return hmd2::gaze_available() ? 1 : 0;
}

// The triggers page's DualSense row: available with the link-time import of scePadSetTriggerEffect
// (third_party/hmd2/hmd2.cpp), while the Sense rows stay separate.
extern "C" int BetterVR_PS5_PadTriggersAvailable()
{
	return hmd2::pad_triggers_available() ? 1 : 0;
}

extern "C" void BetterVR_PS5_SetSenseTriggers(int on)
{
	std::lock_guard lock(s.lock);
	const bool enabled = on != 0;
	if (s.senseTriggers == enabled)
		return;
	if (!enabled && s_triggerSent[1].accepted && s.hands[0].handle >= 0 && s.hands[1].handle >= 0)
	{
		// the effect lives in the controller's firmware: clear the one it took before the row goes off
		const ps5trig::Param off = ps5trig::Both(ps5trig::Off(), ps5trig::Off());
		const int result = hmd2::sense_set_trigger_effects(s.hands[0].handle, s.hands[1].handle, &off);
		Log("[trig] Sense off (the menu's Sense Triggers row), both hands -> 0x%08x", (unsigned)result);
	}
	s.senseTriggers = enabled;
	s_triggerSent[1] = {};
	if (!enabled)
		s_triggerTest.until = 0; // the row going off also ends a Triggers-test step that was holding
	BetterVR_PS5_WriteSetting("senseTriggers", enabled ? 1.0f : 0.0f);
	Log("[trig] Sense trigger effects %s (the menu's Sense Triggers row, saved to bettervr.json)", enabled ? "on" : "off");
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
	// XR_KHR_composition_layer_depth is not offered, as 7fbceeb allows: BetterVR then keeps its depth in a local attachment, and no
	// depth swapchain (three 16 MB images for each eye) is made for a compositor that ignores depth. xrCreateInstance still
	// accepts the name, for an application that asks without having looked.
	// Eye gaze is offered while the gaze table has bound (third_party/hmd2/hmd2.cpp): the runtime builds the
	// eye-gaze profile and space, and BetterVR's Controls page draws its eye rows. A firmware
	// without the table, or a stream that did not start, offers none of it.
	const bool gaze = hmd2::gaze_available();
	*count = gaze ? 2u : 1u;
	if (capacity < *count)
		return capacity == 0 ? XR_SUCCESS : XR_ERROR_SIZE_INSUFFICIENT;
	std::snprintf(props[0].extensionName, XR_MAX_EXTENSION_NAME_SIZE, "%s", XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
	props[0].extensionVersion = XR_KHR_vulkan_enable_SPEC_VERSION;
	if (gaze)
	{
		std::snprintf(props[1].extensionName, XR_MAX_EXTENSION_NAME_SIZE, "%s", XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
		props[1].extensionVersion = XR_EXT_eye_gaze_interaction_SPEC_VERSION;
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
		const bool gaze = std::strcmp(name, XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME) == 0 && hmd2::gaze_available();
		if (std::strcmp(name, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME) != 0 && std::strcmp(name, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME) != 0 && !gaze)
		{
			Log("xrCreateInstance: extension %s is not offered", name);
			return XR_ERROR_EXTENSION_NOT_PRESENT;
		}
	}
	s.instance = new XrInstance_T{};
	for (uint32_t i = 0; i < info->enabledExtensionCount; i++)
	{
		if (std::strcmp(info->enabledExtensionNames[i], XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME) == 0)
			s.instance->depth = true;
		if (std::strcmp(info->enabledExtensionNames[i], XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME) == 0)
			s.instance->gaze = true;
	}
	*instance = s.instance;
	Log("VR start 1/5: OpenXR instance for %s (%s), runtime \"PlayStation VR2\" over hmd2, extensions %s%s%s", info->applicationInfo.applicationName,
		info->applicationInfo.engineName, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME, s.instance->depth ? ", composition layer depth" : "",
		s.instance->gaze ? ", eye gaze interaction" : "");
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
	// The eye-gaze profile is offered with the extension, while hmd2's gaze table is bound. When the
	// stream did not start the property is false and BetterVR logs Supports Eye Tracking: No.
	const bool gaze = hmd2::gaze_available();
	bool chained = false;
	for (auto* next = static_cast<XrBaseOutStructure*>(props->next); next; next = next->next)
		if (next->type == XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT)
		{
			reinterpret_cast<XrSystemEyeGazeInteractionPropertiesEXT*>(next)->supportsEyeGazeInteraction = gaze ? XR_TRUE : XR_FALSE;
			chained = true;
		}
	static bool logged = false;
	if (!std::exchange(logged, true))
		Log(gaze ? "gaze: XR_EXT_eye_gaze_interaction offered%s; a pose is reported only while the headset's calibrated eye tracking is on"
				 : "gaze: XR_EXT_eye_gaze_interaction not offered: the gaze table did not bind%s",
			chained ? "" : " (the application did not chain the system property)");
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
	static bool logged = false; // the Graphics pack's presets (BetterVR's host module) are filled from this size
	if (!std::exchange(logged, true))
		Log("view configuration: two views of %ux%u, one sample (the headset's own eye size, which BetterVR's Graphics pack presets scale)", kEyeWidth, kEyeHeight);
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
	// a new session starts unseated: only Options moves the view's seat
	s.seat = {};
	s.senseOptions = {};
	s.padOptions = {};
	s.senseOptionsRecentered.store(false, std::memory_order_release);
	s.padOptionsRecentered.store(false, std::memory_order_release);
	if (!vk.Load(binding->instance, binding->device))
	{
		Log("the driver lacks a function the compositor needs");
		s.session = XR_NULL_HANDLE;
		delete session;
		return XR_ERROR_RUNTIME_FAILURE;
	}
	vk.vkGetDeviceQueue(session->device, binding->queueFamilyIndex, binding->queueIndex, &session->queue);
	Log("VR start 2/5: session on Cemu's device %p, queue family %u index %u", (void*)binding->device, binding->queueFamilyIndex, binding->queueIndex);
	const uint64_t sessionBeganAt = sceKernelGetProcessTime();
	// read before hmd2::init, which asks Hmd2 for the mirror or not
	s.tv = BetterVR_PS5_Setting("tv", 1.0f) != 0.0f;
	if (!StartSession(session))
	{
		EndSessionObjects(session);
		s.session = XR_NULL_HANDLE;
		delete session;
		return XR_ERROR_RUNTIME_FAILURE;
	}
	s.sessionStartedAt = sceKernelGetProcessTime();
	Log("VR start 3/5: the headset is up and the compositor is ready (%llu ms after the session)", (unsigned long long)((sceKernelGetProcessTime() - sessionBeganAt) / 1000));
	if (s.instance->gaze && !hmd2::gaze_available())
		Log("gaze: eye tracking is not available; the gaze space stays untracked");
	s.gripPitch = BetterVR_PS5_Setting("gripPitch", 32.0f);
	s.aimPitch = BetterVR_PS5_Setting("aimPitch", 0.0f);
	s.gripYaw = BetterVR_PS5_Setting("gripYaw", 17.0f);
	s.gripRoll = BetterVR_PS5_Setting("gripRoll", 30.0f);
	s.reproject = BetterVR_PS5_Setting("reproject", 1.0f) != 0.0f;
	s.asyncCompose = BetterVR_PS5_Setting("asyncCompose", 1.0f) != 0.0f;
	Log("compose wait: %s (bettervr.json's asyncCompose)",
		s.asyncCompose ? "on the frame pump's xrWaitFrame, which submits the frame to Hmd2; Cemu's GPU thread goes on to the next frame"
					   : "in xrEndFrame on Cemu's GPU thread, which submits the frame to Hmd2 (\"asyncCompose\": 0)");
	s.profile = BetterVR_PS5_Profile();
	if (s.profile)
	{
		s.profileAt = sceKernelGetProcessTime();
#if defined(__PROSPERO__)
		s.profileGpuAt = g_ps5GpuBusyNs.load(std::memory_order_relaxed);
#endif
	}
	// the Sense controllers' trigger effects (bettervr.json's "senseTriggers"). The DualSense's triggers need no
	// switch: its effects are the setting in BetterVR's own menu (patches/bettervr 0007).
	s.senseTriggers = BetterVR_PS5_Setting("senseTriggers", 0.0f) != 0.0f;
	s_triggerSent[0] = {};
	s_triggerSent[1] = {};
	if (s.senseTriggers)
		Log("Sense trigger effects on (bettervr.json's senseTriggers), libScePad's both-hands 0x78 call");
	else
		Log("Sense trigger effects off (bettervr.json's senseTriggers); the DualSense's triggers are unaffected");
	Log("reprojection of the game's frames: %s (bettervr.json's reproject); HUD quad drawn as its plane, a mono picture across each eye, "
		"a re-shown pair keeps its render pose", s.reproject ? "on" : "off");
	Log("frame profile: %s (bettervr.json's profile), %s", s.profile ? "one [frame] line every 10 s" : "off",
		s.profile ? "GPU busy, swap, VR end, compose fence and hmd2 submit" : "nothing extra is timed");
	if (!s.tv)
		Log("TV mirror off (bettervr.json's tv): the TV stays dark while VR runs");
	else if (hmd2::mirroring())
		Log("TV mirror on: Hmd2's social screen shows the headset's view on the TV (bettervr.json's tv), no present of ours");
	else
		Log("TV mirror asked for but Hmd2 did not take it (bettervr.json's tv); the hmd2 lines say why, VR continues");
	Log("controllers: grip pitch %.1f deg, aim pitch %.1f deg, grip yaw %.1f roll %.1f deg, yaw and roll left +, right - (bettervr.json's gripPitch, aimPitch, gripYaw, gripRoll)", s.gripPitch, s.aimPitch, s.gripYaw, s.gripRoll);
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
	ResetTriggersLocked("the session ended");
	CloseHands();
	s.attached = false;
	if (s.padTakenOver)
	{
		s.padTakenOver = false;
		PS5Pad_ExternalRumble(0.0f, 0.0f);
		s_pad.sent = 0;
		s_pad.sentSmall = 0;
		PS5Pad_SetExternalRumble(false);
	}
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
	// the Vibration page's DualSense sink is the pad's only rumble from here (Cemu's own pattern output is
	// held back in port/cemu/PS5PadController.cpp), so the game's burst and BetterVR's cannot fire twice
	if (!s.padTakenOver)
	{
		s.padTakenOver = true;
		PS5Pad_SetExternalRumble(true);
		s_pad.sent = -1; // the first mixer level of this session is sent, even if the last session ended on it
		s_pad.sentSmall = -1;
	}
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
	*space = new XrSpace_T{info->referenceSpaceType, false, false, FromXr(info->poseInReferenceSpace)};
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
	auto* created = new XrSpace_T{XR_REFERENCE_SPACE_TYPE_STAGE, true, false, FromXr(info->poseInActionSpace)};
	created->poseAction = info->action;
	created->hand = HandOf(info->subactionPath);
	created->gaze = info->action != nullptr && IsGazeAction(info->action);
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
	if (space->gaze)
	{
		const hmd2::Gaze sample = hmd2::gaze();
		if (!sample.valid)
		{
			location->locationFlags = 0;
			location->pose = {{0, 0, 0, 1}, {0, 0, 0}};
			return XR_SUCCESS;
		}
		// hmd2 hands the firmware's headset-frame direction (-z forward, as PS5VR reads the same result);
		// OpenXR's eyes look down -z, so it is used as it comes, and the pose is in stage space.
		const Vec3 raw{sample.direction[0], sample.direction[1], sample.direction[2]};
		const Vec3 direction = Rotate(HeadAt(time).q, raw);
		const Quat orientation = Mul(Conj(SpacePose(base, time).q), FromTo({0, 0, -1}, direction));
		location->pose = ToXr(Pose{orientation, {0, 0, 0}});
		location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
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
		// the Sense's raw pose has Link's hands turned inward about the vertical axis, mirrored for the two hands
		const int side = int(hand - s.hands);
		const float mirror = side == 0 ? 1.0f : -1.0f;
		const Pose turn{AxisAngle(0, 1, 0, mirror * s.gripYaw), {}};
		const Pose tilt{AxisAngle(1, 0, 0, s.gripPitch + (aim ? s.aimPitch : 0.0f)), {}};
		const Pose roll{AxisAngle(0, 0, 1, mirror * s.gripRoll), {}};
		spacePose = Compose(Compose(Compose(Compose(Compose(s.seat, hand->pose), turn), tilt), roll), space->offset);
		// for the first 20 s, every 2 s: the grip's rotation against the head, which reads about 0/0/0 when the
		// controller points straight ahead
		static XrTime handsStart = 0, handsLogged[2] = {0, 0};
		const XrTime nowTime = Now();
		if (!aim && side >= 0 && side < 2 && (handsStart == 0 || nowTime - handsStart < 20'000'000'000) && nowTime - handsLogged[side] >= 2'000'000'000)
		{
			if (handsStart == 0)
				handsStart = nowTime;
			handsLogged[side] = nowTime;
			const Quat rel = Mul(Conj(HeadAt(time).q), spacePose.q);
			const Vec3 fwd = Rotate(rel, {0, 0, -1}), right = Rotate(rel, {1, 0, 0}), up = Rotate(rel, {0, 1, 0});
			const float deg = 180.0f / 3.14159265f;
			Log("hands: %c yaw/pitch/roll %.0f/%.0f/%.0f (head-relative, degrees)", side == 0 ? 'L' : 'R', std::atan2(-fwd.x, -fwd.z) * deg,
				std::asin(std::max(-1.0f, std::min(1.0f, fwd.y))) * deg, std::atan2(right.y, up.y) * deg);
		}
		const Pose toBase = Inverse(SpacePose(base, time));
		// the velocity is the hand's in the tracker's frame: into the base's, and through the seat, as its pose is
		const Quat toBaseSeated = Mul(toBase.q, s.seat.q);
		for (auto* next = static_cast<XrBaseOutStructure*>(location->next); next; next = next->next)
			if (next->type == XR_TYPE_SPACE_VELOCITY)
			{
				// the hand was found above, so its pose is valid: publish both parts even on the first
				// sample (zero) or after a hitch, so a zero velocity never means "the flags were left off"
				auto* velocity = reinterpret_cast<XrSpaceVelocity*>(next);
				const Vec3 linear = Rotate(toBaseSeated, hand->linear), angular = Rotate(toBaseSeated, hand->angular);
				velocity->linearVelocity = {linear.x, linear.y, linear.z};
				velocity->angularVelocity = {angular.x, angular.y, angular.z};
				velocity->velocityFlags = XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
			}
	}
	else
		spacePose = SpacePose(space, time);
	const Pose pose = Compose(Inverse(SpacePose(base, time)), spacePose);
	if (space->action && space->hand >= 0)
	{
		// the hand's located pose, for the frame log: with a wrong hand both spaces show one pose
		s.handLocated[space->hand] = pose;
	}
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
	const Pose trackerHead = TrackerHeadAt(info->displayTime);
	const Pose head = Compose(s.seat, trackerHead);
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
			s.located[s.nextLocated] = {eyePose.p, trackerHead};
			s.nextLocated = (s.nextLocated + 1) % (int)(sizeof(s.located) / sizeof(s.located[0]));
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
	// one 2D image per swapchain image: BetterVR asks for exactly that (its stereo is two swapchains, one a
	// view), and the multiview rendering of the Cemu fork happens before the pictures reach the runtime
	if (info->arraySize != 1 || info->faceCount != 1 || info->sampleCount != 1)
	{
		Log("xrCreateSwapchain: %u array layers, %u faces, %u samples asked, one of each is offered", info->arraySize, info->faceCount, info->sampleCount);
		return XR_ERROR_FEATURE_UNSUPPORTED;
	}
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
	VkFormat sampleFormat = VK_FORMAT_UNDEFINED;
	if (color)
	{
		if (format == VK_FORMAT_R8G8B8A8_SRGB)
			sampleFormat = VK_FORMAT_R8G8B8A8_UNORM;
		else if (format == VK_FORMAT_B8G8R8A8_SRGB)
			sampleFormat = VK_FORMAT_B8G8R8A8_UNORM;
	}
	for (int i = 0; i < kSwapchainImages; i++)
	{
		if (!MakeImage(session, info->width, info->height, format, usage, swapchain->images[i], swapchain->memory[i], &swapchain->views[i]))
			return XR_ERROR_RUNTIME_FAILURE;
		if (sampleFormat != VK_FORMAT_UNDEFINED)
		{
			// the same image as UNORM: the compose reads the bytes as written, without the sRGB
			// decode that would need the pow chain to encode them again
			VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			viewInfo.image = swapchain->images[i];
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = sampleFormat;
			viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			const VkResult created = vk.vkCreateImageView(session->device, &viewInfo, nullptr, &swapchain->sampleViews[i]);
			if (created != VK_SUCCESS)
			{
				Fail("vkCreateImageView (UNORM sample)", created);
				return XR_ERROR_RUNTIME_FAILURE;
			}
		}
	}
	// the layout the application finds them in, and leaves them in at release
	const bool ok = RunOnce(session, [&](VkCommandBuffer commands) {
		for (int i = 0; i < kSwapchainImages; i++)
			ImageBarrier(commands, swapchain->images[i], color ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
				color ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, 0,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	});
	if (!ok)
		return XR_ERROR_RUNTIME_FAILURE;
	Log("VR start 4/5: swapchain %d, %ux%u format %d (%s), %d images, %u array layer(s)", ++s.swapchainsMade, info->width, info->height, (int)format,
		color ? "colour" : "depth, not shown", kSwapchainImages, info->arraySize);
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
			if (swapchain->sampleViews[i])
				vk.vkDestroyImageView(s.session->device, swapchain->sampleViews[i], nullptr);
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
	// an acquired image is free: a compose's reads end before the next frame's compose is recorded (xrEndFrame waits
	// for them, or with asyncCompose finishes the last compose first), and an image comes round again three acquires on
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*)
{
	std::lock_guard lock(s.lock);
	swapchain->released = (int)((swapchain->next + kSwapchainImages - 1) % kSwapchainImages);
	return XR_SUCCESS;
}

// -- frames ----------------------------------------------------------------------------------------
namespace
{
	// How FinishCompose left the frame
	enum class ComposeFinish
	{
		Submitted, // the compose finished and the frame went to Hmd2
		Open,      // not submitted: the frame stays open and the next xrEndFrame looks at the fence again
		Failed,    // the run is given up (gpuFailed): the caller leaves s.lock and takes the VR-failed path
		Lost,      // the session ended or the runtime stopped during the wait
	};

	// The fence of the compose xrEndFrame submitted, then the Hmd2 submit of the frame it composed. xrEndFrame calls it
	// at once, unless bettervr.json's asyncCompose is on: then the frame pump's next xrWaitFrame calls it (`pump`), before
	// it begins the next Hmd2 frame, and Cemu's GPU thread goes on to the next game frame while the GPU composes;
	// xrEndFrame calls it only for a compose the pump has not taken. The pump takes only a compose that finished in
	// time; a timeout is left to the GPU thread's next xrEndFrame, which handles it as the synchronous path does.
	// Called with s.lock held through `lock`; the fence wait leaves it.
	ComposeFinish FinishCompose(std::unique_lock<std::mutex>& lock, XrSession_T* session, bool pump)
	{
		// The wait runs without s.lock: the compose sits on the shared queue behind the module's submit, which can wait on a
		// timeline value Cemu's GPU thread signals in its next submit, and that thread takes s.lock in xrAcquireSwapchainImage
		// and the like. Holding the lock here would make the wait cause its own timeout.
		session->composeBusy = true;
		const uint64_t fenceStarted = sceKernelGetProcessTime();
		lock.unlock();
		const VkResult waited = vk.vkWaitForFences(session->device, 1, &session->composeFence, VK_TRUE, kGpuTimeoutNs);
		const uint64_t fenceEnded = sceKernelGetProcessTime();
		lock.lock();
		session->composeBusy = false;
		s.composeIdle.notify_all();
		session->drawLogFenceUs += fenceEnded - fenceStarted;
		if (s.profile)
			s.profileFenceNs += (fenceEnded - fenceStarted) * 1000;
		if (s.stopped || s.session != session)
			return ComposeFinish::Lost;
		if (pump && waited != VK_SUCCESS)
			return ComposeFinish::Open; // still due: the GPU thread's next xrEndFrame waits again and counts the timeout
		session->composeFinishDue = false;
		if (!ComposeFenceResult(session, waited) || session->composePending)
			return session->gpuFailed ? ComposeFinish::Failed : ComposeFinish::Open;
		session->frameOpen = false;
		const Pose renderHead = session->composeHead;
		const uint32_t flags = session->composeFlags;
		const uint64_t submitStarted = s.profile ? sceKernelGetProcessTime() : 0;
		const bool submitted = hmd2::submit(ToHmd(renderHead));
		if (s.profile)
		{
			s.profileSubmitNs += (sceKernelGetProcessTime() - submitStarted) * 1000;
			s.profileFrames++;
		}
		// Hmd2's compositor is what puts this frame on the TV: no present of ours and no queue lock
		if (s.tv && submitted)
			(hmd2::mirroring() ? session->tvShown : session->tvSkipped)++;
		if (!s.firstFrameLogged && submitted)
		{
			s.firstFrameLogged = true;
			session->lastFrameLogAt = sceKernelGetProcessTime();
			Log("VR start 5/5: the first frame is submitted to the headset (%llu ms after the session, %s)",
				(unsigned long long)((sceKernelGetProcessTime() - s.sessionStartedAt) / 1000),
				(flags & 3u) ? "with a stereo picture" : "without one yet, the HUD alone or black");
		}
		if (!s.firstStereoLogged && submitted && (flags & 3u))
		{
			s.firstStereoLogged = true;
			Log("the first frame with a stereo picture is submitted (frame %llu, eye pictures %s %s, HUD quad %s)", (unsigned long long)session->frames + 1,
				(flags & 1u) ? "left" : "-", (flags & 2u) ? "right" : "-", (flags & 4u) ? "yes" : "no");
		}
		if (++session->frames % 600 == 0)
		{
			const hmd2::PaceStats pace = hmd2::take_pace();
			const hmd2::FlipInfo flip = hmd2::flip_status();
			// the headset's own frame rate over these 600 frames (the game's is the [perf] line's)
			const uint64_t now = sceKernelGetProcessTime();
			const double seconds = session->lastFrameLogAt ? (double)(now - session->lastFrameLogAt) / 1e6 : 0.0;
			session->lastFrameLogAt = now;
			Log("%llu frames, %.1f fps submitted over the last 600: nap %.2f ms, BeginFrame %.2f ms, ahead %.2f ms, step %.2f ms; flip shown %llu pending %d; "
				"repeated %d, queue full %d, missed %d, errors %d, without layers %llu, skipped %llu, waits one frame ahead %llu, ends without a frame %llu, late begins %llu, worn %d; "
				"GPU timeouts %llu, stuck skips %llu, recovered %llu; "
				"render pose found %llu, not found %llu, kept %llu; "
				"head %.2f %.2f %.2f, hand spaces left %.3f %.3f %.3f right %.3f %.3f %.3f, hands posed %d %d; "
				"plane quads %llu; TV mirror %s, shown %llu skipped %llu",
				(unsigned long long)session->frames, seconds > 0.0 ? 600.0 / seconds : 0.0, pace.nap_ms, pace.call_ms, pace.ahead_ms, pace.step_ms,
				(unsigned long long)flip.shown, flip.pending, hmd2::repeated_frames(), hmd2::queue_full_waits(), hmd2::missed_frames(), hmd2::frame_errors(),
				(unsigned long long)session->emptyFrames, (unsigned long long)session->skipped, (unsigned long long)session->waitsAhead,
				(unsigned long long)session->endsWithoutFrame, (unsigned long long)session->lateBegins, (unsigned long long)session->gpuTimeouts,
				(unsigned long long)session->gpuStuckSkips, (unsigned long long)session->gpuRecovered, hmd2::worn() ? 1 : 0, (unsigned long long)session->poseFound,
				(unsigned long long)session->poseLost, (unsigned long long)session->poseKept, renderHead.p.x, renderHead.p.y, renderHead.p.z,
				s.handLocated[0].p.x, s.handLocated[0].p.y, s.handLocated[0].p.z, s.handLocated[1].p.x, s.handLocated[1].p.y, s.handLocated[1].p.z,
				s.hands[0].poseValid ? 1 : 0, s.hands[1].poseValid ? 1 : 0, (unsigned long long)session->planeQuads, s.tv ? "on" : "off",
				(unsigned long long)session->tvShown, (unsigned long long)session->tvSkipped);
		}
		if (s.profile)
		{
			const uint64_t now = sceKernelGetProcessTime();
			if (s.profileAt && now - s.profileAt >= 10'000'000)
			{
				const double seconds = (double)(now - s.profileAt) / 1e6;
#if defined(__PROSPERO__)
				const uint64_t swapNs = g_ps5FrameSwapNs.exchange(0, std::memory_order_relaxed);
				const uint64_t vrEndNs = g_ps5FrameVrEndNs.exchange(0, std::memory_order_relaxed);
				const uint32_t samples = g_ps5FrameProfileSamples.exchange(0, std::memory_order_relaxed);
				const uint64_t gpuNs = g_ps5GpuBusyNs.load(std::memory_order_relaxed);
				const double gpuBusy = now > s.profileAt ? (double)(gpuNs - s.profileGpuAt) / 1000.0 * 100.0 / (double)(now - s.profileAt) : 0.0;
				s.profileGpuAt = gpuNs;
				const double notifyFrames = samples ? (double)samples : (s.profileFrames ? (double)s.profileFrames : 1.0);
				const double runtimeFrames = s.profileFrames ? (double)s.profileFrames : notifyFrames;
				Log("[frame] swap %.2f ms | VR end %.2f ms | compose fence %.2f ms | hmd2 submit %.2f ms | %.1f fps | GPU busy %.0f%% (over %.1f s, %u frames)",
					(double)swapNs / 1e6 / notifyFrames, (double)vrEndNs / 1e6 / notifyFrames, (double)s.profileFenceNs / 1e6 / runtimeFrames,
					(double)s.profileSubmitNs / 1e6 / runtimeFrames, notifyFrames / seconds, gpuBusy, seconds, samples);
#endif
				s.profileAt = now;
				s.profileFenceNs = s.profileSubmitNs = 0;
				s.profileFrames = 0;
			}
		}
		return ComposeFinish::Submitted;
	}

	// xrEndFrame's FinishCompose: true when the frame went to Hmd2 and the caller goes on; otherwise `result` is what
	// xrEndFrame returns, after the VR-failed path for a given-up run
	bool FinishComposeHere(std::unique_lock<std::mutex>& lock, XrSession_T* session, XrResult& result)
	{
		switch (FinishCompose(lock, session, false))
		{
		case ComposeFinish::Submitted:
			return true;
		case ComposeFinish::Open:
			result = XR_SUCCESS; // the frame stays open: the next xrEndFrame looks at the fence again
			return false;
		case ComposeFinish::Failed:
			lock.unlock();
			BetterVR_PS5_VrFailed("the compositor's GPU work did not finish");
			result = XR_SUCCESS;
			return false;
		case ComposeFinish::Lost:
			break;
		}
		result = Lost();
		return false;
	}
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(XrSession session, const XrFrameWaitInfo*, XrFrameState* state)
{
	// hmd2's pacing naps here: not under the lock, so poses can be located meanwhile
	if (s.stopped)
		return Lost();
	// bettervr.json's asyncCompose: the last frame waits for its compose here and goes to Hmd2, before the next Hmd2
	// frame is begun, at the moment xrEndFrame would have submitted it
	if (s.asyncCompose)
	{
		std::unique_lock lock(s.lock);
		s.composeIdle.wait(lock, [session] { return !session->composeBusy; });
		if (!s.stopped && s.session == session && session->composeFinishDue)
			FinishCompose(lock, session, true);
	}
	hmd2::wait_slot();
	s_waitFrames++;
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return Lost();
	// BetterVR's frame pump waits for the next frame while the game is still drawing this one: the Hmd2 frame
	// is then still open (one at a time), and the next one is begun by the wait that follows xrEndFrame
	const bool openAlready = session->frameOpen;
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
	// the open frame's display time is the one frame ahead of it for the frame that follows
	state->predictedDisplayTime = (XrTime)hmd2::display_time_us() * 1000 + (openAlready ? kFramePeriod : 0);
	state->shouldRender = XR_TRUE;
	if (openAlready)
		session->waitsAhead++;
	// a hold that reached its second seats the view for this frame, before the app locates the eyes it draws from
	ApplyRecenter(state->predictedDisplayTime);
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
	s_endFrames++;
	std::unique_lock lock(s.lock);
	if (s.stopped)
		return Lost();
	// bettervr.json's asyncCompose: a compose the frame pump has not taken yet is finished first, since this frame's Hmd2
	// frame, compose commands, descriptor set and fence are free only once it is done
	if (session->composeFinishDue)
	{
		s.composeIdle.wait(lock, [session] { return !session->composeBusy; });
		if (s.stopped || s.session != session)
			return Lost();
		XrResult result = XR_SUCCESS;
		if (session->composeFinishDue && !FinishComposeHere(lock, session, result))
			return result;
	}
	// A back-to-back xrEndFrame (a repeated frame renders nothing) comes before the pump's next xrWaitFrame has begun
	// the Hmd2 frame: begin it here and submit late, instead of dropping the picture (BeginFrame does not nap)
	if (!session->frameOpen && !session->gpuFailed && session->hmdUp)
	{
		session->frameOpen = hmd2::begin_frame();
		if (session->frameOpen && session->lateBegins++ == 0)
			Log("xrEndFrame found no open Hmd2 frame and began one (counted in the frame log)");
	}
	if (!session->frameOpen || session->gpuFailed)
	{
		if (!session->frameOpen && session->endsWithoutFrame++ == 0)
			Log("xrEndFrame found no open Hmd2 frame and BeginFrame gave none: the frame's picture is dropped (counted in the frame log)");
		return XR_SUCCESS;
	}
	// every return from here adds this xrEndFrame to the 5 s [draws] window
	struct EndFrameSample
	{
		XrSession_T* session;
		uint64_t started;
		~EndFrameSample() { FrameEndedByDraws(session, started); }
	} endFrameSample{session, sceKernelGetProcessTime()};
	// The last compose is still in flight (its wait timed out): look again, and leave the frame open for the next try
	if (session->composePending)
	{
		if (session->composeBusy)
			return XR_SUCCESS;
		session->gpuStuckSkips++;
		if (!ComposeFenceResult(session, vk.vkWaitForFences(session->device, 1, &session->composeFence, VK_TRUE, 0)) || session->composePending)
		{
			if (session->gpuFailed)
			{
				lock.unlock();
				BetterVR_PS5_VrFailed("the compositor's GPU work did not finish");
			}
			return XR_SUCCESS;
		}
	}

	const XrCompositionLayerProjection* projection = nullptr;
	const XrCompositionLayerQuad* quad = nullptr;
	for (uint32_t i = 0; i < info->layerCount; i++)
	{
		if (info->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && !projection)
			projection = reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[i]);
		else if (info->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_QUAD && !quad)
			quad = reinterpret_cast<const XrCompositionLayerQuad*>(info->layers[i]);
	}

	// the pose to submit: the tracker's head pose the submitted left view was located from, unseated, as
	// Hmd2 compares it with the headset's own pose for reprojection. The seat is the game's frame only.
	Pose renderHead = TrackerHeadAt(info->displayTime);
	struct
	{
		int32_t eyeSize[2];
		uint32_t flags;
		uint32_t pad;
		float source[2][4];
		float quad[2][9]; // per eye, eye UV (u, v, 1) to quad UV: the numerators of x and y, then the divisor
		int32_t origin[2]; // the output pixel the dispatch's first invocation sits at (a HUD-only dispatch)
	} params{{(int32_t)kEyeWidth, (int32_t)kEyeHeight}, 0, 0, {}, {}, {}};
	static_assert(sizeof(params) == kComposeParamsBytes, "the compose shader's push constants changed size");
	VkImageView eyeViews[2] = {session->dummyView, session->dummyView};
	VkImage eyeImages[2] = {};
	// the swapchains the eye images came from, for the 1x copy path's size, format and rect
	const XrSwapchain_T* eyeSwapchains[2] = {};
	Pose eyePoses[2];
	XrFovf eyeFov[2] = {};
	bool monoViews = false;
	// whether the submitted views' render pose is known: a mono picture is head-locked, a stereo
	// pair is found in the located ring or kept from when it was published
	bool found = true;
	// a re-shown pair the ring no longer holds: the picture is stale, not a render of this frame
	bool kept = false;
	if (projection && projection->viewCount >= 2)
	{
		const Pose base = SpacePose(projection->space, info->displayTime);
		// A mono projection (the menu/flat path) submits one eye's pose and FOV in both views so
		// both eyes see one picture. Detect it so the HUD quad is placed with the real eyes below.
		monoViews = std::memcmp(&projection->views[0].pose, &projection->views[1].pose, sizeof(XrPosef)) == 0
			&& std::memcmp(&projection->views[0].fov, &projection->views[1].fov, sizeof(XrFovf)) == 0;
		for (int eye = 0; eye < 2; eye++)
		{
			const XrCompositionLayerProjectionView& view = projection->views[eye];
			const XrSwapchain_T* swapchain = view.subImage.swapchain;
			eyePoses[eye] = Compose(base, FromXr(view.pose));
			eyeFov[eye] = view.fov;
			if (!swapchain || swapchain->released < 0 || !swapchain->color)
				continue;
			// the UNORM view reads the bytes as they were written; the sRGB view is only for rendering
			eyeViews[eye] = swapchain->sampleViews[swapchain->released] ? swapchain->sampleViews[swapchain->released] : swapchain->views[swapchain->released];
			eyeImages[eye] = swapchain->images[swapchain->released];
			eyeSwapchains[eye] = swapchain;
			const XrRect2Di& r = view.subImage.imageRect;
			params.source[eye][0] = (float)r.offset.x / swapchain->width;
			params.source[eye][1] = (float)r.offset.y / swapchain->height;
			params.source[eye][2] = (float)(r.offset.x + r.extent.width) / swapchain->width;
			params.source[eye][3] = (float)(r.offset.y + r.extent.height) / swapchain->height;
			params.flags |= 1u << eye;
		}
		// x and z only: BetterVR lifts the views it renders from to a seated eye height (y) before it
		// submits them, so a match on y never held and the frame went to Hmd2 as rendered from the
		// pose of its display time: no reprojection of an image that is 50 ms old or more.
		// A mono picture is one head-locked image, not a stereo render: its views carry this frame's
		// live pose, so the display head is the pose to present it from. The search ignores y for a
		// raised eye height and scans the ring from the oldest entry, so it can match an older head
		// at another height or roll and timewarp the menu against the head (the "screen moved"
		// report).
		found = monoViews;
		if (!monoViews)
		{
			// the same eye, to a hundredth of a millimetre, is the same render: the ring search and
			// the kept-pose fallback both ask this, x and z only for the reason above
			const auto sameEye = [](const Vec3& shown, const Vec3& eye) {
				return std::fabs(shown.x - eye.x) < 1e-5f && std::fabs(shown.z - eye.z) < 1e-5f;
			};
			// the ring first: a fresh frame's pair is in it, and so is a pair the submit thread shows
			// again until the pose thread's 250 Hz writes push it out (256 entries, about a second)
			for (const auto& located : s.located)
				if (s.reproject && sameEye(located.leftEye, eyePoses[0].p))
				{
					renderHead = located.trackerHead;
					found = true;
					s.shownEye = eyePoses[0].p;
					s.shownHead = located.trackerHead;
					s.shownKnown = true;
					s.shownAtFrame = session->frames;
					s.poseKeepLogged = false;
					break;
				}
			// poseKeep: only when the ring cannot answer, a pair the submit thread shows again is a
			// hold that outlives the ring (the rune window's case, a cutscene's frozen frame). The
			// compose below leaves such a pair's eye pictures out and draws the frame's 2D quad
			// instead, black around it; with no quad the kept render pose still reprojects the pair
			// from where it was drawn instead of from the live head. Ordinary play finds its pair in
			// the ring, so it neither counts nor logs a kept frame.
			if (!found && s.reproject && s.poseKeep && s.shownKnown && sameEye(s.shownEye, eyePoses[0].p))
			{
				renderHead = s.shownHead;
				found = true;
				kept = true;
				if (!s.poseKeepLogged)
				{
					s.poseKeepLogged = true;
					Log("render pose kept for a re-shown pair (shown %llu frames)",
						(unsigned long long)(session->frames - s.shownAtFrame));
				}
				++session->poseKept;
			}
		}
		(found ? session->poseFound : session->poseLost)++;
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
	// A re-shown pair the located ring no longer holds is the last picture the submit thread shows
	// again, not a render of this frame. When the frame also carries a 2D quad, the quad is the
	// frame's picture: the eye pictures are left out below, and the quad is placed with the live
	// eyes and Hmd2 given the live head, so the picture is in front of the player and stable there.
	const bool quadUsable = quad && quad->subImage.swapchain && quad->subImage.swapchain->released >= 0 && quad->subImage.swapchain->color;
	const bool stalePair = projection && kept && quadUsable;
	if (stalePair)
		renderHead = TrackerHeadAt(info->displayTime);
	// The HUD quad is placed with the real eyes, not with the projection's shared view: for a mono
	// picture both views carry the left eye's pose/FOV, and using that for QuadInEye put the right
	// eye's HUD at the left eye's position (the scope/menu HUD offset). A stereo pair whose render
	// pose is unknown is placed the same way: Hmd2 is given the live head for it, so the quad must
	// not be drawn in a different frame from the one it reprojects.
	Pose hudPoses[2] = {eyePoses[0], eyePoses[1]};
	XrFovf hudFov[2] = {eyeFov[0], eyeFov[1]};
	if (monoViews || stalePair || (s.poseKeep && !found))
	{
		const Pose head = HeadAt(info->displayTime);
		const float out = std::atan(s.fov.out), in = std::atan(s.fov.in), up = std::atan(s.fov.top), down = std::atan(s.fov.bottom);
		for (int eye = 0; eye < 2; eye++)
		{
			hudPoses[eye] = {head.q, Add(head.p, Rotate(head.q, s.eyeOffset[eye]))};
			hudFov[eye] = eye == 0 ? XrFovf{-out, in, up, -down} : XrFovf{-in, out, up, -down};
		}
	}
	VkImageView hudView = session->dummyView;
	VkImage hudImage = VK_NULL_HANDLE;
	const XrSwapchain_T* hudSwapchain = nullptr;
	bool panelPlaced = false;
	Vec3 panelCentre{};
	if (quad && quad->subImage.swapchain && quad->subImage.swapchain->released >= 0 && quad->subImage.swapchain->color)
	{
		const XrSwapchain_T* swapchain = quad->subImage.swapchain;
		const Pose quadPose = Compose(SpacePose(quad->space, info->displayTime), FromXr(quad->pose));
		panelCentre = quadPose.p;
		bool any = false;
		for (int eye = 0; eye < 2; eye++)
		{
			float rect[4];
			const bool placed = s.quadPlane ? QuadPlaneInEye(*quad, quadPose, hudPoses[eye], hudFov[eye], params.quad[eye])
											: QuadInEye(*quad, quadPose, hudPoses[eye], hudFov[eye], rect);
			if (placed)
			{
				if (!s.quadPlane)
					QuadFromRect(rect, params.quad[eye]);
				any = true;
			}
			else
				std::fill(params.quad[eye], params.quad[eye] + 9, 0.0f), params.quad[eye][8] = -1.0f; // a divisor below 0: nowhere
		}
		if (any)
		{
			hudSwapchain = swapchain;
			hudView = swapchain->views[swapchain->released];
			hudImage = swapchain->images[swapchain->released];
			panelPlaced = true;
			params.flags |= 4u;
			if (quad->layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT)
				params.flags |= 8u;
			if (s.quadPlane)
				session->planeQuads++;
			// A mono picture rides the HUD quad, as one screen in both eyes, not across each eye: the
			// compositor shows a view's pixels 1:1 whatever the view's FOV, and the two eyes' FOVs are
			// mirrored, so across the eye the one picture fell ~1/3 of the eye apart (the doubled
			// Link). Outside the quad is black.
			if (monoViews && s.monoQuad && (params.flags & 1u))
			{
				params.flags |= 16u;
				session->monoOnQuad++;
			}
		}
	}
	// The recenter's one line: the head's yaw before the seat, the yaw it is at now (the seat's forward is -z,
	// so the frame's own head reads 0), and where the 2D panel ended up from that head. The line waits for a
	// panel when there is one, for at most a second, so a recenter with no quad still says where the head is.
	if (s.seatReport.pending)
	{
		const float deg = 180.0f / 3.14159265f;
		const Pose head = HeadAt(info->displayTime);
		const Vec3 forward = Rotate(head.q, {0, 0, -1});
		const float after = std::atan2(forward.x, -forward.z) * deg;
		if (panelPlaced || ++s.seatReport.waited >= 60)
		{
			if (panelPlaced)
			{
				const Vec3 headMid{(hudPoses[0].p.x + hudPoses[1].p.x) * 0.5f, (hudPoses[0].p.y + hudPoses[1].p.y) * 0.5f,
					(hudPoses[0].p.z + hudPoses[1].p.z) * 0.5f};
				const Vec3 toPanel = Sub(panelCentre, headMid);
				const float panel = std::atan2(toPanel.x, -toPanel.z) * deg;
				RecenterLog("%s: head yaw %.1f -> %.1f deg, the 2D panel is %.1f deg right of it; the seat moved %.2f %.2f m (the head was at %.2f %.2f %.2f)",
					s.seatReport.source, s.seatReport.before, after, panel, s.seatReport.offset.x, s.seatReport.offset.z, s.seatReport.head.x,
					s.seatReport.head.y, s.seatReport.head.z);
			}
			else
				RecenterLog("%s: head yaw %.1f -> %.1f deg, no 2D panel this frame; the seat moved %.2f %.2f m (the head was at %.2f %.2f %.2f)",
					s.seatReport.source, s.seatReport.before, after, s.seatReport.offset.x, s.seatReport.offset.z, s.seatReport.head.x,
					s.seatReport.head.y, s.seatReport.head.z);
			s.seatReport.pending = false;
		}
	}
	if ((params.flags & 7u) == 0)
		session->emptyFrames++;

	// A mono/cutscene frame is the 2D picture in front of the player, black around it, never an eye
	// picture stretched across both eyes: the mono picture rides the HUD quad (compose.comp's flag
	// 16), a stale re-shown pair's eye pictures are left out, and a frame with no projection keeps
	// its quad alone.
	const bool monoOnQuad = (params.flags & 16u) != 0u;
	if (stalePair)
		params.flags &= ~3u;
	const ComposeMode compose = (params.flags & 3u) && !monoOnQuad ? ComposeMode::Stereo : ComposeMode::MonoQuad;
	if (compose != s.composeMode)
	{
		s.composeMode = compose;
		if (compose == ComposeMode::MonoQuad)
		{
			const char* why = "no projection layer or eye picture this frame";
			if (monoOnQuad)
				why = "one mono picture in both views, on the HUD quad";
			else if (stalePair)
				why = "a re-shown pair the located ring no longer holds";
			Log("compose mode mono (frame %llu): %s; the 2D picture alone in front of the player, black outside it",
				(unsigned long long)session->frames + 1, why);
		}
		else
			Log("compose mode stereo (frame %llu): both eyes' pictures with the HUD quad over them",
				(unsigned long long)session->frames + 1);
	}

	// 1x: each eye's image is the eye's own pixels and rect, in the byte order Hmd2's buffer takes, so
	// the copy engine can fill the two halves and the shader only has to blend the HUD's box over
	// them. A higher VR Resolution keeps the shader's scaled sample, and the mono picture keeps the
	// black outside its quad. These bytes are the same ones the shader wrote before, alpha included:
	// the 3D present writes the eye pictures opaque (patch bettervr 0023).
	bool eyeCopy = !(params.flags & 16u) && (params.flags & 3u) == 3u && eyeImages[0] && eyeImages[0] != eyeImages[1]
		&& (!hudImage || (hudImage != eyeImages[0] && hudImage != eyeImages[1]));
	for (int eye = 0; eye < 2 && eyeCopy; eye++)
	{
		const XrSwapchain_T* swapchain = eyeSwapchains[eye];
		const float* source = params.source[eye];
		eyeCopy = swapchain && swapchain->width == kEyeWidth && swapchain->height == kEyeHeight
			&& (swapchain->format == VK_FORMAT_R8G8B8A8_SRGB || swapchain->format == VK_FORMAT_R8G8B8A8_UNORM)
			&& source[0] == 0.0f && source[1] == 0.0f && source[2] == 1.0f && source[3] == 1.0f;
	}
	// the HUD's own pixels, the only ones a dispatch has to touch over a copied frame
	int32_t hudBox[2][4] = {};
	int32_t hudPixels[2] = {};
	if (eyeCopy && (params.flags & 4u))
		for (int eye = 0; eye < 2; eye++)
			if (QuadBox(params.quad[eye], (int32_t)kEyeWidth, (int32_t)kEyeHeight, hudBox[eye]))
				hudPixels[eye] = (hudBox[eye][2] - hudBox[eye][0]) * (hudBox[eye][3] - hudBox[eye][1]);
	const bool hudDispatch = hudPixels[0] > 0 || hudPixels[1] > 0;
	if (eyeCopy && !session->eyeCopyLogged)
	{
		session->eyeCopyLogged = true;
		Log("the 1x compose copies each eye into hmd2's buffer and dispatches only the HUD's box (%d and %d pixels this frame)",
			hudPixels[0], hudPixels[1]);
	}

	// a mip chain of the HUD image, so the 2.4x minification through the quad picks a level
	bool hudMips = false;
	if (hudImage && hudSwapchain && hudImage != eyeImages[0] && hudImage != eyeImages[1] && EnsureHudMips(session, hudSwapchain))
	{
		hudMips = true;
		hudView = session->hudMipView;
		params.flags |= 32u;
	}
	// the one colour format the runtime offers that is not sRGB: its bytes are already encoded
	if (hudSwapchain && hudSwapchain->format == VK_FORMAT_R8G8B8A8_UNORM)
		params.flags |= 64u;

	const hmd2::EyeBuffer eye = hmd2::acquire();
	const int index = eye.index >= 0 && eye.index < kEyeBuffers ? eye.index : 0;
	VkDescriptorImageInfo images[3] = {{session->sampler, eyeViews[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
		{session->sampler, eyeViews[1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
		{session->sampler, hudView, hudMips ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
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
	VkImage sampledEyes[2] = {eyeImages[0], eyeImages[1]};
	if (sampledEyes[1] == sampledEyes[0])
		sampledEyes[1] = VK_NULL_HANDLE;
	// where the eye images are taken before the shader reads them and where they are when it is done:
	// the copy path takes them through TRANSFER_SRC, the copy engine's own layout
	const VkImageLayout eyeStart = eyeCopy ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	const VkImageLayout eyeEnd = eyeCopy && !hudDispatch ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	const bool ok = SubmitCompose(session, [&](VkCommandBuffer commands) {
		for (VkImage image : sampledEyes)
			if (image)
				ImageBarrier(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, eyeStart,
					VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
					eyeStart == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
					eyeStart == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		if (eyeCopy)
		{
			// the buffer's rows are the side-by-side image's own; each eye's copy fills half of every row
			for (int eye = 0; eye < 2; eye++)
			{
				VkBufferImageCopy copy{};
				copy.bufferOffset = (VkDeviceSize)(eye * kEyeWidth) * 4;
				copy.bufferRowLength = 2 * kEyeWidth;
				copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
				copy.imageExtent = {kEyeWidth, kEyeHeight, 1};
				vk.vkCmdCopyImageToBuffer(commands, eyeImages[eye], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, session->eyeBuffers[index], 1, &copy);
			}
			if (hudDispatch)
			{
				// the copies' writes land before the shader writes the HUD's box over them, and the
				// box itself still samples the eye image under it
				VkBufferMemoryBarrier copied{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
				copied.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				copied.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				copied.srcQueueFamilyIndex = copied.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				copied.buffer = session->eyeBuffers[index];
				copied.size = VK_WHOLE_SIZE;
				vk.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &copied,
					0, nullptr);
				for (VkImage image : sampledEyes)
					if (image)
						ImageBarrier(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
							VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
			}
		}
		if (hudImage && hudMips)
		{
			// the HUD as the game wrote it, into level 0, then each level down from the one above; the
			// levels stay in GENERAL so a level is read and written without a layout transition
			ImageBarrier(commands, hudImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			ImageBarrier(commands, session->hudMip, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, session->hudMipLevels);
			VkImageCopy copy{};
			copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			copy.extent = {hudSwapchain->width, hudSwapchain->height, 1};
			vk.vkCmdCopyImage(commands, hudImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, session->hudMip, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
			for (uint32_t level = 1; level < session->hudMipLevels; level++)
			{
				ImageBarrier(commands, session->hudMip, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
					VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					level - 1, 1);
				VkImageBlit blit{};
				blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
				blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
				blit.srcOffsets[1] = {(int32_t)std::max(1u, hudSwapchain->width >> (level - 1)), (int32_t)std::max(1u, hudSwapchain->height >> (level - 1)), 1};
				blit.dstOffsets[1] = {(int32_t)std::max(1u, hudSwapchain->width >> level), (int32_t)std::max(1u, hudSwapchain->height >> level), 1};
				vk.vkCmdBlitImage(commands, session->hudMip, VK_IMAGE_LAYOUT_GENERAL, session->hudMip, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_LINEAR);
			}
			ImageBarrier(commands, session->hudMip, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
				VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, session->hudMipLevels);
			ImageBarrier(commands, hudImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_ACCESS_TRANSFER_READ_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
		}
		else if (hudImage)
			ImageBarrier(commands, hudImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		vk.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, session->pipeline);
		vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, session->pipelineLayout, 0, 1, &session->set, 0, nullptr);
		if (!eyeCopy)
		{
			vk.vkCmdPushConstants(commands, session->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
			vk.vkCmdDispatch(commands, (2 * kEyeWidth + 7) / 8, (kEyeHeight + 7) / 8, 1);
		}
		else if (hudDispatch)
		{
			// one dispatch an eye, each over the pixels its HUD quad touches; the copies wrote the rest
			for (int eye = 0; eye < 2; eye++)
			{
				if (!hudPixels[eye])
					continue;
				params.origin[0] = hudBox[eye][0] + eye * (int32_t)kEyeWidth;
				params.origin[1] = hudBox[eye][1];
				vk.vkCmdPushConstants(commands, session->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
				vk.vkCmdDispatch(commands, (hudBox[eye][2] - hudBox[eye][0] + 7) / 8, (hudBox[eye][3] - hudBox[eye][1] + 7) / 8, 1);
			}
		}
		for (VkImage image : sampledEyes)
			if (image)
				ImageBarrier(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, eyeEnd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					eyeEnd == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT, 0,
					eyeEnd == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
		if (hudImage && !hudMips)
			ImageBarrier(commands, hudImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_ACCESS_SHADER_READ_BIT, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
		// the GPU wrote the buffer: visible to Hmd2 before the flip (HLA-PSVR2's barrier, less its host
		// half: Hmd2 samples the buffer on the GPU, nothing on the host reads it, and the host-read
		// access only asks the barrier for a write-back to a reader that is not there)
		VkBufferMemoryBarrier toHmd2{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
		toHmd2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | (eyeCopy ? VK_ACCESS_TRANSFER_WRITE_BIT : 0);
		toHmd2.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		toHmd2.srcQueueFamilyIndex = toHmd2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toHmd2.buffer = session->eyeBuffers[index];
		toHmd2.size = VK_WHOLE_SIZE;
		vk.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | (eyeCopy ? VK_PIPELINE_STAGE_TRANSFER_BIT : 0),
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &toHmd2, 0, nullptr);
	});
	if (!ok)
	{
		Log("the compositor's GPU work failed; the headset stops here and nothing is freed");
		return XR_ERROR_RUNTIME_FAILURE;
	}
	session->composeHead = renderHead;
	session->composeFlags = params.flags;
	session->composeFinishDue = true;
	// bettervr.json's asyncCompose: the frame pump's next xrWaitFrame waits for this compose and submits the frame, and
	// Cemu's GPU thread goes on to the next game frame while the GPU composes
	if (s.asyncCompose)
		return XR_SUCCESS;
	XrResult result = XR_SUCCESS;
	FinishComposeHere(lock, session, result);
	return result;
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

// The Oculus Touch profile's bindings are kept (the Sense controllers stand in for it) and the eye-gaze
// profile's one pose is the gaze space; every other profile's bindings are accepted and not used
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance, const XrInteractionProfileSuggestedBinding* suggested)
{
	std::lock_guard lock(s.lock);
	if (suggested->interactionProfile == XR_NULL_PATH || suggested->interactionProfile > s.paths.size())
		return XR_ERROR_PATH_INVALID;
	const std::string& profile = s.paths[suggested->interactionProfile - 1];
	if (profile == "/interaction_profiles/ext/eye_gaze_interaction")
	{
		s.gazeProfile = suggested->interactionProfile;
		s.gazeSuggested = true;
		int kept = 0, unknown = 0;
		for (uint32_t i = 0; i < suggested->countSuggestedBindings; i++)
		{
			const XrActionSuggestedBinding& suggestion = suggested->suggestedBindings[i];
			Binding binding{};
			if (suggestion.binding == XR_NULL_PATH || suggestion.binding > s.paths.size() ||
				s.paths[suggestion.binding - 1] != "/user/eyes_ext/input/gaze_ext/pose")
			{
				unknown++;
				continue;
			}
			binding.hand = -1; // the eyes, not a hand
			binding.component = Component::GazePose;
			suggestion.action->bindings.push_back(binding);
			kept++;
		}
		Log("Eye gaze profile: %d bindings onto the gaze space, %d not mapped", kept, unknown);
		return XR_SUCCESS;
	}
	if (profile != "/interaction_profiles/oculus/touch_controller")
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
	state->interactionProfile = XR_NULL_PATH;
	if (topLevel != XR_NULL_PATH && topLevel <= s.paths.size() && s.paths[topLevel - 1] == "/user/eyes_ext")
	{
		if (s.gazeSuggested)
			state->interactionProfile = s.gazeProfile;
		return XR_SUCCESS;
	}
	const int hand = HandOf(topLevel);
	if (hand >= 0 && s.touchSuggested && s.hands[hand].handle >= 0)
		state->interactionProfile = s.touchProfile;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession, const XrActionsSyncInfo* info)
{
	std::lock_guard lock(s.lock);
	if (s.stopped)
		return Lost();
	for (XrAction_T* action : s.actions)
		if (action->set)
			action->set->active = false;
	for (uint32_t i = 0; i < info->countActiveActionSets; i++)
		info->activeActionSets[i].actionSet->active = true;
	ReadHands();
	s.syncTime = Now();
	PollRecenter();
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
	{
		if (binding.component == Component::GazePose)
		{
			if (hmd2::gaze().valid)
				state->isActive = XR_TRUE;
			continue;
		}
		if (binding.hand >= 0 && IsPose(binding.component) && (which < 0 || binding.hand == which) && s.hands[binding.hand].connected &&
			s.hands[binding.hand].registered)
			state->isActive = XR_TRUE;
	}
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
	if (s.stopped)
		return XR_SUCCESS;
	if (!header || header->type != XR_TYPE_HAPTIC_VIBRATION)
		return XR_SUCCESS;
	const auto* vibration = reinterpret_cast<const XrHapticVibration*>(header);
	const int which = HandOf(info->subactionPath);
	// the first calls BetterVR's rumble makes, to show whether a hit reaches the controller
	static int s_haptics = 0;
	if (s_haptics < 6)
	{
		s_haptics++;
		Log("haptic call %d: hand %d, amplitude %.2f, duration %lld ns, frequency %.0f", s_haptics, which, vibration->amplitude,
			(long long)vibration->duration, vibration->frequency);
	}
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

// port/bettervr/VrPS5.cpp, at each TV frame Cemu presents (SwapBuffer, patches/cemu 0048)
void BetterVR_PS5_TvPresent()
{
	s_tvPresents++;
}

// port/app/ingame.cpp, on the render thread: the console's own keyboard is up (app/ime_dialog.cpp), so the
// pad is the system dialog's and the game is left an idle controller (controls.cpp, patches/bettervr 0048).
// While it is set the app's keyboard answers the game; nothing else reads it.
static std::atomic<bool> s_gameInputBlocked{false};

void BetterVR_PS5_SetGameInputBlocked(bool blocked)
{
	s_gameInputBlocked = blocked;
}

bool BetterVR_PS5_GameInputBlocked()
{
	return s_gameInputBlocked;
}

// app/ingame.cpp, on the render thread: the in-game menu opens (1), closes (0), and each frame it is up (2).
// Log only: whether the headset's frame loop keeps turning under the menu (xrWaitFrame, xrEndFrame, hmd2
// submits) and whether Cemu keeps presenting the TV. A line when it opens and closes, and one a second between.
void BetterVR_PS5_MenuProbe(int what)
{
	if (!s_live)
		return; // no headset: flat play, VR not started, or already off
	const uint64_t now = sceKernelGetProcessTime();
	const uint64_t counts[4] = {s_waitFrames, s_endFrames, (uint64_t)hmd2::frames_submitted(), s_tvPresents};
	if (what == 1)
	{
		for (int i = 0; i < 4; i++)
			s_menuAtOpen[i] = counts[i];
		s_menuOpenedAt = now;
		s_menuLoggedAt = now;
		Log("in-game menu open (xrWaitFrame %llu, xrEndFrame %llu, hmd2 submits %llu, Cemu TV presents %llu so far)",
			(unsigned long long)counts[0], (unsigned long long)counts[1], (unsigned long long)counts[2], (unsigned long long)counts[3]);
		return;
	}
	if (s_menuOpenedAt == 0 || (what == 2 && now - s_menuLoggedAt < 1'000'000))
		return;
	const unsigned long long ms = (now - s_menuOpenedAt) / 1000;
	if (what == 2)
		s_menuLoggedAt = now;
	else
		s_menuOpenedAt = 0;
	Log("in-game menu %s%llu ms: xrWaitFrame %llu, xrEndFrame %llu, hmd2 submits %llu, Cemu TV presents %llu",
		what == 2 ? "still open at " : "closed after ", ms, (unsigned long long)(counts[0] - s_menuAtOpen[0]),
		(unsigned long long)(counts[1] - s_menuAtOpen[1]), (unsigned long long)(counts[2] - s_menuAtOpen[2]),
		(unsigned long long)(counts[3] - s_menuAtOpen[3]));
}

// app/emulator.cpp, before every LoadExec (PS5Cemu_VrStop): Hmd2 off, from any thread. A title that
// ends with VrTracker2 live faults the tracker (third_party/hmd2/hmd2.h).
void BetterVR_PS5_Stop()
{
	s_gameInputBlocked = false; // the console's keyboard, if it was up, is the app's to end (app/ime_dialog.cpp)
	std::lock_guard lock(s.lock);
	if (s.stopped.exchange(true))
		return;
	ResetTriggersLocked("the app leaves VR");
	for (int i = 0; i < 2; i++)
		if (s.hands[i].vibrating)
			SetVibration(i, 0.0f);
	// the Vibration page's other devices: no motor left on when the game ends, and the pad's own rumble back
	if (s_headset.sent > 0)
		hmd2::set_vibration(0);
	s_headset.sent = 0;
	if (s.padTakenOver)
	{
		s.padTakenOver = false;
		PS5Pad_ExternalRumble(0.0f, 0.0f);
		s_pad.sent = 0;
		s_pad.sentSmall = 0;
		PS5Pad_SetExternalRumble(false);
	}
	if (s.session && s.session->hmdUp)
	{
		hmd2::shutdown();
		s.session->hmdUp = false;
		s_live = false;
		Log("headset off after %llu frames, before the app is replaced", (unsigned long long)s.session->frames);
	}
}

// app/emulator.cpp's VR-failed path (port/bettervr/VrPS5.cpp), before the app starts over: the effects live in the
// controllers' firmware, so they must go off even when the game is replaced without a session teardown.
void BetterVR_PS5_TriggersOff()
{
	std::lock_guard lock(s.lock);
	ResetTriggersLocked("VR failed");
}

} // extern "C"

// BetterVR's controls.cpp (patches/bettervr 0007), once an input frame: the bow's live draw (0-1.5), the shield's
// guard, whether the player is in gameplay (not a menu or the pause screen), BetterVR's setting and its strength.
extern "C" void BetterVR_PS5_AdaptiveTriggers(float bowDraw, int shieldGuarding, int interactive, int enabled, float strength)
{
	std::lock_guard lock(s.lock);
	if (s.stopped || s.session == XR_NULL_HANDLE)
		return;
	const uint64_t now = sceKernelGetProcessTime();
	if (s_triggerTest.until != 0)
	{
		if (now < s_triggerTest.until)
			return; // the Vibration page's Triggers test holds its step
		EndTriggerTestLocked(s_triggerTest.step); // its 15 s are over: both devices off, then the game's own state again
	}
	const bool active = interactive != 0 && enabled != 0;
	const ps5trig::Param param = ps5trig::Both(active && shieldGuarding != 0 ? ps5trig::Shield(strength) : ps5trig::Off(),
		active ? ps5trig::Bow(bowDraw, strength) : ps5trig::Off());
	SendDualSenseTriggers(param, now);
	SendSenseTriggers(param, now);
}

// BetterVR's controls.cpp: whether this Options press became the runtime's recenter (patches/bettervr 0024), on
// the input the caller names (true: the DualSense). The runtime's clock and threshold are the only ones, so a
// release that ends a recenter sends the game no pause, and a shorter release does.
extern "C" bool BetterVR_PS5_OptionsRecentered(bool pad)
{
	return (pad ? s.padOptionsRecentered : s.senseOptionsRecentered).load(std::memory_order_acquire);
}

// BetterVR's mod menu (patches/bettervr): Options on the right Sense controller, as of the last xrSyncActions
extern "C" bool BetterVR_PS5_OptionsDown()
{
	return s_optionsDown.load(std::memory_order_acquire);
}

// BetterVR's 2D panel (patches/bettervr 0030): how many times the view has been seated. A recenter turns the
// world under the head, so a panel whose gaze was locked before it is placed again from this frame's views.
extern "C" uint32_t BetterVR_PS5_RecenterGeneration()
{
	return s.recenterGeneration.load(std::memory_order_acquire);
}

// BetterVR's menu (patches/bettervr): whether either Sense controller is connected. Its actions read any connected
// hand, pose or not, so the pad's sticks may only join the menu when no hand is connected.
extern "C" bool BetterVR_PS5_SenseConnected()
{
	std::lock_guard lock(s.lock);
	for (const auto& hand : s.hands)
		if (hand.connected)
			return true;
	return false;
}
