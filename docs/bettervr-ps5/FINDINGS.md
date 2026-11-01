# Findings, 2026-11-01

What was checked in the code before this repo was set up. "Checked" means it was read in the
source or built. Nothing here has been run on a console.

## PS5-ECAM (this repo's base)

- **Base:** this repo starts from premohq/PS5-ECAM `269e273` (2026-10-31). The `upstream` branch
  tracks that.
- **Cemu version:** Cemu is pinned to cemu-project/Cemu `cc6fea12` (2026-10-04). That pin already
  includes Exzap's June 2026 Latte/Vulkan CPU-side optimizations ("widen conditions for fast draw
  mode", "incremental state update checking"). Exzap's fork has 10 newer commits (2026-10-28), all
  of them fixes, not speedups.
- **Build:** Ubuntu 24.04, clang-18. All pinned dependencies are git repos or release assets on
  GitHub (`tools/deps.json`). The Vulkan driver is Mihawk's RADV port, linked into the app.
- **Vulkan entry points:** `port/cemu/VulkanPS5.cpp` says the driver's `vkGetInstanceProcAddr`
  stands in for a loader. Cemu gets every Vulkan function through `PS5_VulkanGetGlobalProc`, which
  makes it one place to hand out wrapped functions in place of BetterVR's Vulkan layer.
- **Existing present-time hooks:** PS5-ECAM already hooks presentation for frame generation
  (MihawkGen) and ReShade (`patches/cemu/0042`, `0043`, `0047`, `port/cemu/FrameGenPS5.*`,
  `ReShadePS5.*`). Both must be off in VR.
- **App basics:** title ID PPSA99360 (`sce_sys/param.json`, `tools/package.sh`, `Makefile`). The
  boot log is `/data/ps5cemu/logs/boot.log`.
- **BotW status:** v208 is "Playable, about 60 fps at 1440p" (`docs/COMPATIBILITY.md`). Recent
  releases say it holds 60 at 4K more often.

## BetterVR upstream (Crementif/BotW-BetterVR)

- **Version and licence:** 0.9.23 = `main` = `2e06b31` (2026-08-24). MIT.
- **Size:** about 24.5k lines of its own source plus 2.1k in the Windows launcher.
  - Game hooks (camera, controls, bow, weapons, skeleton, entity controller): about 10.8k lines,
    essentially no Windows API.
  - Graphic pack `resources/BreathOfTheWild_BetterVR`: 32 PowerPC `.asm` patch files, about 8.7k
    lines. It needs the FPS++ graphic pack.
  - Rendering and OpenXR (`src/rendering/*`): about 4.8k lines. This is where Windows lives.
- **Connection to Cemu** (`src/hooking/cemu_hooks.h`): `GetProcAddress` on the Cemu module for
  `gameMeta_getTitleId`, `memory_getBase` and `osLib_registerHLEFunction`, then 74
  `osLib_registerHLEFunction("coreinit", "hook_…")` calls. It accepts title IDs
  00050000-101C9300/9400/9500.
- **Vulkan layer** (vkroots, `src/hooking/layer.cpp`):
  - Instance-level overrides: CreateInstance, EnumeratePhysicalDevices,
    GetPhysicalDeviceProperties, GetPhysicalDeviceQueueFamilyProperties, CreateDevice,
    DestroyInstance.
  - Device-level overrides: CreateImage, DestroyImage, CmdClearColorImage,
    CmdClearDepthStencilImage, CreateSwapchainKHR, QueuePresentKHR, QueueSubmit, DestroyDevice.
  - Frame capture works through the clear-image hooks (`src/hooking/framebuffer.cpp`).
- **D3D12 is still in use:** the 0.9.21 release notes say BetterVR "gets rid of D3D12 and only
  uses Vulkan now", but the public 0.9.23 code still creates its OpenXR session with
  `XrGraphicsBindingD3D12KHR` (`src/instance.h`). It also shares textures and fences with Vulkan
  through Win32 handles (`src/rendering/texture.cpp`). If a Vulkan-only upstream appears, compare
  it with the Linux backend.
- **Stereo rewrite:** the 0.9.21 notes describe the new stereo rendering as 10–30% faster.

## Linux backend (`patches/bettervr/0001-*.patch`)

- **Origin:** asingleoat/BotW-BetterVR-Linux `linux-port` (2026-05-28), merged into upstream 0.9.23
  using the conflict-resolution and post-merge patches from mitch030504/BotW-BetterVR-Linux
  `integration/upstream-0.9.23` (2026-09-30). That fork's CI never committed the merged result. It
  was reproduced here, and every hunk applied cleanly.
- **Build check:** compiles with clang 18 on Ubuntu 24.04 into `libBetterVR_Layer.so` with no
  errors. The dependencies were OpenXR-SDK 1.1.49 headers, glm 1.0.1, Vulkan-Headers 1.4.328,
  imgui 1.91.9 and implot 0.16, built as small shims rather than via vcpkg. Not run on a headset.
- **Main pieces:**
  - `src/rendering/renderer_vulkan.cpp` (about 2.2k lines)
  - `src/hooking/layer_linux.*`
  - D3D12 code behind `_WIN32`
- **Requirements:**
  - It requests `XR_KHR_vulkan_enable` and `XR_KHR_composition_layer_depth`, the latter
    unconditionally.
  - It asks for 2 extra queues: queue 0 for Cemu, 1 for the OpenXR runtime, 2 for its copies,
    synchronised with a timeline semaphore.
  - It expects Cemu to export the hook symbols (`DLLEXPORT` as visibility default plus
    `--export-dynamic`). Not needed when built into Cemu.
- **Status doc** (`LINUX_PORT_STATUS.md`, 2026-05-27), with SteamVR on Linux and NVIDIA:
  - **Working:** stereo, head tracking, HUD, menus, depth composition, late-latched pose, and an
    async submit thread that re-presents at the headset's refresh rate.
  - **Open:** a SteamVR compositor hang on NVIDIA, being watched.
  - **Untested:** whether the Linux capture path works with the 0.9.21 stereo rewrite. It was
    written against the older capture flow.

## PS5XR (theghostonline/HLA-PSVR2, `ps5xr/`)

- **Licence and firmware:** GPL-3.0. The files are `runtime/src/{ps5xr_host.h, psvr2.c, psvr2.h,
  vr_exports_1360.h, xr_debug.c, xr_exports.c, xr_input.c, xr_instance.c, xr_internal.h, xr_log.*,
  xr_session.c, xr_vulkan.c}`, `third_party/openxr/include/openxr` and `tools`.
  `vr_exports_1360.h` ties it to firmware 13.60.
- **Extensions advertised** (`xr_instance.c`, `kExtensions`): `XR_KHR_vulkan_enable2`,
  `XR_KHR_vulkan_enable`, `XR_KHR_convert_timespec_time`, `XR_EXT_local_floor`,
  `XR_FB_display_refresh_rate`, `XR_EXT_debug_utils`. Any other extension is rejected at
  `xrCreateInstance`. No depth layer, no D3D, no Win32 time.
- **Vulkan side** (`xr_vulkan.c`):
  - Uses the application's own instance, device and queue.
  - Swapchain images are device-local.
  - On submit it copies both eyes side by side into one of three host-visible buffers on the app's
    queue, then hands the buffer to Hmd2's reprojection with the render pose.
  - Formats: RGBA/BGRA in sRGB and UNORM, A2B10G10R10, R16G16B16A16_SFLOAT, plus depth formats.
- **Unconfirmed:** which interaction profiles it accepts. HLA-PSVR2 maps the Sense controllers to
  the Oculus Touch layout (through OpenComposite).
- **Not reachable from cloud sessions:** `git clone https://theghostonline.dev/...` gets a 403 from
  the egress proxy.

## photo-splat-vr's hmd2 (Patryk's own, `third_party/hmd2/`)

- **Source:** Papyszoo/photo-splat-vr `eddd5b7` (v0.1.1, 2026-10-27), `platform/ps5/hmd2/`.
  GPL-3.0-or-later. A public GitHub repo, so cloud sessions can clone it.
- **Status:** Patryk reports photo-splat-vr tested and working on his PS5 (13.60) with PSVR2, the
  Sense controllers and a DualSense.
- **What it is:** a PSVR2 library with no OpenXR and no GPU API of its own.
  - Its frame path is ported from theghostonline's PS5VR. Its pacing, the RADV buffer hand-off and
    the texture descriptor are ported from HLA-PSVR2's PS5XR (`psvr2.c`, `xr_session.c`,
    `xr_vulkan.c`).
  - In external mode it takes three linear RGBA8 side-by-side images that are RADV host-visible
    `vkMapMemory` pointers.
  - API: `init`/`wait_slot`/`begin_frame`/`acquire`/`submit`, `predicted_poses()` (head plus per-eye
    poses), `field_of_view()`, and `register_controller`/`controller_pose` for the Sense controllers
    through VrTracker2. A code comment marks the controller pose path "UNVERIFIED on hardware".
- **Controller input:** read with `scePadVrControllerReadState` (Sense) and the pad API
  (DualSense), using one table (`reference/pad_input.h`).
- **What it means for this project:** M2 doesn't need PS5XR. For M3, BetterVR still expects OpenXR,
  so either a small OpenXR layer goes on top of hmd2, or BetterVR calls hmd2 directly (see
  `PLAN.md`, M3).

## Reference: RE7-PSVR2 and HLA-PSVR2 behaviour

- **RE7:** about 35–60 fps at full resolution, with lock modes of 60 or 40 fps reprojected to
  120 Hz. TAA has to stay on, because turning it off causes bright dot grain in the headset.
- **HLA:** locked 60 fps at 1984×2048 per eye, filled to 120 Hz.

## Performance expectation (estimate, not measured)

- **BetterVR is CPU-bound.** Upstream recommends at least a recent i5 or Ryzen 5, and the author's
  own PCs used frame interpolation.
- **The PS5 CPU is Zen 2 at up to 3.5 GHz.** Flat BotW is about 60 fps at 1440p.
- **Expected VR frame rate:** roughly 30–40 fps, perhaps 40–50 with the 0.9.21 stereo speedup.
  That's enough for a 40 fps lock with reprojection, probably not 60 in the open world. M1's
  uncapped flat measurement will replace this guess.
