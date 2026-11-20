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
- **Menu**: patches/bettervr 0004, reworked by 0020. Options no longer opens it: a short press is the game's pause
  and a hold is the runtime's recenter. BetterVR's menu opens on 1.0.0's own left Square long press (unchanged) or
  on a DualSense touchpad + Options tap (`port/ps5/pad.cpp`'s chord, `BetterVR_PS5_ToggleModMenu`); the chord held
  for a second is the app's own in-game menu. On by default: no `bettervr.json` switch.
  The first-run prompt names the PS5's own inputs. The overlay is 1.0.0's own: drawn into the
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
unwanted, Link flashed while strafing near grass, and the weapon wheel lacked its left and right arrows. The console
run of build 93ac3d1 (13.60) then reported blurrier menu text, no damage rumble, gaze offered without the exports, and
the Sense trigger switch buried in `bettervr.json`; the fixes are in the sections below and in "Firmware gates".
New patch
files: bettervr 0007 (performance), 0008 (vibration), 0009 (adaptive triggers), 0010 (eye tracking), 0011
(body, HUD and the blue needle), 0012 (third-person snap turn), 0013 (magnesis wrist twist) and 0014 (the
DualSense trigger row), 0020 (Options: the view's recenter, and the menu on Square or a touchpad tap); cemu 0055 (the
GPU thread's swap and VR frame end for the profile). Bettervr 0001-0006 are
v06's, unchanged; 0055 is the only Cemu patch added. `VERSION` stays 0.5.0.

### Options, the view's recenter, and the start popup

- **Recenter: Options held for 1 s.** Holding Options for about a second on the right Sense or on the DualSense
  re-seats the view as a PS VR2 game does: the head's forward becomes the stage's -z and its horizontal position the
  origin, and the head and both hands follow in VIEW, LOCAL and STAGE, so BetterVR and the game need no change. The
  height stays: BetterVR's stage offset is a pure y shift for the seated eye height, so its seated calibration is not
  disturbed. The seat is one pose in the runtime (`Recenter`, `port/bettervr/xr/runtime.cpp`), latched from the head's
  own pose at the display time of the frame the hold reaches its second in (`xrWaitFrame` applies the request, before
  the app locates that frame's eyes), and reset to identity at each session. Hmd2 still reprojects each frame from the
  tracker's own head pose (`Located::trackerHead`), not the seat: the seat is the game's frame, the room stays the
  headset's. It fires once a hold and its release does nothing. A full-screen 2D panel turns with the seat: BetterVR's
  `Layer2D` reads the runtime's seat counter (`BetterVR_PS5_RecenterGeneration`) and lays the panel out again from the
  new frame's views, so it is straight ahead the instant the view recenters, at any head pitch and roll (patch bettervr
  0030). One line a recenter:
  `[recenter] right Sense: head yaw -12.3 -> 0.0 deg, the 2D panel is 0.0 deg right of it; the seat moved 0.15 -0.42 m (the head was at ...)`.
- **A short press is the game's pause.** Released before 1 s, Options is the Wii U "+" that opens the inventory and
  pause screens: one tap on the frame of the release, always `VPAD_BUTTON_PLUS`. The earlier build sent
  `VPAD_BUTTON_MINUS` while `state.mapOpen` said the map was open, as the right stick click is read in the menus, but
  that flag is only written by the stick click: closing the map with B left it set, so the next Options tap opened the
  map instead of the inventory. The tap now clears `state.mapOpen` as well, so the flag cannot stay stale from this
  path. The tap reaches the game unless BetterVR's menu is over it or the overlay blocks the keyboard
  (`ImGuiMenus::BlocksGameInput`); then the release is logged and nothing is sent, so no pause screen opens behind
  the overlay. The game's own 400 ms input block after a menu change no longer drops it: a tap within that window
  reaches the game as the physical "+" did. A hold sends nothing (no pause), a tap opens no menu, and the "+" stays on
  the right stick click; the stick click still picks `VPAD_BUTTON_MINUS` in the map through `state.mapOpen`.
- **The menu no longer takes Options.** The hold is the recenter now, so BetterVR's menu opens on the left Sense's
  Square (a hold, unchanged) or on a DualSense touchpad + Options **tap**. The chord held for about a second is the
  app's own in-game menu, as before. The app's pad reader (`port/ps5/pad.cpp`) holds the chord for the second
  (`kMenuHoldUs`) and reports `Shortcut::Menu` or `Shortcut::ModMenu`; the app calls `BetterVR_PS5_ToggleModMenu`
  (`controls.cpp`, patch bettervr 0020) on the tap, and opens its own menu where no VR session runs. The runtime
  ignores Options while the touchpad is clicked, so the chord never recenters. The threshold is one clock, in the
  runtime's `PollOptions`: `ProcessOptionsButton` reads that decision, so a press is a recenter or a pause, never
  both.
- **The pad's Options is the same button.** The DualSense's Options is VPAD channel 0's +, and it feeds the same
  `ProcessOptionsButton`: a hold reaches the runtime's recenter and the release does nothing, a shorter release sends
  the game one + tap, and + is removed from the game's hold while the button is down, so a hold never pauses;
  upstream's 500 ms `ToggleModMenuOnLongStartPress` no longer runs on the PS5. The Sense's Options and the pad's +
  keep their own state (two instances of the same machine), so one does not swallow the other's press: a tap of the
  one still reaches the game while the other is held. Once BetterVR's menu is up, a tap from the other is dropped on
  purpose, with `[menu] short press ignored, game input is blocked`.
- **The pad moves the menu cursor.** When no Sense controller is connected (the runtime's connection state, not
  whether a hand has a pose), the VPAD's left and right stick are added to the mod menu's navigation, so a
  DualSense-only player can move through the pages and rows with either stick; the Sense path is unchanged (upstream
  issue #117).
- **No popup at start.** `DrawTutorialPromptAndControllerVerification` returns at once on the PS5, so the first-time
  setup card over the game is gone; the PS5 prompt text that patch 0004 added to `GetControllerModMenuPrompt` goes with
  it, as nothing draws it now.
- **No system notification window at start either.** The PS5's notification window comes up over the screen with the
  console's earlier notifications listed behind it, and in the handoff from the launcher to a game (about 0.85 s between
  `[ui] gfx: stopped` and `[vulkan] VideoOut surface`) nothing of the app's is drawn: a notification posted then brings
  that window up on a black screen, jailbreak messages and all. Two changes: the Wii U start's touchpad + Options hint
  is gone (`app/emulator.cpp`, `RunGame`; BetterVR's menu is the left Square's hold, or a touchpad + Options tap), and
  what the app posts in the
  handoff waits for the game's own surface: `ps5notify::Hold()` once the launcher gives the screen up
  (`frontend/shell/shell.cpp`, just before `m_gfx.Stop()`), `ps5notify::Release()` when a VideoOut surface is made
  (`ps5/vulkan_display.cpp`, `ps5vk::CreateDisplaySurface`), which sends what was held, in order, up to four. The other
  systems' touchpad hints are unchanged: their screens are up before they are posted. A DS game's
  "RetroAchievements: N of M achievements" notice is the one that used to land in the handoff and now shows after it.
  No BetterVR or Cemu patch: this is the app's own code. What to check: that the notification window shows nothing at
  start; send `[ui] gfx: stopped`, `[ui] the new launcher is gone ...` and the `[vulkan] VideoOut surface ...` line of
  the start.
- **Logs** (BetterVR's, mirrored to the boot log as `[bettervr] ...`): `[menu] Options down`, `[menu] Options up after
  N ms`, `[menu] Options held for 1 s: the view recenters, no pause`, `[menu] release after the second: no pause` when
  the release itself ends a hold, `[menu] short press -> pause`, `[menu] touchpad + Options tap -> open` or `closed`
  (or `[menu] touchpad + Options tap ignored, the menu's overlay is not ready`), and `[menu] short press ignored, game
  input is blocked` for a tap BetterVR's menu or the overlay keeps from the game; the runtime's one `[recenter] ...`
  line a hold.

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
- **HUD mip chain, two levels, LOD capped at 1.0** (`port/bettervr/xr/runtime.cpp`, `compose.comp`). Each frame the
  HUD image is copied into level 0 and one linear blit makes level 1 (about 5.1 Mpx in all), and the compose samples
  it with `textureGrad`, the gradients taken analytically from the quad's homography. The sampler's `maxLod` is 1.0,
  so the ~2.4x minification takes the half-resolution level instead of one bilinear tap at level 0, and never reaches
  the levels 2+ that blurred menu text in the first v0.6 build (v0.5.0 sampled level 0 only). The eye images still use
  LOD 0. When the HUD format has no linear blit the old single tap stays; the log says `HUD mip chain ...` only when
  the two levels were made.
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
  `CopyDepthToLayer`. The present pass's full-screen depth store is gone too (`vk_backend.cpp` leaves
  `depthWriteEnable` false), since the runtime's depth composition is off.
- **1x is the eye's own 1984x2048.** The Graphics pack's texture-rule search preferred 2000x2040 at 1x, which the
  compositor then scaled to 1984x2048 with a non-integer bilinear tap. The 1x preset now asks for the eye size exactly
  (`utils/eye_resolution.h`), so its compose is 1:1; the other VR Resolution steps keep the rules-compatible search.
- **A 4-tap box for higher presets.** `compose.comp` samples an eye image at its own source texels when it is
  downscaled: the taps sit `(r - 1) / 2` texels out, which is the centre tap at 1x and a 2x2 box at 2x. Upscales below
  1x keep one bilinear tap. The `shaders.h` SPIR-V is regenerated with `tools/render-shaders.sh`.

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
- **Cemu submits every 900 draws, not 300.** Each submission costs the driver a copy, an AGC submission, a state
  reset and a cache write-back, so `cemuSubmitDraws`'s default is 900; the setting still overrides it (`0` keeps 900).
- **A 5 s `[draws]` line.** Every five seconds the runtime logs
  `[vr-xr] [draws] per frame: multiview N, replay N, total N | record X ms | replay Y ms | compose Z ms | xr end W ms`.
  Cemu's draw path counts the draws that took multiview and those that fell back to the right-eye replay (patches/cemu
  0057), and times the stereo pass (`record`) and the replay; the runtime times its compose fence wait and the whole
  `xrEndFrame`. The line is always on, not behind `[frame]`'s `profile`.
- **The 4K TV picture is skipped in stereo.** When BetterVR's stereo path runs, Hmd2's social screen is the TV, so
  `LatteRenderTarget_copyToBackbuffer` does not draw Cemu's own 4K picture (patch/cemu 0056). The gamepad's view and
  Cemu's app overlays still go through; the flat screen keeps its picture. The TV stays on Hmd2's mirror (`tv` in
  `bettervr.json`).
- **Why a draw still takes the replay.** `view_canDrawMultiview` needs every active shader compiled with view arrays;
  a graphic pack's custom shader (BetterVR's Graphics pack has many) is compiled without them, so its draws replay.
  It also replays streamout, a geometry shader when the device has no multiview geometry shader support, `RECTS`
  without it, and a right uniform block the shader does not hold in its view array. A render target without the
  second view layer, or with attachments of different depths, has no multiview FBO and replays too. None of these can
  be widened without a shader or FBO that carries the right view, so this build only counts them.
- **VR Resolution in BetterVR's menu.** On the PS5 the System page's "Graphic Packs: Open On Desktop" row (there is no
  window) is replaced by `VR Resolution`: 0.5x, 0.75x, 1x, 1.25x, 1.5x from the pack's "VR Resolution Multiplier"
  presets. It saves through Cemu's preset API and logs `VR resolution preset: ...`; the pack has texture rules, so
  `GraphicPack2::RequiresRestart` is true and the existing restart notice says it applies at the next start. The pack's
  default stays 1x.
- **CI zlib.** `tools/deps.json` now puts zlib 1.3.2's archive where the boilerplate's setup expects it, from the
  GitHub release (same SHA-256 as zlib.net's); `tools/deps.py` verifies file items by hash instead of their name, so a
  short or corrupt download is fetched again instead of used.
- **The 1x compose copies the eyes and dispatches only the HUD's box.** At 1x each eye's swapchain image is the
  eye's own 1984x2048 with its whole rect, so `xrEndFrame` copies both halves into Hmd2's side-by-side eye buffer
  (`vkCmdCopyImageToBuffer`; the buffer's rows are already 3968 pixels wide) and dispatches the compute shader only
  over the HUD quad's box in each eye: the eye rect clipped by the quad's homography, a pixel or two wider than what
  the shader blends. A higher VR Resolution, the mono picture on the quad and an image that is not R8G8B8A8 keep the
  full dispatch. The 3D present writes the eye pictures opaque (patch bettervr 0023), so the copied bytes, alpha
  included, are the ones the shader wrote before; the flat screen's picture keeps its source alpha, since that quad
  blends by it. The shader takes the dispatch's first output pixel in its push constants (`origin`, 128 bytes now),
  so the box blends exactly as the full frame did. One `[vr-xr] the 1x compose copies each eye into hmd2's buffer and
  dispatches only the HUD's box (N and N pixels this frame)` line says when the path first runs.
- **The compose buffer's last barrier has no host half.** Hmd2 samples the side-by-side buffer on the GPU (its
  texture descriptor points at the mapped image) and nothing on the host reads those pixels, so the barrier asks only
  for memory reads at the end of all commands instead of a host-read write-back.
- **The compose wait can leave Cemu's GPU thread (v0.6.0, `asyncCompose`, on by default).** The console's `[draws]`
  lines show the record at 12.3-13.5 ms at 1x and 12.9 ms at 1.5x (CPU work, not pixels) and the compose wait at
  4.6-5.1 ms at 1x and 11.7 ms at 1.5x (the GPU finishing the frame), in series on the GPU thread. `xrEndFrame` now
  returns once the compose is submitted, and the frame pump's next `xrWaitFrame` (BetterVR's thread, which already
  runs right after each frame end) waits for the fence, submits the frame to Hmd2 and only then naps and begins the
  next Hmd2 frame, so Hmd2 sees the same order and timing. No worker, fence or eye-buffer rotation of its own is
  needed: one compose is in flight at most, because an `xrEndFrame` that finds a compose the pump has not taken
  (`composeFinishDue`) finishes it before it records its own, and Hmd2's buffer index only moves on at its submit. A
  fence timeout is left to the GPU thread's next `xrEndFrame`, which handles it as the synchronous path does. A
  `bettervr.json` with no key runs it on; `"asyncCompose": 0` turns it off, and `xrEndFrame` calls the same
  `FinishCompose` at once. The swapchain images are safe either way: an image comes round again three acquires on.
  Tested on firmware 13.60 at 1x (a shrine, Bokoblins, the inventory, the scope and a save): 59.9 fps of gameplay
  against 52.2 in the previous build's windows with the same draw counts, 1% low 44.4 -> 58.0, frames held for three
  refreshes 31% -> about 1%, `xr end` 4.5-5.1 -> 0.07 ms, no new warnings, and head turns felt the same.
- **The `[draws]` line's draw path counts (v0.6.0, patch cemu 0058).** Per frame: `sequences` (draws that set up
  their state in full, the first of a draw sequence), `index lists` and their KB (index cache misses, decoded and
  uploaded), `sets made` and `pipelines made` (cache misses). With `"profile": true`, `[draws] record split per
  frame` times the stereo pass's sequence setup (shaders, render targets, textures, viewport), its draws (uniforms,
  indices, vertex and uniform buffers, pipeline and descriptor sets, and the rest: binds and the draw, which is the
  driver's recording) and what is left between draws (the command stream's register writes, copies, clears and
  queries) with the TSC; about 0.2 ms a frame, only with the profile on. Every counter is added with a load and a
  store on Cemu's GPU thread, where `xrEndFrame` takes them, instead of a locked add on every draw.
- **Index lists kept across frames (v0.7, patch cemu 0059, `indexCache`, on).** At 1.25x the record split put 1.7-1.9 ms
  a frame into about 1,900 index lists decoded again, because Cemu's eight-list index cache is emptied at every GPU
  wait and finished command buffer. A decoded list is now also kept by pointer, count, mode, type and restart index:
  trusted until Cemu's next flush, then checked with the vertex cache's page hash and used again if unchanged or
  decoded again into new memory if not. Unused lists go after 60 frames; 32 MB at most; the immediate draws' index
  array is never kept. `"indexCache": 0` gives Cemu's cache. The `[draws]` line adds `kept index lists used again N,
  changed N (N KB held)`.
- **View arrays kept when unchanged (v0.7, patch cemu 0060).** A continued multiview draw no longer builds a stage's
  right-view half again when the stage's constants and uniform buffers are clean and the right view's data epoch,
  shader and stereo state are the same as at the last build.
- **RADV threaded recording stays a `ps5cemu.json` switch.** The rest of the 1.25x record is mostly the driver
  recording new state at each sequence's first draw. The driver's layer (`RADV_THREADED_RECORDING=1`) moves that to a
  worker and leaves the copying of each command's arguments on the GPU thread, an estimated 1.5-3 ms at 5-9k draws if
  the worker has a free core; `vkEndCommandBuffer`, destroys and frees wait for the worker. It has never run in a game,
  so Test request 72 runs it before any default changes.

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
  anchor check. Other firmware leaves it unbound (one `hmd2:` line) and VR continues. The value is the motor's speed
  in Hz, not an amplitude (10-25; 1-9 run at 10, 0 stops), as PSVR2Toolkit documents for the same motor, so the
  runtime maps every 0..1 level through `Hz = 25*sqrt(a)`, off below 0.08, and the felt force follows the square. The
  runtime sends on change, caps a continuous hold at 2 s and logs the first calls and every failure. It is not called
  with a zero level at VR start. Its first call is whichever comes first: the page's Headset test or a Damage Taken
  pulse (both default to the headset); the motor was felt on 13.60, so that path is not an unverified offset. The page's Headset button remains the way to call it on purpose: it stays at 25 Hz for
  400 ms and logs `[vib] test headset: level 25 -> 0x...`.
- **The DualSense is taken over while VR runs.** `port/cemu/PS5PadController.cpp` gains `PS5Pad_SetExternalRumble`
  (Cemu's own `start_rumble`/`stop_rumble` return at once, and both motors are cleared when the mode changes) and
  `PS5Pad_ExternalRumble` (the mixer's level; the launcher's Vibration row stays the outer switch, and Cemu's own
  per-controller rumble percentage is not applied while VR drives the pad). `port/bettervr/xr/runtime.cpp` turns it on
  at `xrBeginSession` and off at `xrDestroySession` and `BetterVR_PS5_Stop`; one `[pad] rumble:` line says which side
  drives the pad. No Cemu patch was needed.
- **Damage taken** is the `PlayerNormal` damage-state hook (`player.cpp`): 大/中/小ダメージ (0x101E0910/30/40), one
  falling rumble on both hands per hit. `PlayerLargeDamage`'s setup at `0x02CAB32C` is not a per-hit path: its
  parameters load once per action, and the logs show one call per boot, while a save loads. It still forwards to the
  float-param override, and its load-time call is logged and plays nothing. A Damage request takes its hand even
  while a swing, a guard or a release rumble is still running (patch bettervr 0019): the mixer's priority check used
  to drop it, and the release rumbles are Falling too, the same type the same-rumble check matches. The headset gets
  a held pulse besides the hands' falling envelope (`BetterVR_PS5_HeadsetPulse`, patch bettervr 0029): a 60 ms kick at
  25 Hz, then the hit's mapped level held for 300 ms, scaled by the Damage Taken row and the Headset Limit (100 % at
  the default 60 % cap is a = 0.48 -> 17 Hz). One `[vib] damage path=... state='...' (...) -> hands 0.80, headset
  pulse 17 Hz for 300 ms, Damage Taken 100% [...]` line per event, under `HapticsLogPatterns` like the game's pattern
  lines, names the path, the state's hash and what the row asked for, or `none, ...` and why; the runtime adds
  `[vib] headset pulse 17 Hz for 300 ms (kick 25 Hz for 60 ms, strength 0.48)`. Every `PlayerNormal` state is also
  logged once with its hash at INFO, capped at 64 states, so the next run lists the states the game really uses.
- **The life field drives damage once it is proven.** Link's life is a big-endian s32 at player actor `+0x540` in
  quarter hearts (the console's V3 scan found this field and no other: two hits took it 7 -> 4 -> 1, meals raised it
  again). The scan still runs: 300 ms after a damage or death state, `+0x540` must have fallen within the same limits
  (1..480 before, 0..before after, no more than 160 lost), on two probes, before any event uses it. A damage state
  with no life lost is not a wrong field, so a miss is retried on the next state. Until then the state hook above is
  the source, exactly as in v0.5.0; if the field does not fall on six probes, or leaves 0..480 for 60 frames, the
  state hook stays the source for the session. Once proven, every drop is one Damage Taken event and
  the state hooks play none: falls, burning, cold and heat ticks and a lethal hit all count. The hands' amplitude is
  0.4, 0.6, 0.8 or 1.0 of the state hook's 0.8 for 1, 2, 3 or 4+ quarter hearts lost (reaching 0 is the full one), so
  the headset pulse is about 11, 13, 15 or 17 Hz. A rise (food, a heart container) updates the baseline and never
  fires. The baseline resets on a loading screen, on a new player actor and when the actor address is lost, so a
  load cannot fire. One `[vib] damage path=life <before>-><after> quarter hearts -> ...` line per drop says what the
  row asked for or why it played nothing; one `[vib] life: player+0x540 is the life field ...` line says when the
  field took over.
- **Explosions are the game's blast actor, once per real detonation.** BotW's `ResidentActors.byml` keeps one
  `Explode` instance (`only_res: false`) and the game places it at the blast spot, where its `BombExplode` action runs
  for 15 frames (a new instance, if the game makes one, is the same signal). The actor-list hook records that pointer
  once and each game frame reads its matrix; a move over 0.5 m (or the first placed spot of a new record) within 30 m
  of Link plays one falling rumble on both hands, full at 3 m and straight to nothing at 30 m, with the headset
  following through the mixer's level. A second blast actor at the same spot within 150 ms is the same detonation and
  plays nothing. A loading screen, a missing player actor or a new player actor clears the recorded spots and starts a
  one-second quiet window, so a load's placement, the resident actor's re-creation and a save's first frame cannot
  fire. Removals, ids and `BaseProc::state` are no longer read: state 3 is a live idle state (a stuck arrow sits
  there), so the old test re-armed the same arrow on every actor-creation pass, and a nocked, spent, recalled or
  despawned explosive could not be told from a blast. One `[vib] explosion Explode <dist> m +<n> ms -> ...` line per
  event says what the row asked for or why it played nothing; `n` is the frame interval in which the move was seen, at
  most one game frame after the detonation.
- **The game's rumble stays one category for now, with a learning log.** At the `VPADControlMotor`/`StopMotor` hooks
  (`controls.cpp`, at most 40 lines 150 ms apart, `HapticsLogPatterns` turns it off) it prints the caller LR, the
  playing state, the VPAD pattern bytes, and either the sead `PatternRumbleAddon`'s string at `+0x14` and its repeat
  count at `+0x1C` when the addon is in a callee-saved register or on the stack, or `pattern="?"`.

### Adaptive triggers

- **What the effects are.** R2 (the bow) uses feedback mode 1 at position 2, the mode the firmware accepts: the
  stop's strength grows with the game's own `drawAmount` (weapon `+0x94C`, 0-1.5 clamped to 1), 1-8 across the
  strength setting, so a held trigger gets firmer as the bowstring comes back. Off below 2 % draw. L2 (the shield)
  is feedback mode 1, position 2, strength 3-5 across the setting, while
  `state.isShieldGuarding`; off otherwise. Both are off when BetterVR's menu is open, on the pause and inventory
  screens (`GameState::IsShowingMenu()`), and outside the game. The effect is sent on change, at most 20 times a
  second; an off pair goes out at once. A call libScePad refuses is not marked sent: the same effect is tried again
  5 s later, so a failure cannot hide a value until it changes (the rule is AstroBotVR2's), and a refusal never
  stops the other device or a later call.
- **BetterVR's side** (patch bettervr 0009): `Bow::GetDrawAmount()` (bow.cpp/bow.h); one call each input frame in
  `hook_InjectXRInput` (controls.cpp), and an off call when the input sample is not ready; the PS5-only `ModSettings`
  rows `AdaptiveTriggers` (default true) and `AdaptiveTriggerStrength` (0-1, default 1), which save to
  `BetterVR_settings.ini` like every other row; and a System > **Adaptive Triggers** card with the toggle and the
  strength slider, shown while the toggle is on.
- **The DualSense** (default on): `scePadSetTriggerEffect`, NID `2JgFB2n9oUM`, imported at link time. The packaging
  tool turns the SDK stub's symbol into an import under that NID, and the loader binds it from libScePad, so the name
  table `sceKernelDlsym` reads is not involved; that lookup cannot see the export on 13.60, which is why the setter
  used to stay unbound. The call goes to the handle the app's pad service holds for player 1 (`PS5Cemu_PadHandle`).
  The System card's **DualSense** row reads **Available** once the bind line has been logged, and **Unavailable** only
  if it was not; the Sense rows stay separate. The import cannot be missing on a firmware the app runs on, and the
  export is not new: it is the DualSense's launch feature (the `ScePadTriggerEffectParam` header is Copyright 2019, and
  Astro's Playroom, installed on every PS5 from launch, is the trigger demo), and the NID is the name hash the packaging
  tool computes, not a per-firmware guess, so the same export has the same NID on every firmware. The public NID tables
  list it for 5.50 through 13.60 without a gap, and AstroBotVR2 resolves it on 13.60. A weak import cannot make it
  optional: the packaging tool (`PS5_Vulkan`'s `tooling/native/sce_module_writer.cpp`) turns every undefined symbol that
  an SDK stub exports into a required import, with no weak form, and `tools/link.sh` drops undefined weak symbols
  (`-z nodynamic-undefined-weak`); a weak declaration either still becomes a required import or leaves the call null.
  The strong import stays. The parameter is the 0x78-byte `ScePadTriggerEffectParam`: mask 0x3 (L2 and R2), the L2
  command at +0x08, R2 at +0x40, each a u32 mode, four padding bytes and 0x30 data bytes. The layout and the builders
  are `port/bettervr/xr/triggers.h`, with static asserts on the command and parameter sizes and offsets, and a host
  test (`tools/tests/test-bettervr-triggers.sh`, run by the Tests workflow).
- **The Sense controllers** (the System card's **Sense Triggers** row, default off, saved in
  `bettervr.json` as `senseTriggers`; a hand-written `true` still works): `scePadVrControllerSetTriggerEffects`,
  NID `v8P+9PRqg10`, bound by name first and otherwise at the 13.60 table's `0xa730` only when its anchor sits
  exactly (see the firmware gates below). It takes the DualSense's 0x78 parameter, `command[0]` for the left
  controller and `command[1]` for the right. The one-hand `0x38` export is not used: it has never been bound on
  13.60, and every send there was our own `0x80000001`, so a refused effect is only retried, never followed by
  another layout.
- **The Triggers test** (the Vibration page): one press sends the next of eleven fixed payloads to the DualSense
  and the Sense controllers, whatever the Sense Triggers row says, and writes one
  `[trig test] step N/11 <payload> rc=0x... (device)` line a device. The payload table is
  `ps5trig::kTestPayloads` in `port/bettervr/xr/triggers.h`, so the host test checks its bytes. A step holds 15 s
  or until the next press; meanwhile the game's own effects are held back, and the last step (all off) ends the
  test and lets them out again.
- **Always off on the way out:** `xrDestroySession`, `BetterVR_PS5_Stop` and the failed-start path all send the off
  pair before the pad handles go; the effect lives in the controller's firmware and would otherwise outlive the game.
- **Logs** (the boot log, each line starts `[trig]`): `Sense trigger effects on/off ...`, the binding lines from
  hmd2 (`DualSense trigger effects bound by NID 2JgFB2n9oUM (link-time import, ...)`, `Sense trigger effects bound
  (both hands ...)`), one line per effect call with both commands and libScePad's answer
  (`DualSense (handle N) L2 feedback 2 5, R2 feedback 2 8 -> 0x00000000`, `Sense both hands (0x78) L2 ..., R2 ... ->
  0x...`), the `[trig test]` lines, and `DualSense off (...)` / `Sense off (...), both hands`.

### Eye tracking and the throw assist

- **Gaze probe in hmd2 (13.60 only).** `sceHmd2GazeGetResult`, `sceHmd2InternalGazeStart` and
  `sceHmd2InternalGazeStop` are bound at PS5VR's 13.60 offsets (`+0x35510`, `+0x31b60`, `+0x31b80`) after
  libSceHmd2's `"Not Initialized"` anchor matched (`kAnchor_libSceHmd2_1360`), the same exact-anchor, no-scan way
  as `sceHmd2SetVibration`; the four gaze names with no public offset are not used. The bind is optional: another
  firmware logs `hmd2: [gaze] unavailable on this firmware (no verified libSceHmd2 table)` and the stream stays
  off.
- **Start and log.** After `sceHmd2Open` the probe calls `InternalGazeStart()` once and logs its return code;
  `shutdown()` calls `InternalGazeStop()`. Each `begin_frame()` polls `GazeGetResult` with the 0x10-byte parameter
  `{0x10, headset handle, 0}` into a 0x38-byte result. A sample is valid when the call returned 0, `+4` is 0, the
  `+0x10` flag is 1 and the direction at `+0x14` is finite and unit length. Every logged line carries the raw
  0x38 bytes and the parsed direction, validity and status: about twice a second for the first 60 s of VR, then
  every 10 s, and one measured new-`+8`-sample rate line after the first minute.
- **Logging only.** `hmd2::gaze_available()` is false in this build, so the runtime does not list
  `XR_EXT_eye_gaze_interaction`, does not make the profile or the action set, and BetterVR logs
  `Supports Eye Tracking: No`; the Controls > Motion Controls **Gaze Throw Assist** row stays hidden. The
  console's Test request 60 confirms the direction and rate before any gameplay use.
- **Direction frame.** The firmware gives the direction in its headset frame, `-z` forward, and `xrLocateSpace`
  uses it as it comes (the old `{-x, y, -z}` flip is gone); the pose is rotated by the frame's head pose into stage
  space. This path stays for when the extension is offered again.
- **Throw assist.** Patch bettervr 0010/0028. With the setting on, a released throw takes the point 10 m along the
  gaze ray from the head and bends the hand's world velocity toward the line from the throwing hand to that point
  by at most 15 degrees; the speed is unchanged, and the game's own 1.2x cap still applies. The assist does nothing
  without a valid gaze sample (the old head-direction fallback is gone), so a leftover `EyeGazeThrowAssist: true`
  cannot bend a throw while the row is hidden. A left-hand throw uses the left hand's position. The setting is
  `Gaze Throw Assist` in Controls > Motion Controls, default off. Log line:
  `[Throw] gaze assist bent N degrees toward the gaze`.
- **Backlog, not this build:** item highlight and bow aim assist.

### Touchpad click

- **The Slate on a press.** A player's DualSense touchpad click is `- (Select)` by default on the GamePad and the Pro
  Controller (`touchpadClick` is 2, "Presses a button"); Create presses it too, a finger still moves the GamePad's
  cursor, and touchpad + Options is still only the port's menu. An older `ps5cemu.json`'s 0 (the old automatic default)
  becomes 2 once (a marker, `touchpadClickMigrated`, keeps a later choice of Automatic); 1 and 2 are kept.

### Snap turn in third person

- **Where the third-person yaw comes from.** In third person BetterVR applies no yaw of its own: the game's
  `CameraChase` (and `CameraTail`) reads the right stick and adds the per-frame yaw itself, while
  `hook_UpdateCameraForGameplay` (`camera.cpp`) adopts that game yaw (`ShouldAdoptGameCameraYaw`), and BetterVR only
  writes the stick into VPAD in `ProcessJoystickInput` (`controls.cpp`). The pivot hooks that correct first-person yaw
  do not run for third person. The smallest correct place is therefore the game's own stick-yaw integration, already
  hooked at `captureCameraChaseStickYaw` (`0x02B9C96C`) and `captureCameraTailStickYaw` (`0x02BEEBCC`) in
  `resources/BreathOfTheWild_BetterVR/patch_CTRL_CameraControls.asm`.
- **The setting.** Play Style > Third-Person Camera > **Snap Turn (third person)**, key `ThirdPersonSnapTurn`, default
  off. It reuses the first-person Turning choice's snap angle (`GetSnapTurnAngle`) and
  the shared threshold/re-arm latch, `UpdateSnapTurnStick` in `controls.cpp`: one step per push, and the stick must
  come back past the release threshold before the next. With the toggle on but Turning set to a smooth choice there is
  no snap angle, so third person keeps its smooth turn.
- **How the step is applied.** With the setting on, the right stick's horizontal part (DualSense's in the physical
  VPAD sample plus a Sense stick in XR) is withheld from VPAD, and `hook_OverrideThirdPersonChaseStickYaw` /
  `hook_OverrideThirdPersonTailStickYaw` (`camera.cpp`) replace the game's per-frame smooth yaw delta with one
  `-direction * angle` step. The tail site takes the smooth delta back out of the base yaw so the following `fmadds`
  cannot add it again. Off, both hooks return before writing anything and VPAD is untouched: v0.5.1's behavior.
- **v0.6.1: the offset of the displaced load.** v0.6.0 moved `lfs f12, 0x194(r1)` in
  `captureCameraChaseStickYaw` before the trampoline's stack pop but kept the game-frame offset. The trampoline's
  frame is 0x20 bytes, so the same slot is `0x1B4(r1)` before the pop (as the capture load already said); reading
  `0x194(r1)` took the game's `0x174` slot. With the setting on the hook overwrites f12, so the step was right; with
  it off the chase camera applied the wrong slot every frame, which was no turn on foot and a self-turning swim
  camera in water. Patch bettervr 0036 loads `0x1B4(r1)`, the instruction v0.5.1 executed after the pop.
- **Log.** One `SnapTurn: turning N degrees in third person` line per step (N is ±30, ±45 or ±60; the sign is the
  step's direction). One `[turn] mode:` line when the effective mode changes (`first person smooth, N deg/s`,
  `first person snap, N degrees`, `third person smooth, the game's right stick`, `third person snap, N degrees`),
  and at most one a second `[turn] <source> applied N deg of yaw with the right stick at rest (...)` when a yaw
  arrives with the stick inside the deadzone for a quarter second or more, at the first-person application and both
  third-person sites.
- **Not done:** no comfort fade or blink is added for the third-person step. Patches bettervr 0012 and 0036. Built,
  untested on console.

### Magnesis wrist twist

- **The twist is asked for, then checked.** While a metal object is held (`hook_GetMagnesisHeldBodySpin`, `magnesis.cpp`),
  the right grip still asks for an angular velocity from the hand's rotation since the grip:
  `axis * min(15 * error, 20 rad/s)` through `setAngularVelocity`. The first request above 0.5 rad/s arms a probe: the
  next frame measures how fast the body's own transform turned since. A body that turned at least a quarter of the
  request keeps the angular velocity path for the rest of the hold; one that stayed near zero takes the transform path
  instead, and one line says which. Fixed objects (`MagneManagerFixedObject`, manager +0x2C3) never arm the probe and are
  untouched.
- **The transform path.** The hook turns the body's matrix about its own centre (the basis turned, the position kept) by
  `min(15 * error, 6 rad/s) * frame time` and returns 2, so a new label in the same cave calls `setTransform`
  (`0x03486CD8`) instead of `setAngularVelocity`. Both cave sites (`0x02B0117C` and `0x02B011C8`,
  `patch_CTRL_EntityController.asm`) now hand the hook the body in r5; only the first call of a game frame acts, and a
  frame longer than 50 ms counts as 50 ms, so a hitch cannot snap the object round. Patch bettervr 0013.
- **The slide.** If a wrist twist also moves the object along the stick, the reach ratio the aim scales the stick length
  by is the suspect: `stickLength = |grip hand->object| * (reach / reach0)^2` (`DriveMagnesisAimFromController`), and the
  tracked controller position moves when the wrist turns, squared. The `reach=` field is that ratio, recomputed in the
  spin hook from the same head and hand pose. It is left alone in this build: changing it means changing how far a push
  or pull moves an object, which needs the console's values first.
- **Logs** (INFO, so always on, at most twice a second while a body is held):
  `[magnesis] spin: fixed=false twist=12.3 deg want=5.2 rad/s body=0.0 rad/s reach=1.03x`, where `twist` is the hand's
  rotation since the grip, `want` the angular velocity the game is handed (also the rate the transform path aims for),
  `body` the body's own angular speed measured from its transform between frames (no angular-velocity getter is pinned),
  and `reach` the stick length's ratio. Once per hold: `[magnesis] spin path: angular velocity (probe: wanted 5.2 rad/s,
  body 4.9 rad/s)` or `... transform (probe: wanted 5.2 rad/s, body 0.0 rad/s)`. The older aim line reads
  `[magnesis] hand aim on/off`.

### Settings and About

- **Settings are BotW VR's alone.** The PayPal card is gone from About, which now has one licence line (Cemu and PS5-ECAM under the GPL, Crementif's BetterVR under MIT) and only the Wii U's paths; the backup, box art, recompiler and remote-screen texts no longer name other systems, and what is left is marked "ask" in the track's settings audit.

### The audio click every half second

- **What the recording showed.** In idle play a click lands every 0.500 s (24,000 samples at 48 kHz, 60 of 60 points in
  30 s): a 2-8 sample impulse, up to 28 dB over the >6 kHz floor, with no zero gap and no repeated buffer. The console's
  audio path is `port/cemu/PS5AudioAPI.cpp` only, since Cemu's other audio APIs are unsupported here.
- **The cause.** Cemu hands over 576-frame blocks (12 ms, `AXOut_init`), AudioOut plays 256-frame grains (5.33 ms), and
  `OutputThread` padded every grain that was short with silence (`PS5AudioAPI.cpp`, the fill after the mix). The queue's
  refill target was Cemu's `audio_delay`, 2 blocks = 24 ms, inside the block and grain jitter and only four and a half
  grains deep, so a few frames short at a grain boundary became the padded tail: the click's 2-8 samples, once per
  crossing of the stream's clock and the console's. `FeedBlock` could also drop a whole 576-frame block (12 ms of sound)
  when the 288 ms queue filled.
- **The fix.** `port/cemu/PS5AudioQueue.h` keeps whole blocks, and the output thread plays a grain only when a whole one
  is queued: a short grain is waited for, never padded. The target is `max(audio_delay, 4)` blocks (48 ms) with eight
  blocks of room, so a hitch drains the queue without leaving a grain short; a stall that fills it gives up the oldest
  sound and keeps the newest. The output thread now asks for priority 384, as the other ports' sound threads do. No
  Cemu file changed, so no patch file is added.
- **Logs.** At most one line every 10 s, and only when something happened:
  `PS5 AudioOut: dropped blocks N, padded grains M since the last line (queue K ms)`. In steady play it never appears.
- **Host test.** `tools/tests/test-audio-queue.cpp`, in the Tests workflow, runs Cemu's 3 ms and 2.9 ms producer against
  the 256-frame consumer: 30 s exactly matched, a 30 ms hitch (the old 24 ms target pads a grain, four blocks do not),
  and two minutes 300 ppm apart, with no block dropped and no grain padded.

### Settings follow-ups for one game

- **The folder row is gone.** Settings > Game files no longer has "A folder for each system" (`settings.cpp`), and the
  launcher no longer offers it at start (`shell.cpp`); an install already in separate folders is still read as it is
  (`ps5reorganize::Look` and the goback guard stay), and the row's action is removed with it. The folder and file names
  (`/data/ps5cemu`, `ps5cemu.json`, `backups`, `mods`) are unchanged.
- **Box art reads for one game.** "No box art yet" / "Downloaded", "The game's cover, from GameTDB.", "Download the box
  art" and "Download the box art again", with the descriptions singular: no "every game on this side".
- **No guide links.** The mods help texts no longer end with "The guide on GitHub, docs/GUIDE.md, has more."
- **Strings.** Every change is through `Tr`/`TrF`/`TrP`, and every language table has the new strings: `tools/lang.py
  check` passes and `qps.po` is regenerated.

### Firmware gates for libScePad and libSceHmd2

- **Never a shifted offset.** The v0.5.1 hotfix is in this branch: `module_table()` and `resolve_module()` take a
  `scan` flag, and a library whose code offsets are only right where its table's anchor string sits passes false. A
  moved anchor therefore never moves a call into unknown code (the 12.40 console's signal 6 at the first Sense
  vibration-mode call, issue #2). The Sense vibration exports already did this; the Sense trigger exports and the
  headset motor now do too (`third_party/hmd2/hmd2.cpp`).
- **One line, then VR continues.** Each of those optional binds is by name first and at the exact 13.60 table
  otherwise. Where neither works, one line says
  `Sense vibration off on this firmware (no verified libScePad table)`,
  `Sense trigger effects off on this firmware (no verified libScePad table)` or
  `sceHmd2SetVibration off on this firmware (no verified libSceHmd2 table), the headset stays still`; VR, tracking and
  the game run on. The DualSense's setter is a link-time import under its NID (no name lookup, no table) and the five
  gaze names are name-only and never use a table.

### Open

Nothing here has run on a console. The mip chain's cost and the skipped passes need the `[draws]` and `[frame]` lines;
the HUD quads' edges need a headset. The optional FSR/upscale for presets below 1x is not done; the compose boxes
BetterVR's already-upscaled eye image. The compose fence wait leaves Cemu's GPU thread with `asyncCompose`, on by
default since the 13.60 console result; Test request 69 checks the default at 1x and 1.5x and the `"asyncCompose": 0`
path. Index lists kept across frames (0059) and unchanged view arrays kept (0060) target the 1.25x record; Test
request 72 measures them and RADV threaded recording at 1.25x. The Sense trigger
payload, which of its two formats the firmware accepts, and its return-code meaning are unverified; the DualSense's
pad handle is opened in vibration mode 2. The headset's
`+0x2ae00` is from public tables, not our own dump; its test button is the proof. Explosions have settings but no
event source. The five gaze names, the meaning of status 0 and the direction's sign wait for the console. The
third-person snap step's direction, its pad/Sense merge and the release threshold wait for the console too. For the
magnesis twist, the console decides whether `body=` follows `want=` (the angular velocity path) or stays at zero (the
transform path), and whether `reach=` moves during a pure wrist twist; if `body=` stays at zero on both paths, the game
writes the body's transform after the cave and the next probe is a later hook site.

Test request 53 in `STATUS.md`.

## v0.6 body, HUD and needle

Done, one new patch (bettervr 0011; no Cemu patch was needed):

- **The strafe flash.** A frame whose actor, model, hand pose or bone is skipped now writes the last good first-person
  `Skl_Root` before it returns (`WriteLatchedBodyRoot`, `skeleton.cpp`), so the game's stale anim root can no longer
  put the gear in front of the eyes for that frame. The root itself is still posed from the live player matrix
  (`Player::GetMtxAddress`) and `Camera::GetFreshReferenceMtx`, as in 0.5.0: a 0.6.0 build posed it from a player
  matrix the camera hook built from the camera's own rotation, which is not the player model's matrix, and
  `ExtractPlayerBodyYaw`'s fixed 180 then left the body facing away from the view. Log at most once a second:
  `[pose] first-person body root kept the last good frame (...)`.
- **The HUD's squared alpha.** The ImGui background draw now binds a second, premultiplied pipeline
  (`dependencies/imgui_impl_vulkan.cpp`, `ImGui_ImplVulkan_PremultipliedBlendCallback`) for the HUD image only, so the
  game's premultiplied HUD is written `rgb + dst*(1-a)` instead of `rgb*a²`; menu text, menu quads and the 3D
  background keep the normal straight-alpha pipeline (`vulkan_imgui.cpp`, `renderHUDBackground`).
- **The first 2D frame after a screen opens.** `Capture::OnHud` (`capture.cpp`) holds the first 2D capture after a
  screen change for one frame when a HUD has already been presented, so the wheel's first frame no longer shows the
  bright pre-menu 3D copy; log: `[capture] held the first 2D capture after a screen change ...`.
- **The world shift on closing the wheel.** `Camera::ResolveGameplayBasePose` (`camera.cpp`) logs its source for the
  first frames after a screen change: `[camera] first frame after a screen: source=cached|game (...)`. No fix: the
  log has to say which source the two frames took before a change is justified.
- **The blue needle in third person.** A short press no longer opens the world guide on the PS5 (`UpdateGuide`,
  `controls.cpp`): it is only the game's pause, whichever controller sent it; a hold is the runtime's recenter
  (`runtime.cpp`), and BetterVR's menu opens on the left Square's hold or a touchpad + Options tap.
  The bow aim arc (`Bow::QueueAimingArcPreview`, `bow.cpp`) is cleared in third person below a 5 % draw, so a resting
  bow no longer draws it. Logs at most once a second: `[guide] world guide ribbon drawn in third person ...` and
  `[bow] bow aim arc drawn in third person (draw=..., engaged=...)`.

Open: built only on CI; nothing in this section has run on a console. The HUD pipeline is a second pipeline object
(one more pipeline at ImGui init), and the 2D hold is one skipped capture per screen opening.

Next step: Test request 53 (b), (c) and (k) in `STATUS.md`: the `[pose]`, `[capture]`, `[camera]`, `[guide]` and
`[bow]` lines, whether the body faces the view again when walking, and whether the flash, the wheel's first frame,
the close shift and the needle are gone.
