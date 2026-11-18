# BetterVR 1.0.0 on PS5: the work list

Planned 2026-11-16, not built, untested on console. Research behind it: BetterVR `2e06b31` to 1.0.0,
Cemu `cc6fea1` to `d4f7205`, the BetterVR Cemu fork, and premohq's renamed branch.

## Target and why

- **BetterVR 1.0.0** (`db5495b`, also the repository's HEAD). 0.9.23 has no future: the mod now builds
  into its own Cemu fork and 92 commits (530 files) restructured it.
- **Cemu: Crementif's fork** (`https://github.com/Crementif/Cemu.git`, branch `bettervr`, commit
  `86168a8`). It is upstream Cemu `3b10316` (2026-11-04) plus 34 commits: host modules, two-view
  (multiview) rendering in Latte/Vulkan, foveated shading, a window crop, a boot-title option, a vsync
  override. The pin `cc6fea1` is an ancestor of it. Plain Cemu cannot run 1.0.0.
- **Not merged: premohq's Playtendoh branch.** The PS5-ECAM branch is gone; the project was renamed
  Playtendoh (`d8a3466`, 2026-11-03). A merge conflicts in 75 entries (32 are `.po` files) and brings
  libretro and Switch 2 code this fork does not use. Its Cemu pin did not move (`cc6fea1`). Only
  Cemu-related changes are cherry-pick candidates (see Quick wins).

## Step 1: does the PS5's Vulkan driver run the fork?

The driver is Mihawk's RADV port (Mesa 26.2 fork, `tools/deps.json` item `ps5-mesa`, commit
`7b59ef27c1b0`), reporting the console as GFX1013 with GFX10.3's shader core.

| What the fork needs | Where | Driver | Verdict |
|---|---|---|---|
| `multiview` feature, `maxMultiviewViewCount >= 2`, `shaderSampledImageArrayDynamicIndexing` | fork `VulkanRenderer.cpp:301-320, 365-367` | `radv_physical_device.c:803` `KHR_multiview = true`, `:1111` `.multiview = true`, `:1994` `maxMultiviewViewCount = AC_MULTIVIEW_MAX_VIEWS` (unconditional) | **yes** (by source; not read from a console) |
| `multiviewGeometryShader` (optional) | `VulkanRenderer.cpp:320` | RADV sets it on GFX10+ | optional; without it only geometry-shader draws drop to two passes |
| `VK_KHR_fragment_shading_rate`: `attachmentFragmentShadingRate`, `pipelineFragmentShadingRate`, layered attachments (foveation) | `VulkanRenderer.cpp:306-311, 368-380, 850-857`; `VulkanRendererFoveation.cpp:12-16` | `radv_physical_device.c:779` `= pdev->info.has_vrs`; the PS5 winsys copies `has_vrs` from the GFX1013 model, and `radv_ps5_winsys.c:296` asserts it **false** (`ac_gpu_info.c:1074`: `has_vrs = gfx_level >= GFX10_3`) | **no** (by source) |
| `VK_EXT_external_memory_host` (optional) | `VulkanRenderer.h:469` | `has_userptr` | as today |

- **Fallbacks in the fork.** Multiview: `LatteView_UsesMultiview` returns false when the renderer does
  not support it (`LatteView.cpp:262-268`), so two-view scopes are drawn in two passes. Shading rate:
  `foveation_appliesTo` returns false without it (`VulkanRendererFoveation.cpp:12`), and
  `VulkanPipelineStableCache.cpp:279` skips cached pipelines that used it. The fork therefore starts
  and draws without foveation. Cost: no foveated shading, so the GPU shades the full eye image (the
  main reason the fork exists on weak GPUs; here it matters because the port already runs at about
  32 fps in the overworld at 9,000 to 10,000 draws a frame).
- **Verdict: go for multiview, no-go only on the foveation speed-up.** Multiview must be confirmed
  on the console because the winsys reports a made-up GPU identity.
- **Does Cemu's log say it?** No. It prints "Using GPU", "Driver version", "Vulkan: present_wait
  extension", and lines only for missing optional extensions (`VulkanRenderer.cpp:220-235, 322,
  384-422`); nothing about multiview or shading rate. The probe is in Stage 1: after line 367 and
  line 380 of the fork's `VulkanRenderer.cpp`, log `Vulkan: multiview {supported, maxViewCount,
  geometryShader}` and `Vulkan: fragment shading rate {supported, layered, 2x2, 4x4}` (about 6
  lines, in a numbered port patch). Until then the test request is "send log.txt" only for the
  device name and driver line.

## How the build changes

- **`tools/deps.json`:** `cemu` url becomes the fork, commit `86168a8`, same submodules
  (`ZArchive`, `Vulkan-Headers`, `imgui`; confirm the fork's submodule list). `bettervr` commit
  becomes `db5495b`. The pins `openxr-sdk`, `imgui-bettervr` and `implot-bettervr` are replaced by
  whatever 1.0.0 vendors (`dependencies/` in the repository; OpenXR loader, ImGui and ImPlot are
  vendored since `5c80aa6`), so check before keeping them.
- **BetterVR is no longer a separate object.** 1.0.0's `CMakeLists.txt:32` sets
  `CEMU_EXTERNAL_MODULES` to its own `src/`, and the Cemu build compiles it in
  (`src/Cafe/HostModule/HostModuleRegistry.cpp.in`). `tools/build-bettervr.sh` and
  `port/bettervr/layer/CMakeLists.txt` (which make `build/bettervr/bettervr.o`) go: the PS5 Cemu
  build passes `CEMU_EXTERNAL_MODULES=<.deps/BotW-BetterVR>/src` (with `patches/bettervr` applied
  there) and the PS5-only sources of `port/bettervr/` as a second module directory, or a
  platform file BetterVR's `src/utils/platform.h` selects. `tools/build-cemu.sh` and
  `patches/cemu/0048`/`0049` change accordingly.
- **Pack asm.** `resources/BreathOfTheWild_BetterVR/patch_compiled.asm` is checked in; the build
  compiles it only when `CemuPatchCompiler` is found (`CMakeLists.txt:208-262`). Use the checked-in file;
  no PowerPC compiler needed, unless a pack source is patched.
- **CI** (`.github/workflows/bettervr-build.yml`): the cached-checkout step (lines 91-97) resets the
  `ps5` branch of `.deps/BotW-BetterVR`; keep it, add the same for the fork, and key the caches on the
  new `tools/deps.json`. Expect a longer first build (Cemu rebuilt, 2-core runner).
- **C++ standard.** 1.0.0 needs C++23 and CMake 3.27; the PS5 toolchain is clang 18 / libc++ 18
  (`tools/ps5.cmake`). Upstream builds Linux with clang 19 in an Ubuntu 22.04 container
  (`06c6329`). Missing library parts surface at Stage 2.

## patches/cemu on the fork (49 patches)

Trial: `git am --3way` in order on `86168a8` (29 clean, 20 conflict). The same trial on Cemu HEAD
`d4f7205` (36 clean, 13 conflict) is given so the two causes can be told apart: a conflict in both is
upstream drift, a conflict only on the fork is the fork's renderer work. Patches 0001 to 0047 are
premohq's; 0048 and 0049 are this repository's.

| Patch | Fork 86168a8 | Cemu HEAD d4f7205 | Conflict files and why |
|---|---|---|---|
| 0001 | **conflict** | conflict | src/CMakeLists.txt, HW/Espresso/Recompiler/BackendX64/BackendX64.cpp: both bases moved the CMake include/PCH layout; BackendX64.cpp too (PPCRec refactor 3f8dd91) |
| 0002 | **conflict** | conflict | CMakeLists.txt, src/input/CMakeLists.txt: input/DualSense CMake lists moved |
| 0003 | **conflict** | conflict | src/CMakeLists.txt: CemuBin target lines moved |
| 0004 | clean | clean |  |
| 0005 | **conflict** | conflict | Vulkan/VulkanAPI.cpp: the fork adds multiview and shading-rate entries next to the extension list this patch edits |
| 0006 | clean | clean |  |
| 0007 | clean | clean |  |
| 0008 | clean | clean |  |
| 0009 | **conflict** | conflict | src/util/helpers/helpers.cpp: upstream timer rework, one hunk |
| 0010 | clean | clean |  |
| 0011 | clean | clean |  |
| 0012 | clean | clean |  |
| 0013 | clean | clean |  |
| 0014 | **conflict** | clean | Vulkan/VulkanRenderer.cpp: the fork rewrote the copy path around the readback checks; compare with Cemu fced55a and 42876aa first |
| 0015 | **conflict** | clean | Vulkan/TextureReadbackVk.cpp: same area as 0014 |
| 0016 | clean | clean |  |
| 0017 | clean | clean |  |
| 0018 | **conflict** | clean | Vulkan/VulkanRenderer.cpp: in-game menu picture, context moved by the fork |
| 0019 | **conflict** | conflict | src/audio/IAudioAPI.cpp: upstream change, one hunk |
| 0020 | **conflict** | clean | Vulkan/VulkanRenderer.cpp: the fork submits every 1000 draws instead of 300 (b2b35e2); may be redundant in part |
| 0021 | clean | clean |  |
| 0022 | clean | clean |  |
| 0023 | **conflict** | conflict | OS/libs/nsyshid/nsyshid.cpp: upstream change, one hunk |
| 0024 | clean | clean |  |
| 0025 | clean | clean |  |
| 0026 | clean | clean |  |
| 0027 | clean | clean |  |
| 0028 | **conflict** | clean | Vulkan/VulkanAPI.h, Vulkan/VulkanRenderer.cpp: the fork batches submits, so the timing points move |
| 0029 | clean | clean |  |
| 0030 | clean | clean |  |
| 0031 | **conflict** | clean | Vulkan/VulkanRenderer.cpp: the fork edits the same feature block |
| 0032 | clean | clean |  |
| 0033 | **conflict** | clean | Vulkan/VulkanRenderer.cpp: GamePad touch-screen picture, context moved |
| 0034 | clean | clean |  |
| 0035 | clean | clean |  |
| 0036 | clean | clean |  |
| 0037 | clean | clean |  |
| 0038 | clean | clean |  |
| 0039 | **conflict** | conflict | OS/libs/swkbd/swkbd.cpp: upstream change, one hunk |
| 0040 | clean | clean |  |
| 0041 | clean | clean |  |
| 0042 | **conflict** | conflict | CMakeLists.txt: the fork adds HostModule files to the same list |
| 0043 | **conflict** | conflict | CMakeLists.txt, Latte/Core/LatteRenderTarget.cpp, Vulkan/VulkanRenderer.cpp: ReShade on the TV picture meets the fork's render-target and two-view changes (3 files) |
| 0044 | clean | clean |  |
| 0045 | clean | clean |  |
| 0046 | clean | clean |  |
| 0047 | **conflict** | conflict | Vulkan/VulkanRenderer.cpp: frame-generation picture, context moved |
| 0048 | **conflict** | conflict | CMakeLists.txt, Vulkan/VulkanRenderer.cpp: redo on the host module (HostEvent surfaces); 4 hunks |
| 0049 | **conflict** | conflict | CMakeLists.txt: replaced by CEMU_EXTERNAL_MODULES; CMake only |

## patches/bettervr on 1.0.0 (25 patches)

Trial: 1 clean (0007), 24 conflict, because the files moved or were deleted. "Obsolete" means 1.0.0
solves the problem or removes the code (grep of 1.0.0, not a headset result). Each "obsolete" is to be
confirmed on a headset in Stage 3 or 4 before the patch is deleted for good.

| Patch | What it did | Decision | Reason |
|---|---|---|---|
| 0001 | Linux Vulkan/OpenXR backend | obsolete | 1.0.0 has its own Linux x64/ARM64 build (src/utils/platform_linux.cpp, vk_backend.cpp, 8c64e3b, 844db81) |
| 0002 | PS5 ELF glue, libc++ 18 | redo | the PS5 platform layer for the host module: no sys/sysinfo.h, no browser, libc++ 18 formatter and strtod workarounds, one queue lock (ps5xr::QueueMutex) |
| 0003 | log to boot log, settings in the data folder | redo | 1.0.0 asks HostModule::GetUserDataPath() for BetterVR_log.txt and BetterVR_settings.ini (host_module.cpp:235-259); the PS5 port answers that path and mirrors lines to the boot log |
| 0004 | which hooks run, VR-failed to library | redo | VR-failed fallback (issue #3) must hook the module start and the first eye clear again; the hook list is gone with src/hooking |
| 0005 | large input/game state through a mutex | redo (re-check) | libc++ 18 lowers large std::atomic to __atomic_load/store with sizes the console libc lacks (SIGILL 0x800102040); check whether 1.0.0 still has std::atomic<InputState>/<GameState> and do the same |
| 0006 | stick/Link position diag line | obsolete | diagnostic for the two-pass physics problem; 1.0.0 replays one simulation |
| 0007 | Graphics pack with PS5 eye sizes, 0.5x | redo | 1.0.0 bundles the packs and fills presets from the headset resolution at start (host_module.cpp:197, 251); give it hmd2's eye size instead of editing placeholders |
| 0008 | XR input without the ImGui overlay | re-check | 1.0.0 has one overlay path (vulkan_imgui.cpp); drop unless input is skipped without the overlay on the PS5 |
| 0009 | aspect ratio of the rendered image | obsolete | 1.0.0 renders eye-shaped images through the fork's two-view path; no 16:9 default left |
| 0010 | roomscale switch, physics counters | obsolete | diagnosis for the late-physics problem of the two-pass model |
| 0011 | seated height moves the stage space | re-check | keep as a setting if 1.0.0 still pins the head to 1.65 m by shifting views; `seated` code exists in controls.cpp and roomscale.cpp |
| 0012 | bettervr_settings.ini is read | obsolete | Settings_ReadFile in host_module.cpp:259 |
| 0013 | player physics gate once a game frame | obsolete | OnlyRunPlayerUpdateJobOnce is gone from 1.0.0 |
| 0014 | hot hooks stop copying actors/strings | obsolete | the hooking layer is gone; re-profile instead |
| 0015 | flat view, cutscene bars, comfort fades | obsolete | upstream's renderer.cpp does these on every platform now |
| 0016 | swing analyser velocity bits | obsolete | 1.0.0 checks velocityFlags (controls.cpp:267, 803) |
| 0017 | one view pair frozen for both eyes | obsolete | replay model; no stereo pair published from two passes |
| 0018 | stereoRays right-eye shadow | obsolete | calcViewMaskedLightShadow no longer in the pack |
| 0019 | weapon analyser resets once a frame | obsolete | weapon code rewritten (weapon_motion.cpp) |
| 0020 | flat view shows the scene, menu mono frame once | obsolete | Linux renderer_vulkan.cpp gone; re-test the menu on a headset |
| 0021 | left eye's 3D slot clear of the published pair | obsolete | same |
| 0022 | a menu's mono frame lasts the menu | obsolete | same; 1.0.0 has mono sections in two-view scopes (d16aa10) |
| 0023 | menus fixed in the room while open | obsolete | 1.0.0 redesigned the VR menu (f7fb77b) and opens it in front of the player (99f43ee) |
| 0024 | autosave snapshot holds last stereo pair | obsolete | replay model; re-test autosave |
| 0025 | UI blend fix and screen log | obsolete | vk_backend.cpp has its own blend state; the screen log needs a check |

Count: 15 obsolete, 5 redo, 3 re-check, 0 keep as is (0007 applies textually but its placeholders go).

## PS5 pieces to redo

- **hmd2/OpenXR path.** Keep `port/bettervr/xr/runtime.cpp` (2,390 lines) and `SensePS5.cpp`: the
  OpenXR runtime over hmd2. 1.0.0's `openxr.cpp:98-108` accepts `XR_KHR_vulkan_enable2` or
  `XR_KHR_vulkan_enable` (ours lists only the latter at `runtime.cpp:1177`), `XR_KHR_composition_layer_depth`
  (listed) and treats the time-convert extension as optional (`openxr.cpp:150`). The session binding is
  `XrGraphicsBindingVulkan2KHR` (`instance.h:29`): the runtime must take it, or list `vulkan_enable2`.
  Backport commit `7fbceeb` ("OpenXR runtimes without compositor depth support") as it applies to a
  runtime that ignores depth. The OpenXR instance must exist before Cemu loads its packs
  (`host_module.cpp:251`), so the runtime starts earlier than today (`PS5Cemu_BetterVRPrepareGraphicPack`
  runs before packs load already).
- **Queue lock.** The console has one graphics queue; `ps5xr::QueueMutex` around every submit and
  present must wrap the module's submits and the fork's 1000-draw batching.
- **Settings ini.** `BetterVR_settings.ini` and `BetterVR_log.txt` go where `HostModule::GetUserDataPath()`
  points; set that to the app's data folder (the working directory is not writable). The old
  `bettervr.json` switches (`roomscale`, `stereoRays`, `graphicsPack`) are gone with the patches that
  read them; keep `bettervr.json` only for the VR on/off switch the launcher reads. 1.0.0 has
  `TakeOverOlderReleaseSettings` (`host_module.cpp:240`) for old files.
- **Launcher Play in VR / Play on TV.** `PS5Cemu_BetterVRApplyMode` (`port/bettervr/VrPS5.cpp:547`)
  toggles the pack "Better VR" and "FPS++" by name. 1.0.0 loads its own packs from the module
  (`GraphicPack: Load packs from host modules`, `8496174`), so Play on TV means not registering the
  module at all, or a module switch; BetterVR's patches call hooks that exist only while it runs
  (comment at `VrPS5.cpp:516`). Needs a design decision at Stage 3: start the module only for VR, which
  the registry in `HostModuleRegistry.cpp.in` suggests but has not been read in detail.
- **Graphic packs.** 1.0.0 bundles the community packs (FPS++ and the others it depends on,
  `996f9c8`) and the "Graphics" pack, whose presets are filled from the headset. Our
  `assets/bettervr/BreathOfTheWild_BetterVR` and `_Graphics` copy (`VrPS5.cpp:521-541`), and the
  by-name toggling, are replaced by: the module's own packs, plus a PS5 resolution passed in (patch
  0007's job). The player's Graphic packs choice for "Graphics (Modified Version For BetterVR)" (build20)
  must stay the player's: confirm the bundled pack keeps a switch in Cemu's list. 1.0.0 also removes the
  pack the 0.9.23 launcher left (`5424405`) and turns Extended Memory on by default (`ec3b787`).
- **TV picture while in VR** (backlog since 2026-11-05, and issue #2): the fork's host events give
  surfaces (`HostEvent`, `EventSurface`), the natural way to redo patch `0048`.
- **In-game menu.** 1.0.0's VR menu replaces the "menu does not exist" caveat in `INSTALL.md`.
  Check it works with the Sense controllers on the DualSense/hmd2 path before telling players.
- **Update check** (`update_check.cpp`) and the `BetterVR_Launcher` links: off on the PS5.
- **Linux AppImage notes** in 1.0.0 (xdg paths, `7452360` "layers loaded twice") do not apply; skip.

## Stages

Each stage ends in a pre-release build (tag `testing`), notes starting "Untested on console", and one
numbered Test request in `STATUS.md`. Numbers continue from 33.

1. **Fork base, flat only (about 3 to 4 days).** `tools/deps.json` cemu pin to `86168a8`; rebase
   the 47 premohq patches (conflict files above; most are CMake lists and `VulkanRenderer.cpp`
   context) and port 0048/0049 as stubs; BetterVR not compiled in; add the multiview and
   shading-rate probe lines. Host tests (`make test`) pass. Done: `make release` builds; BotW and one more
   game boot flat with the same fps as the current build; the new log lines are there.
   **Test request 34.** Install, play BotW flat (Kakariko or Hateno, 2 minutes). Send `log.txt` and the
   boot log: `Using GPU`, `Driver version`, the `multiview` and `fragment shading rate` lines, the fps
   and GPU-busy lines, any `[crash]`. Expected: multiview supported, max views at least 2; shading rate
   `unsupported`. Say whether the picture and fps match the last build.
2. **BetterVR 1.0.0 compiled in, no headset (about 1 week).** `CEMU_EXTERNAL_MODULES`, the PS5
   platform layer (log, data path, atomics mutex, libc++ 18 gaps), the runtime from `port/bettervr/xr`
   linked as the OpenXR loader replacement. Done: the app builds and links; with VR off BotW runs flat
   as in stage 1; with VR on, the log shows the module starting, the OpenXR instance, the system, and the
   packs loaded, ending at the first frame without a crash. **Test request 35.** Start BotW with Play
   in VR, then Play on TV. Send the logs zip, every `[bettervr]` and `[vr]` line, `hmd2` lines, and
   `BetterVR_log.txt`. Say whether the headset shows anything and whether TV mode still works.
3. **Stereo in the headset (about 1 to 2 weeks).** Two-view submitted to hmd2 eye buffers through the
   queue lock; checked-in `patch_compiled.asm`; foveation off; the VR-failed fallback (issue #3) back
   in; settings ini; Graphics pack/preset resolution from the headset. Done: BotW in stereo, head
   tracking, no `[crash]`, fps and GPU-busy lines logged against the current build's 31.6 to 34 fps
   overworld and 60 fps flat. **Test request 36.** Play two minutes at the start and two in a village.
   Send the logs zip. Say: is the picture correct in both eyes (depth, no ghosting), what the fps line says,
   and whether the head follows. Also fail on purpose once with the headset off and report the
   screen.
4. **Controllers and menu (about 1 week).** Sense sticks, grip gestures, the 1.0.0 menu, haptics via
   `SensePS5.cpp`; decide the keep/redo patches from the table by headset check (0008, 0011, 0005).
   Done: walk, fight, bow, magnesis and the menu work; the "late physics" symptom of
   2026-11-05 is gone or recorded. **Test request 37.** The numbered checks of the old Test requests 5
   and 6 (stick delay, swing, magnesis, menu), plus: open the VR menu with the Sense button and
   adjust one setting.
5. **TV picture, launcher, release (about 3 to 4 days).** Patch 0048 redone on host events, Play in VR
   and Play on TV choices, INSTALL.md rewritten, update check off, quick wins from the next section
   that are not in the fork yet. Done: tests pass on host; rolling pre-release published.
   **Test request 38.** Full path from the library: Play in VR, quit to the library, Play on TV, Play
   in VR again; say what the TV shows during VR.

Total: about 4 to 5 weeks of work for one agent, most of it stage 2 and 3. Stop and report if stage
1's Test request shows multiview unsupported: then the two-pass fallback doubles draw cost and the
target moves back to 0.9.23 plus the quick wins.

## Quick wins on the current base (BetterVR 2e06b31, Cemu cc6fea1)

Not part of the port; each is independent of the fork and was cherry-picked in a scratch clone
(applies to cc6fea1 with all 49 patches on it unless noted). Effect on frame rate is not measured.

| Id | What | Size | Lands in | Risk |
|---|---|---|---|---|
| `babd690` + `b044641` | GraphicPack: patch groups resolved in linear time; full code cave per group | 6+/12-, 11+/3- lines | `src/Cafe/GraphicPack` (new `patches/cemu/00NN`) | low; helps BetterVR's thousands of patch groups at start |
| `feb7c7b` | Vulkan: async pipeline compile race | 2+/1- | `VulkanPipelineCompiler`/renderer | low; next to our 0027 and 0035 (compile threads) |
| `d1ca2e9` | PPCRec: function scan read of unmapped memory | 14+/4- | `src/Cafe/HW/Espresso/Recompiler` | low; crash fix |
| `5a0430b` + `934329f` | gs copyshader crash; GX2 flush starts a bigger command buffer | 4+/1-, 1+/1- | Latte core, GX2 | low |
| `fced55a`, `34b5908`, `182b0bc` | Latte readback heuristic, colour-buffer mask, memcpy-style texture copies | 8, 48, 19 lines | Latte | medium; may overlap our 0014/0015 and cost on the GPU thread (9,000 to 10,000 draws a frame) |
| `47ea413` (BetterVR) | `GetPhysicalDeviceMemoryProperties2` instead of the KHR alias (null when the extension is not enabled) | 1 line | `src/rendering/vulkan.cpp` | lowest; possible cause for a failed start on a driver that leaves the alias null (check against issue #3 logs) |
| `c86bb01` (BetterVR) | shrink grass and flower particles from cutting plants | 34 lines of pack asm | `resources/BreathOfTheWild_BetterVR/patch_compiled.asm` source | low; comfort, not a fix |

Not quick wins: `540487b` and `8303fed` (recompiler entry; the XMM6-15 save is a Windows-ABI fix, and
540487b conflicts with our 0001); `42876aa`, `3f8dd91`, `5b430d6`, `bb28394`, `9bfdf81` (conflict with
our patches or touch 15 to 65 files); Playtendoh's Cemu patches `0049` (alpha texture, 50 lines) and
`0050` (loading screen, 55 lines) apply but need Playtendoh's port code (`PS5Cemu_DrawLoading`), and
`0048` (DolphinBar, 138 lines) has no use here. BetterVR's other post-0.9.23 fixes (`67a2e03` session
restart, `1cf3fc9` desktop view on skipped frame, `03daaaa` water colour, `7b0557b` HUD once) sit on
the restructured code and conflict on 0.9.23; they arrive with the port.

## Risks

- Multiview works by source but is untested on the console; foveation is absent: the GPU shades every
  pixel of both eyes, as it does today.
- The fork's renderer work collides with frame generation, ReShade, FSR and the TV picture
  (0042, 0043, 0047). Keep the fork's `VulkanRenderer.cpp` changes minimal and rebase in order.
- Toolchain: C++23 and CMake 3.27 against clang 18 / libc++ 18, and the large-atomic SIGILL class of
  bug (patch 0005).
- Nothing here has run on the console; every stage is "built, untested on console" until a result is
  reported.

## Stage 1 progress

Branch `bettervr-1.0/stage1`. Built only on CI; every result is "built, untested on console".

Done:
- `tools/deps.json`: cemu is `https://github.com/Crementif/Cemu.git` at `86168a81a65d8962c3a19d0e57e9ab26acc37206`.
  The fork's `.gitmodules` has the same three submodules the build uses (`ZArchive`, `Vulkan-Headers`, `imgui`;
  it also lists cubeb, vcpkg, metal-cpp and xbyak_aarch64, which this build does not take).
- `patches/cemu` rebased on the fork with `git am --3way` in order, re-exported with
  `tools/cemu-patches.sh export` (it numbers the files again: 0001 to 0049 keep their numbers, the
  rest moved up). Refreshed by hand: 0001 (CMake: fork's module registry stays, the PS5 `CemuBin` also
  links `HostModuleRegistry.cpp`; `BackendX64.cpp`: fork's `s_enterRecompilerCodeExit` kept), 0014 (header
  include), 0020 (the fork's submit threshold is 1000: the port's `PS5Cemu_SubmitDraws()` still sets it, default
  stays 300; the comment says so), 0023 (includes), 0028 (`vkCmdWriteTimestamp` entry, `PS5_TimestampEnd()`
  before `vkEndCommandBuffer`, since the fork has no occlusion-query hook there), 0031 (comment context), 0034
  (`kFsrFilter` branch), 0039 (`swkbd.cpp` keeps the fork's `CafeStaticVar` state, the PS5 counter stays),
  0048 (destructor hunk goes before `HostModule_NotifyDeviceDestroying()`), 0049 (see below).
  The other patches applied cleanly (0002, 0003, 0005, 0009, 0042, 0043 conflicted on trial and merged by 3-way).
- Dropped, already in the fork: old 0050 (`babd690`, `b044641` = fork `babd6906`, `b0446411`), old 0051 (`feb7c7b` =
  fork `feb7c7b0`), old 0052 (`d1ca2e9` = fork `d1ca2e98`). Old 0053 keeps only the gs copyshader
  half (now 0050); its GX2 flush half (`934329f`) is fork `934329f9`.
- 0048/0049 as stubs: 0048 (TV picture in the headset through hmd2) applies as is. 0049 got the CMake option
  `PS5CEMU_BETTERVR_LAYER` (default ON, the 0.9.23 object); `tools/build-cemu.sh` passes OFF, which compiles
  `port/bettervr/BetterVRStubPS5.cpp` (the object's four exports as no-ops, Negotiate fails) and defines
  `CEMU_PS5_NO_BETTERVR`. `port/bettervr/VrPS5.cpp` then never picks BetterVR mode (Play in VR on BotW shows the TV's
  picture in the headset, with a log line) and never turns BetterVR's pack on. `make build` and `make check` no
  longer depend on `make bettervr`; `tools/build-bettervr.sh` and `port/bettervr/layer` stay for stage 2 to replace.
- New 0051: the probe lines `Vulkan: multiview: ..., max views: ..., geometry shader: ...` and
  `Vulkan: fragment shading rate: ..., layered: ..., 2x2: ..., 4x4: ...`, after the fork's feature checks.
- CI: `.github/workflows/bettervr-build.yml` also builds `bettervr-1.0/**`; the deps cache falls back to older
  `bettervr-deps-` keys (RADV and SDK builds) and a new step removes a cached `.deps/Cemu` that is not the fork at the
  pin, or resets its `ps5` branch to the pin.
- Host tests: the ones in `docs/BUILDING.md` and `make test-*` need Linux packages and do not touch Cemu patches; left to CI.

CI: run 44198974605 green on 576d15a (`make release`); the ELF contains `Vulkan: multiview` and
`fragment shading rate`, and no BetterVR strings. A read-only review of the diff found no high or
medium defects (hand-merged patches keep their change; dropped patches are ancestors of `86168a8`).
Test request 46 in `STATUS.md` (numbers continue from 45, not 34).
Backlog: the workflow's "Cemu from the pin" step does not reset submodules of a restored
`.deps/Cemu` (harmless while the submodule pins are unchanged); the `bettervr` pin in
`tools/deps.json` still serves the 0.9.23 graphic packs for `tools/package.sh` until stage 2.
Next step: Test request 46 on the console; stage 2 (`CEMU_EXTERNAL_MODULES`, BetterVR 1.0.0 `db5495b`
compiled in) can start on this branch meanwhile.

## Stage 2 progress

Branch `bettervr-1.0/stage2` (from stage 1's head 795b56f). Built only on CI; every result is "built,
untested on console".

How it fits together:
- **Build.** `tools/build-cemu.sh` applies `patches/bettervr` (`tools/bettervr-patches.sh`, branch `ps5` in
  `.deps/BotW-BetterVR`, pin `db5495b`) and passes `-DCEMU_EXTERNAL_MODULES=.deps/BotW-BetterVR/src`. BetterVR's
  `src/CMakeLists.txt` (patch 0001) includes `port/bettervr/module.cmake` for `CEMU_PS5`, which adds
  `PlatformPS5.cpp` and `xr/runtime.cpp` to the module's library instead of `platform_linux.cpp` and the
  vendored OpenXR loader (only its headers are used: `dependencies/CMakeLists.txt`). The module needs
  nothing the Cemu build lacks (glm, fmt, pugixml, curl are Cemu's; ImGui headers equal the fork's, checked).
  Gone: `tools/build-bettervr.sh`, `make bettervr`, `port/bettervr/layer/`, `BetterVRStubPS5.cpp`,
  `VulkanLayerPS5.cpp` (1.0.0 creates its own Vulkan instance and device through the host module),
  `PS5CEMU_BETTERVR_LAYER` (old `patches/cemu` 0049).
- **Deps.** `bettervr` is `db5495b`. `openxr-sdk`, `imgui-bettervr` and `implot-bettervr` are dropped:
  1.0.0's `dependencies/` has the OpenXR headers, ImGui headers (equal to the fork's `dependencies/imgui`,
  `f65bcf4`), ImPlot and its own Vulkan ImGui backend. Its `cemu-src` submodule is not fetched.
- **Starting the module.** The PS5 app has no `main.cpp`, so `ps5emu::StartBetterVR` (`port/app/emulator.cpp`)
  calls `HostModule::Startup`, `BeforeConfigLoad`, `Initialize` and loads the graphic packs again, in
  `LaunchGame` after `PrepareForegroundTitle` succeeds, only when `PS5Cemu_BetterVRWanted` (`port/bettervr/VrPS5.cpp`: Breath of the Wild,
  `bettervr.json` "vr" on, "bettervr" not false). Play on TV registers no module. The packs are the app's
  `assets/bettervr/BetterVR/packs` (`tools/package.sh`, as BetterVR's Linux download lays them out);
  `Platform::GetExecutableDirectory` is `<app>/assets/bettervr`. `PS5Cemu_BetterVRPrepareGraphicPack` now only
  removes the `graphicPacks/bettervr` folder older builds made; the by-name toggling of "Better VR" and
  "FPS++" (`PS5Cemu_BetterVRApplyMode`) is gone. BetterVR's settings and log are in Cemu's data folder
  (`HostModule::GetUserDataPath()`: `BetterVR_settings.ini`, `BetterVR_log.txt`).
- **Log.** The module's lines also go to the boot log as `[bettervr] ...` (`BetterVR_PS5_LogBatch`, patch 0001);
  the app's own lines: `[emu] BetterVR's host module starts`, the runtime's `[vr] ...`.

`patches/bettervr` (28 on 0.9.23, now 1):
- **0001 (new, 1.0.0):** the CMake switch above; `BETTERVR_PS5_QUEUE_LOCK` around the module's own submit
  (`vk_backend.cpp`); libc++ 18 gaps: `std::ranges::contains` (`game.cpp`), floating-point `std::from_chars`
  (`mod_settings.h`, strtod as in old 0002), `gettid` (`logger.cpp`); boot-log mirror; no update check; no
  settings sharing with the Cemu fork's second settings file (`host_module.cpp`: `Sync`/`Enforce`/`ShareBack`
  skipped).
- **Redone:** old 0002 (platform, libc++, queue lock) and 0003 (log, data path) into 0001 and `PlatformPS5.cpp`;
  old 0005 (large atomics through a mutex) as the generic `__atomic_load/store/exchange/compare_exchange`
  defined in `PlatformPS5.cpp` (libatomic's interface, a lock per address, in place of the system
  libSceLibcInternal's version that gave SIGILL on 0x6c0 bytes): covers every std::atomic of a large struct
  in 1.0.0 (camera matrix, game state, events) without editing them. Not confirmed on console.
- **Dropped, purpose gone (reasons as the table in "patches/bettervr on 1.0.0"):** 0001 (Linux backend:
  upstream has it), 0004 (hook list gone; the VR-failed fallback returns in stage 3, `BetterVR_PS5_VrFailed`
  stays in `VrPS5.cpp` unused), 0006, 0009, 0010, 0013 to 0025 (two-pass model, flat view and menu pictures,
  replay model), 0026 (1.0.0 has `GetPhysicalDeviceMemoryProperties2`), 0012 (settings file read upstream),
  0007 (the headset's eye size now comes from the runtime: `xrEnumerateViewConfigurationViews`; the pack's
  presets are filled at start by `WriteGraphicsPack`).
- **Dropped for stage 4:** 0027, 0028 (the Options menu toggle on the 0.9.23 ImGui path) and the Sense
  haptics side stay in `SensePS5.cpp` untouched; the 1.0.0 menu is opened by its own action in stage 4. Not
  redone: 0008 (XR input without ImGui), 0011 (seated height): re-check on a headset in stage 4.
- Count: 28 on 0.9.23 -> kept 0, redone 3 (into 0001/`PlatformPS5.cpp`), dropped 25 (incl. 0027/0028 for stage 4).

`patches/cemu` (51 -> 51): 0049 (stub layer CMake) removed; new 0051 puts `ps5xr::QueueMutex` around the
renderer's `vkQueueSubmit` and `vkQueuePresentKHR` (the runtime and the module's own submit already take it),
as the 0.9.23 layer's hooks did for Cemu's calls. Old 0050 and 0051 are now 0049 and 0050.

Stand-ins: `runtime.cpp` still asks for 0.9.23's `BetterVR_PS5_PlayerOnce/Fades/FlatScene/MonoQuad/MenuLock/QuadPlane/PoseKeep/SaveHold/PoseFreeze/LeftSlot/StereoRays`
and the three `*Proof` counters; `PlatformPS5.cpp` answers all off and zero. Stage 3 takes that use out of the
runtime.

CI and review (built, untested on console): CI is green on 77e06ae (run 44208990960); the ELF contains
"BetterVR's host module starts", "Acquired system to be used", "Vulkan: multiview" and "BetterVR Settings".
A code review of 77e06ae found three defects, fixed after it:
- The module started at the top of `LaunchGame` and cannot be unregistered, so a failed Play in VR followed by
  Play on TV in the same process ran BetterVR, and a second Play in VR registered it twice. Now
  `StartBetterVR` runs after `PrepareForegroundTitle` succeeds, once per process (a static flag), and the
  module's `CreateVkInstance` (patch 0001) stays inactive unless `PS5Cemu_BetterVRWanted` holds for that launch.
- ImGui's font upload (`imgui_impl_vulkan.cpp`) and `vk_backend.cpp`'s `vkQueueWaitIdle` calls ran without
  `ps5xr::QueueMutex`; they now hold it (patch 0001).
- A stale comment in `runtime.cpp` named the removed `port/bettervr/layer`.

Open problems and risks (none seen on a console):
- The module is started in `LaunchGame`, on the thread that launches the game, and `GraphicPack2::ClearGraphicPacks`
  and `LoadAll` run again then: a launcher thread listing packs at that moment would race. Not checked.
- `HostModule::GetTitleId()` (what `CreateVkInstance` checks) is `CafeSystem::GetForegroundTitleId`, set by
  `PrepareForegroundTitle`, which `LaunchGame` runs after `StartSystem` but before Cemu creates its renderer;
  if the renderer is made earlier, the module stays inactive and the log says nothing: check the order in the
  first log (`Cemu's Vulkan instance` lines).
- The old runtime lists only `XR_KHR_vulkan_enable`; 1.0.0 takes that path (`usesVulkanEnable2` false). Its
  `XrGraphicsBindingVulkan2KHR` has the layout of the KHR one. `7fbceeb` (no compositor depth) is not backported:
  the runtime lists `XR_KHR_composition_layer_depth` as before.
- The module's Cemu settings sync, which forces `vkAccurateBarriers=false` and the VSync mode, is off; the app's
  own settings stand. Whether `vkAccurateBarriers` should be false for speed is a stage 3 question.
- If the OpenXR instance fails, the module stays off (`BetterVR is off for this run`) and the game runs with an
  empty headset; the flat fallback is stage 3.
- `__atomic_*` defined in the app: if the link finds them twice (a static compiler runtime with them), the
  build fails with a duplicate symbol; remove the member that clashes.
- The settings ini and the log are in the Cemu data folder; BetterVR copies packs with `std::filesystem::copy`
  there, which the app did before for 35 files.

Backlog (outside stage 2): the `bettervr.json` keys `roomscale`, `stereoRays`, `graphicsPack` and the other
old switches are no longer read; `docs/bettervr-ps5/INSTALL.md` still describes the 0.9.23 flow (stage 5).

CI: run 44204670020 on de2a992. Its first attempt failed in `make deps` before any compile: a fresh
`.deps` (the stage 1 branch's cache is not readable from this branch) and the boilerplate's zlib download
from zlib.net gave a wrong checksum (the file is right when fetched by hand; a transient answer). Re-run
started; a full build from empty caches takes hours (RADV included). The `Tests` workflow fails on the
Eden layout copy check (`port/app/system_layout.h` differs from `ecam_layout.h`), as it did on stage 1 and
`testing`: not from this branch.
Local checks done: the module's sources that were patched, and `xr/runtime.cpp` against 1.0.0's vendored
OpenXR headers, pass a clang `-fsyntax-only` with `-D__PROSPERO__` (macOS libc++, not libc++ 18); every `xr*`
call 1.0.0 makes exists in the runtime; the patch applies on the pin and equals the branch.
Next step: read `gh run list --branch bettervr-1.0/stage2`; on failure `gh run view <id> --log-failed | tail -80`
and fix the first error (likeliest: a libc++ 18 gap in the module, a duplicate `__atomic_*` symbol, a missing
include dir for `PlatformPS5.cpp`/`runtime.cpp` in `module.cmake`); on green, check the ELF for the strings
`BetterVR's host module starts` and `Acquired system to be used`, then publish nothing and report to the user.

## Stage 3 progress

Branch `bettervr-1.0/stage3` (from stage 2's head c462dda). Built only on CI; every result is "built, untested
on console". Written blind: each step of the VR start logs a line, so one console run shows how far it gets.

Done:
- **What 1.0.0 asks of the runtime**, read from its calls (`swapchain.cpp`, `openxr.cpp`, `renderer.cpp`), and
  what `port/bettervr/xr/runtime.cpp` now answers:
  - swapchains: 2D images, `arraySize 1`, `sampleCount 1`, colour `R8G8B8A8_SRGB` first of the offered formats,
    three images each; BetterVR's stereo is two swapchains (one per eye) plus one for the HUD. A request for more
    array layers, faces or samples returns `XR_ERROR_FEATURE_UNSUPPORTED` with a log line. The multiview rendering
    of the Cemu fork happens before the pictures reach the runtime, so nothing here depends on it: when the driver
    has no multiview the fork draws two passes and `Capture::On3DColor` gets no right surface.
  - `XR_KHR_composition_layer_depth` is **no longer listed** (`7fbceeb` is in 1.0.0): BetterVR then keeps its depth
    in a local attachment and makes no depth swapchain, which saves six 16 MB x 3 images; `xrCreateInstance` still
    accepts the name.
  - view configuration: two views of 1984x2048, one sample (logged once); the Graphics pack's presets are scaled
    from it.
  - frame timing: BetterVR's frame pump calls `xrWaitFrame` for the next frame while the game still draws the
    current one. The Hmd2 frame is then still open (one at a time), so the wait returns the open frame's display
    time plus one period, and the next frame is begun by the wait that follows `xrEndFrame`. A
    `xrEndFrame` that finds no open Hmd2 frame (a repeated frame renders nothing, so it follows the begin at once)
    begins one itself and submits late (`late begins` in the frame log); only when BeginFrame gives none is the
    picture dropped and counted (`ends without a frame`).
  - the 0.9.23 stand-ins are gone (`BetterVR_PS5_PlayerOnce` and the other ten switches, the three proof counters,
    in `PlatformPS5.cpp`); the runtime composes with fixed behaviour: HUD quad as its plane, a mono picture across
    each eye, a re-shown pair keeps its render pose.
- **`patch_compiled.asm`**: the checked-in file is the pack's (CemuPatchCompiler is not found, so CMake says
  "using the checked-in patch_compiled.asm"). `tools/package.sh` fails when the packaged pack lacks it.
- **Foveation off**: patches/bettervr 0002 skips `UseFoveatedShading` on the PS5 (log line "Foveated shading is
  off") and defaults the `FoveatedRendering` setting to Off, so the menu does not offer it as on. The fork already
  draws without a shading-rate attachment when the feature is missing (`foveation_appliesTo`).
- **VR-failed fallback (issue #3)**: when the session or the headset fails while Cemu creates its renderer
  (`OnVkDeviceCreated`: `Init`, `InitSession`, so `xrCreateSession` with hmd2's init in it), 0002 logs, calls
  `BetterVR_PS5_VrFailed` and rethrows. The app returns to the library with the reason on Breath of the Wild's page
  ("VR did not start (hmd2 step, rc) ... Play on TV starts the game without the headset"), as in build20. The game
  does not go on flat in the same process: the packs are registered and the game's patches call the module's hooks.
  A failure before that (`OnInitialize`: OpenXR instance, system) leaves the module off and the game flat, with
  BetterVR's own error through the system notification and a `[bettervr] BetterVR is off for this run` line. Decision for
  the owner: a flat continuation in the same process would need the pack unregistered at that point.
- **Settings and resolution**: `BetterVR_settings.ini` and `BetterVR_log.txt` are in Cemu's data folder
  (`/data/botwvr/cemu`); the Graphics pack's presets are filled from the 1984x2048 eye size and start on BetterVR's
  own default, 1x (console run 2: 57 to 60 fps, the GPU never making the game wait; the earlier 0.5x override, about
  32 fps in the overworld, is gone); the other presets stay in Cemu's graphic pack list, and a choice saved there wins.
- **Logs** (`[vr-xr]` in the boot log; the module's `[bettervr]` lines before and after): `VR start 1/5` OpenXR
  instance, `2/5` session on Cemu's device, `3/5` headset up (ms after the session), `4/5` swapchain n (size,
  format, images, layers), `5/5` first frame submitted (ms after the session, with or without a stereo picture), and a
  line for the first frame with a stereo picture. A GPU timeout of the compositor logs `the compositor's GPU work has
  not finished` (counted). Every 600 submitted frames: the headset's fps over them, hmd2's pacing,
  waits one frame ahead, ends without a frame, render pose found / not found / kept, head and hand poses. The
  app's `[perf]` fps and GPU-busy lines are unchanged.
- Test request 48 in `STATUS.md`.
- **Review fixes** (review of c462dda..49d3744, three findings; built only on CI, untested on console):
  1. a swapchain or layer failure in BetterVR's lazy `EnsureLayers` (first capture event, Cemu's GPU thread) escaped
     `OnHostEvent` and would have ended the process: patches/bettervr 0003 catches it there, logs, calls
     `BetterVR_PS5_VrFailed` and ignores the later events (back to the library with the reason, as the other failures).
  2. one 2 s fence timeout set `gpuFailed` for good: the compose now has its own command buffer and fence and waits
     without `s.lock`; a timeout is logged and counted, the frame is retried on the next `xrEndFrame`, and 30 s after the
     first timeout the run ends through `BetterVR_PS5_VrFailed`.
  3. a fast `xrEndFrame` with no open Hmd2 frame dropped its picture: it begins the frame itself and submits late; the
     drop counter stays in the log next to `late begins`.

Open problems and risks (none seen on a console):
- **One queue.** The compositor's own wait no longer holds `s.lock`, which Cemu's GPU thread takes in the swapchain
  calls (so the wait could not cause its own timeout); a 2 s timeout is logged and counted, the frame stays open and the
  next `xrEndFrame` looks at the fence again, and 30 s after the first timeout the run ends in the library with the reason
  (`BetterVR_PS5_VrFailed`). A stall that only Cemu's later submit ends is still possible on the shared queue. If the graphics family has one queue, the module's queue is shared with Cemu's (log: `Creating
  Vulkan device with ... mod queue on family F index I (shared with Cemu)`). The module submits with timeline waits
  on Cemu's work; on one queue a wait that only a later submission satisfies would stall. `ps5xr::QueueMutex` orders
  the submits, not the waits. The first console log shows whether the run gets past the first frames.
- **Frame pump order.** The wait-ahead rule above is reasoned from the code, not seen: if frames repeat or the
  `ends without a frame` count is high, the Hmd2 frame is begun at the wrong call (the alternative: begin in
  `xrBeginFrame` and make the wait only nap).
- **Memory.** Colour swapchains: 3 x (2 eyes + HUD) at 16 MB for the 1984x2048 size, on top of the three 16 MB eye
  buffers, Cemu's two-view targets at the preset size and the module's layer textures. If the device runs out, the
  `vkAllocateMemory` failure is in the log and ends in the library with the reason (patches/bettervr 0003 catches it
  in the first capture event, where the swapchains are made on Cemu's GPU thread); `kSwapchainImages` can drop to 2.
- A preset below 1x (a player's choice) has a rules size under the eye size (the 0.5x preset is about 1280x1320 against
  1984x2048): BetterVR's present pipeline scales to the swapchain, which is the same as on the PC.
- `quadPlane` and `poseKeep` are on and `monoQuad` off in the runtime, as `State` had them as defaults; 0.9.23's mono
  flat-scene path (menus drawn mono) was a mod feature that 1.0.0 does not have in that form.

Backlog (outside stage 3): the runtime's pose ring (`located[]`) and kept-pose logic come from 0.9.23's mono/stereo
pair handling; 1.0.0 locates once a frame (`UpdateViews`) and re-submits `m_lastLayer3DViews`, so the ring may reduce to
"the head the last located views came from". Not simplified without a log of how often `render pose not found` counts.

CI: `BetterVR build` run 44266283541 on 49d3744 is green; the ELF has `VR start 5/5` and `Foveated shading is off`, and
the zip has `patch_compiled.asm`. The Tests run's Eden layout copy check fails on every branch of this fork. A code
review of that range found the three problems fixed above; the run for the fixes is read with
`gh run list --branch bettervr-1.0/stage3 -L 2`.
Next step: on failure `gh run view <id> --log-failed | tail -80` and fix the first error; on green check the ELF for
`VR start 5/5`, `Foveated shading is off` and `late begins`, then publish nothing and report. Stage 4 (Sense input, the 1.0.0 menu, haptics) follows Test request 47 and 48.

CI green on e8798cc (run 44272216600). A second review of the fixes (49d3744..e8798cc) found no high or
medium defects. Backlog (low): xrDestroySession from another thread during the compose's unlocked wait
would write freed memory (runtime.cpp, after the wait; needs a concurrent destroy); the comment saying
hmd2::begin_frame does not nap is wrong (it naps under `s.lock`); the frame counters also count discarded
attempts; the VR-failed reason can name a stale hmd2 error step.

## Stage 4 progress

Branch `bettervr-1.0/stage4` (from stage 3's head 36623bd). Built only on CI; every result is "built, untested on
console". Written blind.

Done:
- **Input through the runtime** (`port/bettervr/xr/runtime.cpp`, already in place from 0.9.23): read against 1.0.0's
  `controller_bindings.h` Touch profile (`/interaction_profiles/oculus/touch_controller`). Every path 1.0.0 suggests
  is in the runtime's table: grip/aim pose, `thumbstick` (move left, camera right, scroll/navigate in menus),
  `squeeze/value` (grab, tab switch), `trigger/value`, `a/b/x/y/click`, `thumbstick/click`, `output/haptic`. Sense
  mapping: Square = X, Triangle = Y, Cross = A, Circle = B, L1/R1 = squeeze, L2/R2 = trigger, L3/R3 = stick click.
  `xrSuggestInteractionProfileBindings` logs `Touch profile: N bindings onto the Sense controllers, M not mapped`:
  M should be 0. 1.0.0 binds the right Options (`menu/click`) only in the simple profile, so it has no action in
  the Touch profile (the 0.9.23 patches had added it).
- **Hand pose**: the runtime's grip/aim turn is unchanged (gripYaw 17, gripPitch 32, gripRoll 30, aimPitch 0, left +,
  right -). 1.0.0 applies no grip or aim offset of its own on top: its only fixed angles are the wrist turns in
  `hand_aim.cpp` and `link_skeleton.cpp` (90, -90, 30 degrees on Z, ...), which 0.9.23 had with the same values
  (`2e06b31:src/hooking/skeleton.cpp:690-697`), so the correction is not doubled and the result should equal
  build24-27's.
- **Menu**: patches/bettervr 0004. `ProcessModMenuInput` (`controls.cpp`) takes `BetterVR_PS5_OptionsDown()` (the
  runtime's, set at each `xrSyncActions` from the right Sense's Options bit, false when the hand is gone or the
  system has the pad) and toggles `ImGuiMenus::g_isOpen` on a press released within 1 s (a hold does nothing; no
  combination, no 3 s hold). Logs: `[menu] Options down`, `[menu] Options up after N ms`, `[menu] toggle -> open/closed`,
  `[menu] toggle ignored, the menu's overlay is not ready`. On by default: no `bettervr.json` switch. 1.0.0's own left
  Square long press stays. The first-run prompt names Options on the PS5. The overlay is 1.0.0's own: drawn into the
  HUD layer the runtime shows (`capture.cpp` `Draw3DLayerAsBackground`), its font upload under the queue lock from
  stage 2. Create on the left (the recording button) is not used.
- **Vibration**: 1.0.0's rumble calls `xrApplyHapticFeedback` (`rumble.h`), the runtime sets the Sense's vibration
  through `hmd2::sense_set_vibration_mode` (mode 2, at open) and `hmd2::sense_set_vibration`, the libScePad lookup
  living only in `third_party/hmd2/hmd2.cpp` (unchanged; Test request 45's result may still change it). New logs:
  the first six `haptic call n: hand, amplitude, duration, frequency` lines, and the level and result of the first
  three vibrations per hand and of every failure (at most 20).
- **Old patches** (decision table, 0.9.23 numbers):
  - 0008 (XR input without the ImGui overlay): dropped. In 1.0.0 `hook_InjectXRInput` does not depend on the overlay;
    `ImGuiMenus::ProcessInputs` returns when there is none.
  - 0011 (seated height moves the stage space): replaced by 1.0.0's own `UpdateSeatedHeightCalibration`, which moves
    the stage space already. Kept from it: Seated is the PS5 default (`PlayPosition`; the tracker's origin is not known
    to be the floor, and builds 24-27 ran Seated), and the first in-game frame asks for the calibration when the
    loading screen passed before the session existed (the PS5 starts the session after the boot load). Logs
    `Seated height calibration requested on the first in-game frame` and 1.0.0's `Seated height calibrated: eyes are
    X m above the floor`. A player's choice in the menu stands (the ini wins over the default).
  - 0005 (large input state through a mutex): done in stage 2 as the app's own `__atomic_load/store/exchange/
    compare_exchange` with a lock per address (`PlatformPS5.cpp`), which covers 1.0.0's `std::atomic<InputState>`.
- **Late physics (2026-11-05)**: 1.0.0 replays one simulation per game frame and latches one input sample per game
  frame (`m_frameInputLatched`, `Tracking::SampleHands`), where 0.9.23 ran the player update in two passes. This should
  remove the cause; only a console tells. The `[vr-xr]` frame lines (render pose found / not found, ends without a
  frame) and Test request 49 (a) show whether anything is still late.
- Test request 49 in `STATUS.md`.

Open problems and risks (none seen on a console):
- The runtime's Options bit comes from the pad's state word (`kOptions` 0x8); if a press does not register, the
  `Sense buttons 0x...` lines (first 40 changes) show which bit the right controller sends.
- `DetectActiveControllerType` runs at session creation, when the hands may not be open yet: the type is `Unknown`
  until the profile-changed event (`QueueProfileChanged`) makes BetterVR ask again; the prompt text and button glyphs
  follow it.
- Seated calibration needs a valid head pose; until it runs the stage origin is the tracker's.

Review pass and fixes (2026-11-18, built only on CI, untested on console):
- Review (high): the launcher wrote `bettervr_settings.ini` in the launcher's folder, 1.0.0 reads `BetterVR_settings.ini`
  from `HostModule::GetUserDataPath()` (= `ActiveSettings::GetUserDataPath()` = `ps5paths::CemuRoot()`, set in
  `emulator.cpp` `SetPaths`), so on the case-sensitive filesystem the launcher's choices were ignored. Fixed:
  `ps5paths::BetterVrSettings()` (`port/app/paths.h`) is the one path, used by the launcher page and by the app before
  the game's packs load, where it logs `BetterVR reads its settings from <path> (found / not there yet)`. A
  `bettervr_settings.ini` from 0.9.23 builds is moved there once (rename, or copy then remove across drives) when the new
  file does not exist; nothing is overwritten. 1.0.0's own overlay saves to the same file.
- Keys checked against 1.0.0's `ModSettings` (`mod_settings.h`): CameraMode, CutsceneCameraMode, PlayPosition,
  PlayerHeightOffset, TurnMode, WalkingDirection, SwingSensitivity, OneHitPerSwing, LeftHanded, UiFollowsGaze,
  HudDistance (0.5-2.5), HudSize (0.4-1.75), UseBlackBarsForCutscenes all exist with the same name, type and value names
  (the parser also takes the names case-insensitively and numbers). Nothing dropped or renamed, so no row removed.
  The Seated default is `PlayPosition`'s default in 1.0.0 (patch) and the page's own default when the key is absent, so it
  applies only while the player has not chosen.
- Vibration (low): `SetVibration` no longer calls libScePad when the level has not changed on that handle (1.0.0 repeats
  30 ms pulses); the remembered level is cleared when the handle is opened and after a failed call.
- 2 s vibration duration clamp: see the backlog.

Backlog (outside stage 4): headset vibration on damage; calibration UI; the 2 s clamp on a haptic duration (low): a
rumble that 1.0.0 starts with `XR_INFINITE_DURATION` or more than 2 s stops at 2 s while BetterVR thinks it still runs.

Next step: `gh run list --branch bettervr-1.0/stage4`; on failure `gh run view <id> --log-failed | tail -80` and fix the
first error (likeliest in `controls.cpp`: an include for `VRManager` / `RND_Renderer`); on green check the ELF for
`[menu] Options down` and `haptic call`, then report.

## Console run 1

Build `3e8c18d` (stage 4), 13.60 console, PS VR2 and both Sense controllers, Play in VR from the launcher. Reported from
the console: the PS5's own play-area (room) scan ran when VR mode turned on, and "PSVR crashed during room scan". The log
(`boot.log`, Cemu's `log.txt`) was symbolized with `tools/symbolize-crash.py` against that build's ELF (load offset
0x400000 from the handler at 0x1c06ac0).

**The crash (proven to the function).** Signal 11, fault address 0x60, error 0x6 (a write), RIP in
`proc_ui::AddCallbackInternal` (`proc_ui.cpp`), under `ProcUIRegisterCallback` called from the game (PPC IP 0x00e02b1c is
the HLE stub; r3 0 = AcquireForeground, r4 0x02e25964 the callback). `_AllocMem` returned 0, so `entry` was a null host
pointer, and `entry->funcPtr` sits at 0x60 (OSAlarm 0x58 and the 8-byte tickDelay before it). Nothing of BetterVR, OpenXR
or the port is on the stack: it is Cemu's own code answering the game's very first ProcUI call.

**Why the allocator gave 0 (not proven).** The log has no "ProcUI: Trying to register callback before init", so the
allocator is the default heap or ProcUI's memory pool; the log cannot tell which, nor how full it was. What is new in
1.0.0 and visible in the log: the bundled "Extended Memory" pack is on by default (`ec3b787`), `Using modified RAM
mapping 10000000-a0000000 (+1310720KiB)`, and its code patch group `BotW_ExtendedMemory_V208` is applied (main game
heap enlarged to 0x8B600000 from 0x3B600000, plus other heap sizes). Likely, not proven: the game's heaps of that pack
leave the default heap (about 2.2 GiB after the MEM2 extension) too little for the first 0x70-byte callback, or the pack
plus the PS5's memory differ from PC. The old 0.9.23 build did not map extended memory. Fix in this change: Cemu patch
0052 leaves the callback out and logs one line instead of crashing: `ProcUI: no memory for a callback (allocator, memory
pool, initialized, default heap free, MEM2 end)`. The game then goes on without that foreground callback (it is the
Home-menu handling; a guest crash later would still show in the PPC dump). Extended Memory is now off (below).

**Extended Memory (answered from the code).**
1. It is the bundled pack `Mods/ExtendedMemory` (`rules.txt`: `default = true`, so Cemu enables it unless a saved choice in
   settings.xml says otherwise; no Cemu setting, game profile or BetterVR module code switches it). Its RAM section widens
   MEM2 to 10000000-a0000000 and the overlay area to a0000000-e0000000; `BotW_ExtendedMemory_V208` raises the main game heap
   0x3B600000 to 0x8B600000 and grows the save-area, trigger-param, Havok and physics heaps, terrain and forest buffers. Its
   own text says "only enable this if a mod you want to use requires this". BetterVR 1.0.0 does not require it: nothing in
   its source or its pack mentions it; only DrawDistance's "Extreme (2x)" and "Ultra (1.5x)" presets say "requires Extended
   Memory". Optional (proven by the code).
2. Same backing (proven): `MemMapperPS5.cpp` commits every range of the guest reservation through `MapDirect` (direct
   memory, 256 MiB chunks); there is no separate path for 0x50000000 and up. The "flexible memory free 394 MiB" in the log is
   `sceKernelAvailableFlexibleMemorySize`, which this backing does not use; the same line (taken at 11.0 s, before the game
   loaded) says largest free direct block 9970 MiB, 1471 MiB committed. The extension adds about 1.8 GiB, and a failed commit
   would have logged `[memmap] out of direct memory` and a toast: neither is in the log. So the backing is unlikely to be why
   the allocator gave 0; the enlarged game heaps are the likelier cause (not proven).
3. Decision: off by default. Patch bettervr 0005 sets `default = false` in the pack's rules.txt, so the guest memory
   layout is 0.9.23's again; the player can still turn it on in Graphic packs (a saved choice wins). Logged as one line at
   start: `Extended Memory pack: off unless turned on in Graphic packs (default = false)` (`VrPS5.cpp`). A settings.xml that
   already holds an entry for the pack from an earlier build keeps its choice. Patch cemu 0052 and its log line stay.

**0 draws and 0 game frames for about 30 s (proven: not BetterVR, not frame pacing).** Cemu starts the guest only after
the GPU thread has loaded the shader cache (`CafeSystem` waits for `g_isGPUInitFinished`, set after
`LatteShaderCache_Load`). The log shows exactly that: "Run title" at 13:26:21, perf lines with 0 draws and the pipeline
cache at 280 compiled, 336 read of 2081 at 31 s, the OSSched threads and `IOSU_ACT` (the first guest code) at 41.3 s /
13:26:50, and the crash in the same second. The guest clock ratio 1.000 with 0.0 game frames is a game that had not started,
so there was no draw to wait on `xrWaitFrame`, the shared queue or the capture events, and the crash is at the first
frame's start, before any swapchain. VR start 4/5 (the swapchain of the first frame, `runtime.cpp`) was never reached for
that reason. What the log does not show: how long the cache load takes with no scan running, and whether the system's
play-area scan (which owns the headset display and input while it runs) slowed it; no XR call is made in that period
(session 3/5 was up, no frame loop yet). The crash does not follow the scan ending: it follows the end of the cache load,
and its cause (an allocation) has no path to the session state. One log line for the next run: none added, the existing
`[perf]` pipeline-cache counts and the time of `OSSched` against `Run title` tell the cache load's length.

**libScePad anchor (vibration).** The check reads the whole expected string, `scePadCtrlpSyncInit Error : cannot open
[%08x]` and its NUL, from +0x1f650 (inside segment 1, +0x1c000 size 0x4000, readable) and compares it with `memcmp`; the
in-process scan walks every readable segment the same way and found 0 copies. The code is consistent: the first 8 bytes
are "scePadCt", so the text there starts like the anchor and differs further on (or the firmware's string is not the one in
`hmd2.cpp`; the firmware is 13.590.001, the table says 13.60). It cannot be told from the 8 logged bytes, so the check
stays as it is (a prefix match would pick unverified offsets) and the log now prints up to 64 bytes at the anchor, as text
and hex, with how many leading bytes match. Execute-only code stays unread. The Sense vibration stays off until that line
shows the real string; the Sense's mode calls still answered 0x80000001.

Next console run must show: no `modified RAM mapping` and no `BotW_ExtendedMemory_V208` in Cemu's log (the line above says the pack is off), the game starting past ProcUI, `[perf]` lines and the time to `OSSched`, the `ProcUI: no memory for a callback` line if it
fires (its numbers decide the heap question), `VR start 4/5` and `5/5`, and the `at anchor +0x1f650` line.

## Console run 3

**Launcher's community graphic packs page empty (proven in code, the console listing agrees).** The packs are on the
console: `graphicPacks/downloadedGraphicPacks/` holds `version.txt` and the full tree (`BreathOfTheWild/Enhancements`,
`Graphics`, `Mods`, ... and the other games'). The Cemu base moved to Crementif's fork, whose commit 6f5f3fe9 ("Run beside
a regular Cemu install") makes `GraphicPack2::LoadAll` skip `graphicPacks/downloadedGraphicPacks` entirely ("the community
packs ship with this build instead", `GraphicPack2.cpp`). The "build" there is BetterVR's bundle (`assets/bettervr/BetterVR/
packs/graphicPacks`), which a host module registers only when `StartBetterVR` runs (`app/emulator.cpp`, Play in VR). At
the launcher (`LibraryInit` -> `LoadAll`) and after the Settings > Online download (`ReloadGraphicPacks` -> `LoadAll`) no
module is registered, so no pack is loaded and `PacksOf` finds none. Settings.xml entries stay unused for the same reason.

Fix (patch cemu 0053): `LoadAll` skips only `downloadedGraphicPacks/BreathOfTheWild`, and only once a host module has
registered bundled pack directories (`GetBundledGraphicPackDirectories()` not empty). At the launcher every downloaded pack
lists as in 0.9.23; in VR, after `StartBetterVR` reloads, BotW's community packs come from BetterVR's bundle only, so none
is listed or activated twice. Other games' downloaded packs always load. Log lines (confirm on the console):
`[packs] N graphic packs loaded at start` (library) and `[packs] N graphic packs loaded with BetterVR's` (VR start); the
existing `[packs] N graphic packs loaded` follows a download. Built, untested on console.

## Console run 3: scene corruption

Reported from the console (build 50143ed, 1x = 2000x2040 per eye, 50 fps): after the inventory closes, a corrupted 3D
scene (screen-aligned patches of clipped colour and black blocks, the HUD clean), worse after each open and close, not
growing over time on its own. Run 2 (0.5x) reported none; whether the inventory was opened there is unknown.

**Cause, ranked.**
1. Proven in code and matching the log (not seen on console): patch cemu 0015's image-copy clamp counted a texture's
   layers as `depth - slice`. In the Crementif fork a 2D render target created for stereo has a view layer
   (`hasViewLayer`, `LatteTextureVk.cpp`: `arrayLayers = depth * 2`) and the right view's slices follow the guest's
   (`LatteView_GetSiblingSlice` = slice + depth). `VulkanRenderer::texture_copyImageSubData` calls itself for the right
   view (`LatteView_OnTextureCopy`) with that slice, so the clamp computed 0 layers and skipped the copy. The boot log
   shows exactly this, 107 `[copy] image clamped` lines in run 3, e.g. `VkFormat 122 mip 0 -> mip 0, 800x725 x1 layers
   asked, 800x725 x0 copied (nothing: skipped)`, with width and height unchanged, each followed by the left view's
   `[copy] image:` line of the same size. The inventory copies the scene behind it (and the blur and bloom textures it
   uses) with GX2 copies; every one left the right view's slice of the destination stale (the image's earlier content,
   or never written), so each open and close adds more stale texture data, as reported. Run 2 has the same lines. Not
   proven: that this is the whole symptom (the TV mirror shows one view; which eye it is was not checked), and that the
   0.5x run did not show it only because the inventory was not opened.
2. Likely not: the capture and release path (`capture.cpp`: mono capture during the pause menu, no clear in mono), texture
   redefine rules at 2000x2040 (the 640x368 rule gives 1000x1043 against 1000x1020 at 640x360; the same rounding exists
   at 0.5x), the A2B10G10R10 format (UNORM cannot hold NaN), upstream: Crementif/BotW-BetterVR #26 and #144 describe a
   black screen after menus or the map on PC (a different symptom, open, no cause given), none match coloured patches.

**Fix (patch cemu 0054):** the clamp counts `depth * (hasViewLayer ? 2 : 1) - slice` layers. Built, untested on console.
The log's `[copy] image clamped ... x0 copied` lines for same-size images should be gone; Test request 51 asks for that
and for the eye.

## v0.6

Branch `bettervr-1.0/v06-all`, from v0.5.0, combining the v0.6 tracks (`bettervr-1.0/v06`, `v06-perf`, `v06-vibration`,
`v06-triggers`, `v06-eye`). Built only on CI; every result is "built, untested on console". Reported from the console
on v0.5.0: the short press of Options was too easy to hit when the player meant to pause, the popup at game start was
unwanted, Link flashed while strafing near grass, and the weapon wheel lacked its left and right arrows. New patch
files: bettervr 0007 (performance), 0008 (vibration), 0009 (adaptive triggers), 0010 (eye tracking), and cemu 0055
(the GPU thread's swap and VR frame end for the profile). Bettervr 0001-0006 are v06's, unchanged; 0055 is the only
Cemu patch added. `VERSION` stays 0.5.0.

### Options on the Sense and the DualSense, and the start popup

- **The menu takes a hold.** Options on the right Sense held for 700 ms opens BetterVR's menu when the threshold is
  reached, not on the release; holding it again for 700 ms closes it, and the release that ends either hold does
  nothing. If a stall kept every game frame from seeing the threshold, the release compares the measured time itself:
  700 ms or more toggles the menu and sends nothing, so a hold is not lost to a hitch. The threshold is one constant
  (`kLongPress` in `ProcessOptionsButton`, `controls.cpp`, patch bettervr 0004).
- **A short press is the game's pause.** Released before 700 ms, Options is the Wii U "+" that opens the inventory and
  pause screens: one tap on the frame of the release, always `VPAD_BUTTON_PLUS`. The earlier build sent
  `VPAD_BUTTON_MINUS` while `state.mapOpen` said the map was open, as the right stick click is read in the menus, but
  that flag is only written by the stick click: closing the map with B left it set, so the next Options tap opened the
  map instead of the inventory. The tap now clears `state.mapOpen` as well, so the flag cannot stay stale from this
  path. The tap reaches the game only while its input is open: with BetterVR's menu over the game, with the overlay's
  keyboard block (`ImGuiMenus::BlocksGameInput`), or during `state.preventInputs` (the 400 ms after a menu change), the
  release is logged and nothing is sent, so no pause screen opens behind the overlay. A hold sends nothing (no pause),
  a tap opens no menu, and the "+" stays on the right stick click; the stick click still picks `VPAD_BUTTON_MINUS` in
  the map through `state.mapOpen`.
- **The pad's Options is the same button.** The DualSense's Options is VPAD channel 0's +, and it now feeds the same
  `ProcessOptionsButton`: held for 700 ms it opens or closes BetterVR's menu at that moment and the release then does
  nothing, a shorter release sends the game one + tap, and + is removed from the game's hold while the button is down,
  so no pause opens behind the menu; upstream's 500 ms `ToggleModMenuOnLongStartPress` no longer runs on the PS5, and
  the port's own touchpad+Options overlay is unchanged.
- **The pad moves the menu cursor.** When no Sense controller is connected, the VPAD's left and right stick are added
  to the mod menu's navigation, so a DualSense-only player can move through the pages and rows with either stick; the
  Sense path is unchanged (upstream issue #117).
- **No popup at start.** `DrawTutorialPromptAndControllerVerification` returns at once on the PS5, so the first-time
  setup card over the game is gone; the PS5 prompt text that patch 0004 added to `GetControllerModMenuPrompt` goes with
  it, as nothing draws it now.
- **Logs** (BetterVR's, mirrored to the boot log as `[bettervr] ...`): `[menu] Options down`, `[menu] Options up after
  N ms`, `[menu] long press -> open` or `closed` (or `[menu] long press ignored, the menu's overlay is not ready`),
  `[menu] release after the threshold -> hold` when the release itself ends a hold, `[menu] short press -> pause`, and
  `[menu] short press ignored, game input is blocked` for a tap the overlay or `state.preventInputs` keeps from the
  game.

### First person through a camera stall

- **The one-frame body flash is gone.** `IsInGame()` is false while the gameplay camera has not updated for five
  presented frames, a texture-streaming hitch at 50-60 fps, and the pose switched to the third-person path for that
  frame: Link's head materials were shown and the controller arm pose was skipped. `skeleton.cpp` now tolerates a camera
  stall shorter than 250 ms and keeps the first-person pose; the pause menu, a cutscene's event setting and the
  third-person camera setting still switch at once. If the actor list misses the player for a frame, the head is still
  hidden by the last decision, and the root bone starts the frame's material list so it keeps or drops the last one.
  The list is kept for the 250 ms stall window only, and only while the units still belong to the live player: the
  actor at the remembered address must still name itself `GameROMPlayer`, still hold the model the units were
  collected from, and the game must still be animating that model. Otherwise the list is dropped, so a load, a
  teleport or the player's death cannot hand a freed model unit to `setMaterialVisible`; every unit that is used is
  also checked live. Log line (at most once a second): `[pose] a short camera stall kept first person (N frames)`, N
  the frames the tolerance kept since the run started. Patch bettervr 0006.

### HUD alpha, shimmer and the compose

- **The HUD alpha is no longer squared.** The runtime compositor blended the premultiplied HUD (ImGui writes `rgb*a,
  a`) as straight alpha at `compose.comp:59`, so semi-transparent UI became `rgb*a²`: the weapon wheel's dimmed white
  arrows vanished on the black buttons, and the minimap and clock looked faded. The compositor has one blend for the
  HUD and the menu quad, and it now blends premultiplied, `color.rgb = c.rgb + color.rgb * (1.0 - c.a)`;
  `port/bettervr/xr/shaders.h` is regenerated with `tools/render-shaders.sh` and committed.
- **HUD mip chain and LOD-aware sampling** (`port/bettervr/xr/runtime.cpp`, `compose.comp`). Each frame the HUD image
  is copied into a full mip chain (about 5.4 Mpx of copy and linear blits) and the compose samples it with
  `textureGrad`, the gradients taken analytically from the quad's homography. The ~2.4x minification now picks a level
  instead of one bilinear tap at level 0. The sampler is mipmap-linear; the eye images still use LOD 0. When the HUD
  format has no linear blit the old single tap stays; the log says `HUD mip chain ...` only when the chain was made.
- **Compose without the eye pow chain.** The eye swapchain images get a UNORM view on the same image
  (`VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`), so the compose reads the bytes as written; only the pixels under the HUD
  decode and encode again, to blend it in linear light. `toSrgb` no longer runs for all 8.13 Mpx.
- **The ImGui round trip is skipped when it is only the HUD.** With the menu, prompts, performance overlay and weapon
  debugger all absent and a 3D picture behind it, the HUD image is already the 2D layer: no copy into
  `hudFramebuffer`, no clear/draw/copy back. The desktop ImGui pass (Cemu's window) is not run on the PS5 at all;
  Hmd2's social screen is the TV. `hudWithoutAlphaFramebuffer` is now a second view of the same HUD image with alpha
  forced to one, so the HUD is copied once, not twice.
- **The depth copy is skipped.** The runtime does not offer `XR_KHR_composition_layer_depth`, so Hmd2 never reads a
  depth image; `Capture::On3DDepth` still accepts the depth capture (the frame must count as complete) but makes no
  `CopyDepthToLayer`. The present pass still writes `gl_FragDepth` into its local attachment, which nothing reads.

### Performance: packets, profile and VR Resolution

- **Accurate barriers off by default, as on a PC VR run.** The app's `StartBetterVR` (`port/app/emulator.cpp`) sets
  Cemu's `vk_accurate_barriers` false for the run (the PS5 app keeps Cemu's settings itself, so `CemuSettings::Enforce`
  does not run). `bettervr.json` `"accurateBarriers": true` (or `1`) puts them back for a comparison; the boot log has
  `[bettervr] Accurate barriers: off/on`, and the app's `[perf]` line keeps saying `accurate barriers off/on`.
- **Frame profile.** `bettervr.json` `"profile": true` turns on Cemu's GPU timestamps and one `[frame]` line every
  10 s: `swap X ms | VR end Y ms | compose fence Z ms | hmd2 submit W ms | fps | GPU busy N%`. `swap` and `VR end`
  come from Cemu patch 0055 (ticks around `SwapBuffers` and `NotifyFrameSubmitted`); the fence and hmd2 submit are
  timed in `xrEndFrame`. Off by default: one bool test a frame, nothing else. After Cemu loads the packs,
  `BetterVR_PS5_LogPresets` logs one `Graphics pack presets:` line with the active VR resolution, quality, AA,
  recording and shadow presets (their names carry the width and height).
- **VR Resolution in BetterVR's menu.** On the PS5 the System page's "Graphic Packs: Open On Desktop" row (there is no
  window) is replaced by `VR Resolution`: 0.5x, 0.75x, 1x, 1.25x, 1.5x from the pack's "VR Resolution Multiplier"
  presets. It saves through Cemu's preset API and logs `VR resolution preset: ...`; the pack has texture rules, so
  `GraphicPack2::RequiresRestart` is true and the existing restart notice says it applies at the next start. The pack's
  default stays 1x.
- **CI zlib.** `tools/deps.json` now puts zlib 1.3.2's archive where the boilerplate's setup expects it, from the
  GitHub release (same SHA-256 as zlib.net's); `tools/deps.py` verifies file items by hash instead of their name, so a
  short or corrupt download is fetched again instead of used.

### Vibration: one page and one mixer

- **One Vibration page, nine categories.** BetterVR's own menu has a page between Controls and Display
  (`rendering/imgui_menus.cpp`, `VIBRATION_PAGE`), with a master switch, a headset cap, a test button for each device
  and a row per category: Weapon Swings, Bow, Shield & Guard, Gear & Inventory, Attacks/Items/Throws, Runes, Game
  Rumble, Damage Taken, Explosions. Each row has on/off, strength 0-100 % and a tick per device (Sense controllers,
  PS VR2 headset, DualSense). Changes are live and save to `BetterVR_settings.ini` with the rest.
- **Defaults reproduce v0.5.0**: every existing category on at 100 % on the Sense controllers, the game's pattern on
  both hands at full strength. Damage Taken and Explosions are new, so they start on at 100 % on the hands and the
  headset. Keys: `Haptics`, `HapticsHeadsetLimit` (60), `HapticsLogPatterns` (true), and per category
  `Haptics{Category}`, `Haptics{Category}Strength`, `Haptics{Category}Devices` (bits: Sense 1, headset 2, pad 4). Old
  settings files get the defaults; an older build ignores the new keys, so a downgrade is safe.
- **A 60 Hz mixer in `RumbleManager`** (`rendering/rumble.h`) replaces the two independent XR calls. The game's pattern
  is a level per tick, each input rumble an enveloped level on its hand, and every device takes the louder of the
  categories switched on for it. The game's pattern and an input rumble no longer cut each other: they add by
  maximum. A game pause stops the input envelopes (100 ms), so nothing stays on.
- **The PS VR2 headset's motor** is bound in `third_party/hmd2` as an optional export: `sceHmd2SetVibration(handle,
  level)`, 0-25, one motor. `sceKernelDlsym` first; otherwise the 13.60 table's `+0x2ae00` only behind libSceHmd2's
  anchor check. Other firmware leaves it unbound (one `hmd2:` line) and VR continues. The runtime sends on change, caps
  a continuous hold at 2 s, maps 0..1 to 0..25 and logs the first calls and every failure. It is not called with a zero
  level at VR start: the page's Headset test is its first call, so an unproven offset cannot crash a session that never
  uses the motor. The page's Headset button logs `[vib] test headset: level 25 -> 0x...`.
- **The DualSense is taken over while VR runs.** `port/cemu/PS5PadController.cpp` gains `PS5Pad_SetExternalRumble`
  (Cemu's own `start_rumble`/`stop_rumble` return at once, and both motors are cleared when the mode changes) and
  `PS5Pad_ExternalRumble` (the mixer's level; the launcher's Vibration row stays the outer switch, and Cemu's own
  per-controller rumble percentage is not applied while VR drives the pad). `port/bettervr/xr/runtime.cpp` turns it on
  at `xrBeginSession` and off at `xrDestroySession` and `BetterVR_PS5_Stop`; one `[pad] rumble:` line says which side
  drives the pad. No Cemu patch was needed.
- **Damage taken** uses the existing `PlayerNormal` damage-state hook (`player.cpp`): one falling rumble on both hands,
  once per state change (300 ms apart at most), plus `[vib] damage state '...'` for the first six.
- **The game's rumble stays one category for now, with a learning log.** At the `VPADControlMotor`/`StopMotor` hooks
  (`controls.cpp`, at most 40 lines 150 ms apart, `HapticsLogPatterns` turns it off) it prints the caller LR, the
  playing state, the VPAD pattern bytes, and either the sead `PatternRumbleAddon`'s string at `+0x14` and its repeat
  count at `+0x1C` when the addon is in a callee-saved register or on the stack, or `pattern="?"`.
- **Not done: explosions.** They are a category with their settings, but no event source yet.

### Adaptive triggers

- **What the effects are.** R2 (the bow) uses slope mode 5: from zone 0 at strength 1 to zone 9 at a strength that
  grows with the game's own `drawAmount` (weapon `+0x94C`, 0-1.5 clamped to 1), 1-8 across the strength setting. A held
  trigger therefore firms up as the bowstring comes back, and the effect stays at the end strength above zone 9. Off
  below 2 % draw. L2 (the shield) is feedback mode 1, position 2, strength 3-5 across the setting, while
  `state.isShieldGuarding`; off otherwise. Both are off when BetterVR's menu is open, on the pause and inventory
  screens (`GameState::IsShowingMenu()`), and outside the game. The effect is sent on change, at most 20 times a
  second; an off pair goes out at once.
- **BetterVR's side** (patch bettervr 0009): `Bow::GetDrawAmount()` (bow.cpp/bow.h); one call each input frame in
  `hook_InjectXRInput` (controls.cpp), and an off call when the input sample is not ready; the PS5-only `ModSettings`
  rows `AdaptiveTriggers` (default true) and `AdaptiveTriggerStrength` (0-1, default 1), which save to
  `BetterVR_settings.ini` like every other row; and a System > **Adaptive Triggers** card with the toggle and the
  strength slider, shown while the toggle is on.
- **The DualSense** (default on): `scePadSetTriggerEffect`, NID `2JgFB2n9oUM`, looked up by name in libScePad and
  called on the handle the app's pad service holds for player 1 (`PS5Cemu_PadHandle`). The parameter is the 0x78-byte
  `ScePadTriggerEffectParam`: mask 0x3 (L2 and R2), the L2 command at +0x08, R2 at +0x40, each a u32 mode, four
  padding bytes and 0x30 data bytes. The layout and the builders are `port/bettervr/xr/triggers.h`, with a host test
  (`tools/tests/test-bettervr-triggers.sh`, run by the Tests workflow).
- **The Sense controllers** (the experiment, `bettervr.json` `"senseTriggers": true`, default off):
  `scePadVrControllerSetTriggerEffect(s)`, NIDs `6Cdc9bbjrRY`/`v8P+9PRqg10`, bound by name first and otherwise at
  the 13.60 table's `0xa730` (both hands) behind the existing libScePad anchor. The VR payload is unpublished, so the
  first effect tries the DualSense 0x78 layout on the both-hands call, and if that call fails one 0x38-byte command a
  hand; the accepted one is used from then on. `"senseTriggerLayout": 1` or `2` forces one of the two for the next
  run. `BetterVR_PS5_Setting` now also reads a JSON boolean, so `true` works.
- **Always off on the way out:** `xrDestroySession`, `BetterVR_PS5_Stop` and the failed-start path all send the off
  pair before the pad handles go; the effect lives in the controller's firmware and would otherwise outlive the game.
- **Logs** (the boot log, each line starts `[trig]`): `Sense trigger effects on/off ...`, the binding lines from
  hmd2 (`DualSense trigger effects bound`, `Sense trigger effects bound`), one line per effect call with both
  commands and libScePad's answer (`DualSense (handle N) L2 feedback 2 5, R2 slope 0-9 1-6 -> 0x00000000`), the
  Sense layout experiment's results and the chosen payload, and `DualSense off (...)` / `Sense off (...)`.

### Eye tracking and the throw assist

- **Gaze probe in hmd2.** `sceHmd2GazeGetResult`, `...ForFoveatedRendering`, `ActivateGazeTracking`,
  `GetDeviceStatus` and `GetUserCalibrationStatus` are optional slots resolved with `sceKernelDlsym` only. No gaze
  offset is added to `hmd2_exports.h`; a library that does not export a name logs which one and leaves eye tracking
  off. After `sceHmd2Open` the probe calls `ActivateGazeTracking(1)`, then logs the device status and the user
  calibration status. Status 0 means detected/calibrated; any other value is logged raw. A device status other than 0
  says in the log that eye tracking is off in the PS5 settings (Settings > Accessories > PS VR2 > Eye Tracking) or
  that the headset does not see the eyes; a calibration status other than 0 says the user is not calibrated and names
  Adjust Eye Tracking. The status is re-checked every 2 s, so putting the headset on or changing the setting after VR
  starts is seen.
- **The first 10 s.** At most once a second, the probe logs the full `0x38` result and the `0x18` foveated result as
  hex, then the parsed raw direction and both status fields. Every later status change is logged once (at most 20
  lines). A sample is exposed only when the device and calibration checks pass, both calls returned 0, the foveated
  (or, failing that, the full) result's status is 0 and its direction is finite and non-zero.
- **OpenXR exposure.** The runtime offers `XR_EXT_eye_gaze_interaction`, accepts it in `xrCreateInstance`, fills
  `XrSystemEyeGazeInteractionPropertiesEXT` when the application chains it, keeps the
  `/interaction_profiles/ext/eye_gaze_interaction` profile's `/user/eyes_ext/input/gaze_ext/pose` binding, and marks
  that action's space as the gaze space. `xrLocateSpace` returns an orientation-only pose (valid + tracked) while the
  probe has a valid sample, and an untracked pose otherwise; `xrGetActionStatePose` reports the action active only
  then. `xrGetSystemProperties` runs before `xrCreateSession`, so the property is advertised optimistically: only the
  session's `hmd2::init` can ask the headset, and the pose is what is gated.
- **Direction frame.** The firmware gives the foveated direction headset-relative with +z forward; the runtime applies
  PSVR2Toolkit's `{-x, y, -z}` conversion and rotates it by the frame's head pose into stage space. The first console
  run's hex and raw direction are what confirm the sign.
- **Throw assist.** Patch bettervr 0010. When the new setting is on, a released throw takes the point 10 m along the
  gaze ray from the middle of the eyes (head forward when no gaze sample exists), and bends the hand's world velocity
  toward the line from the hand to that point by at most 15 degrees; the speed is unchanged, and the game's own 1.2x
  cap still applies. The setting is `Gaze Throw Assist` in Controls > Motion Controls, default off. Log line:
  `[Throw] gaze assist bent N degrees toward the gaze` (or `head`). With eye tracking off or uncalibrated the
  fallback is the head direction, so the setting still behaves predictably.
- **Backlog, not this build:** item highlight and bow aim assist.

### Touchpad click

- **The Slate on a press.** A player's DualSense touchpad click is `- (Select)` by default on the GamePad and the Pro
  Controller (`touchpadClick` is 2, "Presses a button"); Create presses it too, a finger still moves the GamePad's
  cursor, and touchpad + Options is still only the port's menu. An older `ps5cemu.json`'s 0 (the old automatic default)
  becomes 2 once (a marker, `touchpadClickMigrated`, keeps a later choice of Automatic); 1 and 2 are kept.

### Settings and About

- **Settings are BotW VR's alone.** The PayPal card is gone from About, which now has one licence line (Cemu and PS5-ECAM under the GPL, Crementif's BetterVR under MIT) and only the Wii U's paths; the backup, box art, recompiler and remote-screen texts no longer name other systems, and what is left is marked "ask" in the track's settings audit.

### Open

Nothing here has run on a console. The mip chain's cost and the skipped passes need the `[frame]` line; the HUD
quads' edges need a headset. The optional FSR/upscale for presets below 1x is not done; the compose still samples
BetterVR's already-upscaled eye image. The depth write in the present shader is not removed. The Sense trigger payload
and its return-code meaning are unverified, and the DualSense's pad handle is opened in vibration mode 2. The headset's
`+0x2ae00` is from public tables, not our own dump; its test button is the proof. Explosions have settings but no
event source. The five gaze names, the meaning of status 0 and the direction's sign wait for the console.

Test request 53 in `STATUS.md`.
