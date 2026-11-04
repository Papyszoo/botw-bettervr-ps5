# Status

Newest entries first. Every console result here was reported by Patryk. Nothing else counts as
tested.

## 2026-11-04: M5, BotW VR as an app of its own (Patryk's choice)

**Built, untested on console.** Patryk chose an own title ID for the whole app and the other
emulators taken out.

- **Release:** `m5-botw-vr`, on the commit named in its notes. Test request 5 below.
- **Title ID `PPSA98720`, "Breath of the Wild VR".** Not found in a web search, and outside the range
  the app's own forwarder tiles draw from (PPSA99200-99899, psfwd.c). Changed in the code, scripts,
  `sce_sys/param.json`, the payloads, host tests and the string tables (only the ID in the
  messages; `lang.py check` passes). Not changed: Eden's patches and `tools/build-eden.sh` (Eden is
  no longer built), upstream's docs, README and site.
- **Its own data, `/data/botwvr`:** `PS5CEMU_DATA` is `/data/botwvr/launcher`, so Cemu's folder, its
  sibling, is `/data/botwvr/cemu`. On a drive: `BotWVR/launcher` and `BotWVR/cemu`. The boot log and
  the drive's folder make `/data/botwvr` (`BotWVR`) first.
- **Cemu alone.** `tools/build-cemu.sh` passes no Azahar, melonDS or libretro archives, so the port
  links `azahar/unavailable.cpp` and `melonds/unavailable.cpp` (both needed `SetResume`).
  `tools/package.sh` packages no Eden, ES-DE, retro runtimes or 3DS borders, and the Makefile builds
  none of them. The launcher shows the Wii U side alone (`ui.sides` is 1, and Settings > Launcher
  has no Sides). Their code stays in the tree, unbuilt, so upstream still rebases.
- **No update checks** (`kOwnUpdates` in `port/app/updates.cpp`): premohq's release is another app.
- **Art:** the app's own tile and background (`tools/render-botwvr-art.py`: a visor and "BotW VR",
  nothing of Nintendo's), and at run time `port/bettervr/TileArt.cpp`. Once the launcher has the
  player's Breath of the Wild (`Shell::PollBotwArt`), it writes `sce_sys/icon0.png` from its box art
  (or icon) and `pic0.dds`/`pic1.dds` from its boot screen (BC7, the Python encoder in C++) into the
  app's folder, and `icon0.png` into `/user/appmeta/PPSA98720` when that exists. Whether the home
  screen picks them up is Test request 5's question.
- **Cemu's version** (Patryk asked): upstream's `cc6fea1` from 4 October 2026, after Cemu's last
  release (2.6, February 2025), with PS5-ECAM's 48 patches and ours.

## 2026-11-04: install guide; the app's name, title ID and tile

- **`INSTALL.md`:** where the game, its update v208, the DLC and (for WUD/WUX only) `keys.txt` go,
  which formats work, and the steps from install to VR. Written from the app's guide and code, not
  checked on a console.
- **Asked by Patryk:** an own title ID and name, and a tile and background from the game. The options
  as written then (he chose the second, with the other emulators out: M5 above):
  - **Game art can't ship.** BotW's icon and boot picture are Nintendo's, and the hard rules keep
    game material out of the repo and releases. They can be taken from his own dump on the console,
    at run time: the app's **Add to home screen** already makes a game a tile of its own (a new,
    free title ID, the PS5 Forwarder Format) with its box art. Adding the game's `bootTvTex` as the
    tile's background (`pic0`/`pic1`, BC7 DDS) and having that tile start VR is a contained change.
  - **An own title ID for the whole app** is the change judged too wide on 2026-11-01 (about 120
    lines across code, scripts, five Eden patches and 30 translations), plus the HEN's jailbreak list
    needs the new ID. The app's data would still be shared with PS5-ECAM unless `/data/ps5cemu`
    changes too.
  - **What PS5-ECAM bundles:** Cemu, Azahar (3DS), melonDS (DS), Eden (Switch, 78 MB of the 210 MB
    zip), five libretro cores, an ES-DE port, the launcher, streaming and an updater. 33
    dependencies (9.5 GB in `.deps`). BetterVR's part is about 3,000 of the port's 71,000 lines.

## 2026-11-04: M4, the Sense controllers (built ahead of the M2 and M3 results, as Patryk asked)

**Built, untested on console.** Patryk asked for M4 with the Sense controllers, also ahead of the
results for M2 and M3.

- **Release:** [`m4-sense`](https://github.com/Papyszoo/botw-bettervr-ps5/releases/tag/m4-sense),
  a pre-release on `0981a54`. The zip's SHA-256 is `02b65718…0379`. Test request 4 below. The
  ELF's one new system import is `scePadVrControllerReadState` (libScePad).
- **What's in it:** the OpenXR runtime's actions are real now (`port/bettervr/xr/runtime.cpp`).
  - **Opening:** the Sense controllers are opened for Hmd2's user as scePad types 3 (left) and 4
    (right), with `scePadOpen`, else `scePadGetHandle`, retried every 2 s. Each is registered with
    VrTracker2 (`hmd2::register_controller`) as photo-splat-vr does.
  - **Reading:** once a frame (`xrSyncActions`) with `scePadVrControllerReadState`: the button word
    at +0x00, the stick at +0x04/+0x05, the trigger as the larger byte of +0x08/+0x09, and
    connected at +0x40 bit 0 (`third_party/hmd2/reference/pad_input.h`, HLA-PSVR2's offsets).
    While the system menu has the pads (bit 31), every input reads as nothing.
  - **The Touch profile BetterVR binds**, onto the Sense controllers:

    | Touch | Sense | BetterVR, in game |
    | --- | --- | --- |
    | left X, Y | Square, Triangle | mod menu (X), rune / d-pad menu (Y) |
    | right A, B | Cross, Circle | run / interact (A), jump (B) |
    | squeeze (both) | L1, R1 | grab / interact |
    | trigger (both) | L2, R2 (analogue) | use the item in that hand |
    | thumbstick (both) | the stick | move (left), camera (right) |
    | thumbstick click | L3, R3 | crouch / scope (left), inventory / map (right) |
    | menu | Create (left), Options (right) | not bound by BetterVR |

  - **Poses:** grip and aim are VrTracker2's controller pose (`hmd2::controller_pose`, tracker
    space, like the head). Velocities are worked out from successive poses, because BetterVR's
    weapon swings use them. `gripPitch` and `aimPitch` in bettervr.json tilt the two poses about
    the controller's x axis, in degrees (default 0), so a wrong angle can be fixed without a new
    build.
  - **Profile:** `xrGetCurrentInteractionProfile` names the Touch profile while a controller is
    open. A controller that connects or goes away is an interaction-profile event.
  - **Haptics:** BetterVR's rumble goes to `scePadSetVibration` on the Sense controller for the
    duration asked, with the vibration mode the app's DualSense uses (2). The first call's result
    is logged.
  - **DualSense:** the game's GamePad through Cemu, as before. BetterVR merges it with the
    controllers' input, so it still works next to them.
- **Not in this build (rest of `PLAN.md`'s M4):** the 40/60 fps locks (hmd2 shows each frame for
  two of its 120 Hz refreshes, a 60 fps cadence, and a 40 fps lock needs a change there),
  `XR_FB_display_refresh_rate`, and BetterVR's performance options as settings.
- **Unverified, and what would show it:**
  - **The pose path:** hmd2's own note says the controller pose path is "UNVERIFIED on hardware".
  - **The poses' axes:** which way a Sense controller's pose points, against what Touch's grip and
    aim mean. Hands tilted by a fixed angle mean adjusting `gripPitch`/`aimPitch`. Hands mirrored
    or rotated about another axis mean a code change.
  - **The vibration call:** whether `scePadSetVibration` drives a Sense controller at all.

## 2026-11-04: M3 built ahead of the M1 and M2 results, as Patryk asked

**Built, untested on console.** Patryk asked for M3 before Test requests 1 and 2 came back, to try it
on his PS5. `PLAN.md` says to stop where work builds on a guess about the console, and M3 builds
on M2 working (hmd2 on Cemu's device, next to the TV's VideoOut), which nobody has seen yet.
If M2 fails on the console, M3 fails the same way. Test request 3 starts with M2's check for that
reason.

- **Release:** [`m3-stereo`](https://github.com/Papyszoo/botw-bettervr-ps5/releases/tag/m3-stereo),
  a pre-release on `a0b2c3d`. The zip's SHA-256 is `8ce33465…62b6e`. Test request 3 below.
- **Build:** `make release` with `EDEN_SKIP_SOURCE_CHECKS=1`, as before. `tools/build-bettervr.sh`
  takes about 5 minutes from clean (30 files, each with BetterVR's 80,000-line precompiled header).
  A new patch in `.deps/Cemu` changes Cemu's `EMULATOR_HASH`, so all of Cemu compiles again (about
  25 minutes here), then the 12-minute link. `ps5cemu.elf` is 165.6 MB, 1 MB more than M2. Its
  new system imports are `__atomic_load`, `__atomic_store`, `atanf`, `remainderf` and `tanf`, all
  from `libSceLibcInternal`, like the app's other maths.
- **Choice: option A, a small OpenXR runtime over hmd2** (`port/bettervr/xr/runtime.cpp`), as
  `PLAN.md` prefers. It keeps BetterVR's code close to upstream: patches/bettervr 0002 and 0003 are
  about 150 lines, none of them in its renderer's logic.
- **How it fits together:**
  - **BetterVR's object:** `tools/build-bettervr.sh` builds BetterVR 0.9.23 (with patches/bettervr
    0001-0003), its own ImGui 1.91.9 and ImPlot 0.16, and the runtime with the console toolchain
    into one relocatable object, `build/bettervr/bettervr.o`. Every symbol in it is local but
    `VRLayer_NegotiateLoaderLayerInterfaceVersion` and `BetterVR_PS5_Stop`, because Cemu links
    ImGui 1.89.5 of its own. `patches/cemu/0049` adds it to CemuCafe.
  - **The layer without a loader:** `port/bettervr/VulkanLayerPS5.cpp` is the
    `vkGetInstanceProcAddr` Cemu is given (one line in `port/cemu/VulkanPS5.cpp`). For Breath of
    the Wild with BetterVR on, it creates the instance and the device through BetterVR's layer with
    the loader's link info; everything else takes RADV's functions as before. vkroots keys its
    tables by the handles themselves, so it needs nothing of a loader's dispatch data.
  - **Cemu's three exports** (`gameMeta_getTitleId`, `memory_getBase`, `osLib_registerHLEFunction`)
    are named directly in `include/platform/compat.h` (patch 0002). BetterVR registers its 74 HLE
    hooks the way upstream does, on the first frame whose magic clear colour its graphic pack draws.
  - **One queue:** RADV on the PS5 exposes one graphics queue (`radv_physical_device.c`, queueCount
    1), so BetterVR's three-queue setup falls back to sharing Cemu's, as its own code allows. Patch
    0002 adds one lock (`ps5xr::QueueMutex`) around every submission and present on it: Cemu's
    (through the layer's QueueSubmit and QueuePresentKHR), the submit thread's, and the runtime's.
  - **The runtime:**
    - It offers `XR_KHR_vulkan_enable`, and `XR_KHR_composition_layer_depth`, which BetterVR
      requires. The depth it is given is ignored, since Hmd2 reprojects without depth. That's the
      "make the depth layer optional" step, done on the runtime's side, not BetterVR's.
    - Views come from hmd2's predicted head pose for the asked time, with the eyes' offsets from
      the frame's tracker result (half a 63 mm IPD each side until there is one) and hmd2's FOV
      tangents. Spaces are hmd2's tracker space, whose axes are OpenXR's (+y up, −z forward,
      metres; photo-splat-vr's `vr_mode.cpp`).
    - Swapchains are three images each on Cemu's device, 1984×2048 per eye (photo-splat-vr's
      size).
    - `xrWaitFrame` is hmd2's `wait_slot` + `begin_frame`. `xrEndFrame` composes the projection
      layer and the HUD quad into hmd2's eye buffer in one compute pass
      (`shaders/compose.comp`: sRGB in, sRGB bytes out, the HUD blended by its alpha), waits for
      it, and calls `hmd2::submit(render pose)`. That's a new overload in `third_party/hmd2`, which
      sends the head pose the submitted views were located from.
    - Actions are all inactive, so BetterVR keeps the game's own GamePad input (the DualSense
      through Cemu, which BetterVR merges with its own) and hands nothing to the hands. The Sense
      controllers are M4.
  - **Graphic pack:** `tools/package.sh` puts `resources/BreathOfTheWild_BetterVR` (without its
    debug patches) in the app's `assets/bettervr`. At start, before Cemu loads its packs, the app
    copies it to `graphicPacks/bettervr/` while bettervr.json asks for BetterVR, and removes that
    copy when it doesn't. The pack is on by default, and its patches call BetterVR's hooks, which
    only exist while BetterVR runs: a leftover copy would break the game flat. FPS++ (which
    BetterVR needs) comes with the community packs the app downloads; turn it on yourself.
  - **The switch:** `bettervr.json`'s `{"vr": true}` now means BetterVR for Breath of the Wild, and
    M2's flat screen for every other game. `{"vr": true, "bettervr": false}` gives Breath of the
    Wild M2's flat screen, to compare.
  - **Logs and settings:** BetterVR's lines go to the boot log as `[bettervr] [INFO] …`, the
    runtime's as `[vr-xr] …`, hmd2's as `[vr-xr] hmd2: …`. BetterVR's settings (its ImGui ini) are
    in `/data/ps5cemu/bettervr_settings.ini`.
  - **Stop:** before every LoadExec the app turns the runtime's headset off
    (`BetterVR_PS5_Stop`), as for M2.
- **Fixed in BetterVR's Linux backend on the way** (patch 0002): `RND_Renderer::EndSession` was
  never defined on Linux. A shared library links without it, but one ELF can't. The C++ library
  differences: formatters now take their context generically, and floats are parsed with `strtod`.
- **Not done, and why:**
  - **Controllers** (M4): the hands stay where BetterVR puts them without controller poses.
  - **BetterVR's own re-presentation thread:** kept. Its submit thread drives `xrWaitFrame`, so
    Hmd2's pacing paces it; dropping it means changing BetterVR's renderer, and it can wait for a
    measured reason.
  - **The desktop mirror:** compiled but off (it is enabled only by an environment variable). The
    launcher script and Nix build aren't built.
- **Guesses the console will settle:** all of M2's, plus these.
  - **Cemu's swapchain image in BetterVR's frame:** never seen on RADV PS5. BetterVR's own capture
    path on Linux has never been run against the 0.9.21 stereo rewrite either (`FINDINGS.md`).
  - **Speed:** BetterVR's submit thread copies two 1984×2048 eyes a frame, then the compositor reads
    them again. That's GPU time the flat game didn't spend. And as there is one queue, the
    compositor's work waits behind whatever of Cemu's frame is queued before it, and `xrEndFrame`
    waits for that too. If headset frames come late (the `[vr-xr] N frames: … missed` count), this
    is the first suspect.
  - **What happens on a BetterVR error:** `checkAssert` throws, and nothing above Cemu's Vulkan
    calls catches it, so an error at start ends the app. The boot log's `[bettervr] [ERROR]` line
    before it says why.

## 2026-11-01 (second run): M2 built and released as `m2-hmd2-flat`

**Built, untested on console.** No headset or console result exists for any of this.

- **Release:** [`m2-hmd2-flat`](https://github.com/Papyszoo/botw-bettervr-ps5/releases/tag/m2-hmd2-flat).
  Same title ID and install as `m1-baseline`. Test request 2 below.
- **What it does** (`port/bettervr/VrPS5.*`, `patches/cemu/0048`, one call in
  `port/app/emulator.cpp`):
  - **Switch:** `/data/ps5cemu/bettervr.json` with `{"vr": true}`, read once, when a Wii U game
    first presents. It's a file of its own rather than a key in `ps5cemu.json`, because the app
    rewrites `ps5cemu.json` from its settings and would drop an unknown key. It adds no setting row
    and no strings. Without the file, nothing VR runs.
  - **Eye buffers:** on the first frame, three host-visible, host-coherent buffers of
    `2 × 1984 × 2048 × 4` bytes on Cemu's own `VkDevice` (device-local type preferred, as in
    `reference/eye_buffers.cpp`). They are mapped, must be 256-byte aligned, and go to `hmd2::init`
    as `Config::external`. 1984×2048 per eye is photo-splat-vr's size. The TV mirror is off, so the
    TV keeps Cemu's picture.
  - **Each TV frame**, in `VulkanRenderer::SwapBuffer` before the present:
    `wait_slot` → `begin_frame` → `acquire`. Then, on Cemu's own command buffer: blit the swapchain
    image (after the overlays and the in-game menu, so those show in the headset too) into the
    middle of one 1984×2048 eye image (60% of its width, the TV's shape kept, black around it), and
    copy that image into both halves of the acquired buffer. It ends with HLA-PSVR2's buffer barrier.
    Cemu then submits and presents to the TV, waits for that command buffer
    (`WaitCommandBufferFinished`), and calls `hmd2::submit`.
  - **Colour:** the eye image is `R8G8B8A8_UNORM` when the swapchain is UNORM (Cemu picks
    `B8G8R8A8_UNORM`), so the bytes Hmd2 samples are the TV's bytes, already sRGB-encoded. That
    matches photo-splat-vr, whose comment says the sampled bytes are sRGB-encoded.
  - **Off while VR is on:** frame generation (`SwapBuffer` skips `ps5framegen::Wanted`) and
    ReShade (`PS5_PostEffects`).
  - **Shutdown:** a game ends with a LoadExec, so Cemu's destructor never runs.
    `LeaveEndingToSystem` (before both of the app's LoadExec calls) now calls `PS5Cemu_VrStop` →
    `hmd2::shutdown()`, under the same lock as the frame calls. hmd2's notes say a title that ends
    with VrTracker2 live faults the tracker. The destructor path frees the buffers too. If the
    process is killed from the PS button, nothing runs, and Hmd2 isn't stopped.
  - **Logs:** every line starts `[vr]`, including hmd2's own (`[vr] hmd2: …`). At start there's a
    line per eye buffer and `[vr] headset up: …` (size, buffers, Hz, user, export table, FOV). Every
    600 headset frames there's a pacing line, and on failure `VR off: …` with hmd2's step and return
    code.
- **Costs and guesses to check on the console:**
  - **The wait for the GPU.** Waiting for this frame's command buffer before `hmd2::submit` holds
    Cemu's GPU thread until the GPU finishes the frame. Without VR it only waits for the previous
    frame. Expect a lower flat frame rate with VR on. If it matters, the next step is a thread that
    waits on the fence and submits.
  - **Two pacers.** `wait_slot`/`begin_frame` pace the GPU thread to Hmd2's 60 Hz slots, and the
    TV's FIFO present paces it too.
  - **TV and headset together.** photo-splat-vr in VR mode doesn't show its own picture on the TV.
    Here, RADV's VideoOut port for the TV and Hmd2's bus-32 port are open at the same time. Nothing
    has tried that.
  - **RADV versions** (`third_party/hmd2/README.md`): photo-splat-vr links PS5_Vulkan `f410e67`'s
    Mesa, and this app links PS5_Mesa `7b59ef27`. If the headset shows garbage or black, check that
    first.
  - **The picture is head-locked.** Hmd2 reprojects it with the pose it predicted, so the screen
    follows the head. It's the same image in both eyes, so it reads as far away.
- **Next:** Test requests 1 and 2. M3 starts from BetterVR's own code, which doesn't depend on
  their results, but its pacing and buffer path build on M2 working on the console.
- **Build:** `make release` with `EDEN_SKIP_SOURCE_CHECKS=1`, as for M1, on `72aa2a7`. The zip's
  SHA-256 is `4d82b1c8…1b3f`. `ps5vr::*` and `hmd2::*` are in the ELF, and its new imports
  (`sceVideoOutRegisterBuffers2`, `sceKernelDlsym`, `sceSysmoduleLoadModule`, …) are all in the
  payload SDK's system-module stubs. Adding the include directories rebuilt all of CemuCafe (647
  steps, about 16 minutes), then the link.

## 2026-11-01 (second run): M1 built and released as `m1-baseline`

Cloud session, with zlib.net and wrapdb.mesonbuild.com now allowed. **Built, untested on console.**

- **Release:** [`m1-baseline`](https://github.com/Papyszoo/botw-bettervr-ps5/releases/tag/m1-baseline),
  a pre-release on `14b0b9c`. It contains `PS5-ECAM-v5.1.1.zip` (76 MB, SHA-256 `5c0624aa…ab63`),
  `ps5cemu-v5.1.1.elf.xz` (the eboot's ELF with symbols, for `tools/symbolize-crash.py`) and
  `SHA256SUMS`. Title ID PPSA99360, so it replaces an installed PS5-ECAM. The notes say not to take
  the in-app update. Test request 1 can now use it.
- **Times** on this machine (4 cores this time, not 2):

  | Step | Time |
  | --- | --- |
  | apt packages and `pip install meson mako packaging` | 45 s |
  | `deps.py fetch` (everything, fresh container) | 2 min 56 s |
  | `tools/build-deps.sh` | 51 s |
  | `tools/build-radv.sh` (both RADV builds) | 7 min 33 s |
  | Azahar, melonDS and Cemu compiles | about 19 min, in two 9.5-minute calls |
  | Cemu's link (`ps5cemu.elf`) | **11 min 33 s**, one lld step, longer than a 10-minute shell call |
  | Eden: its RADV, FFmpeg, then Eden itself (1721 steps) | about 33 min (95 CPU-minutes for Eden) |
  | packaging | about 2 min |

  The Cemu link and the Eden build can't fit in one 10-minute shell call. They ran as background
  processes, watched from the same turn with foreground waits of 9 minutes or less.
- **Host packages the docs don't list** (`docs/BUILDING.md` names the rest): `bison`, `lld-18`,
  `ccache` (Eden refuses to build without it, or with `USE_CCACHE=0`), `libclang-rt-18-dev`
  (an Eden host tool links ASan), and CMake 3.31 at `~/hosttools/cmake-3.31.8` (Eden wants 3.31 or
  later; Ubuntu has 3.28). CMake came from Kitware's GitHub release and was checked against its
  `SHA-256.txt`.
- **Downloads the network refused, and what replaced them.** Each replacement was checked against
  the dependency's own pin:
  - **GitHub source archives** (`github.com/<repo>/archive/*.tar.gz`, HTTP 403): Eden's `deps.py`
    needs FFmpeg and fmt that way, and Eden's CMake (CPM) needs 35 more packages. All were rebuilt
    with `git archive --prefix=<GitHub's folder>/ <ref> | gzip -cn` from the pinned refs. Every one
    is byte-identical to the pin (the SHA-512 or SHA-256 matches), so Eden's own checks accepted
    them. They're in `~/.cache/prosperoeden-deps/` and in CPM's cache under Eden's prepared source.
    The script is simple to redo: fetch each ref with git, try `<repo>-<ref>`,
    `<repo>-<ref without v>` and `<repo>-<full commit>` as the folder, and keep the file only if
    its hash matches. CPM's folder is the package's `package` name in lower case where it has one,
    otherwise its key.
  - **blackbearreloaded's account is gone** (HTTP 404). Eden pins three things from it:
    - the PS5 OpenGL SDK 1.0.2, from darkxex/ps5-opengl's `v1.0.2` release, as
      `tools/build-eden.sh` says (SHA-256 matches);
    - ps5-homebrew-ui's 10 theme files at `aed286b`, from Phononenom/ps5-homebrew-ui (each SHA-256
      matches);
    - the Lapy helper at commit `57fcc19b7f57`, from Phononenom/PS5-Lapy-JB-Daemon-bbr, the only
      copy found with that commit.
- **Eden's source checks were skipped** (`EDEN_SKIP_SOURCE_CHECKS=1`, Eden's own switch). After
  Eden builds, `build-headless-native.sh` runs text checks on its sources. At this repo's base, four
  of them fail: `check-load-failure`, `check-startup-performance`, `check-native-gpu-thread` and
  `check-gpu-sync-stop`. Upstream's newest Eden patches (`patches/eden/0021`, `0022`, eden-nx2)
  changed the code they read. For example, 0022 put `Eden::Nx2::ResetForSession()` inside the block
  `check-load-failure` compiles against stubs. The sources weren't changed. This is upstream's to
  fix, and it only concerns the Switch side.
- **Publishing:** this session's GitHub access can't create releases or dispatch workflows (HTTP
  403). `.github/workflows/bettervr-prerelease.yml` does it instead: push a branch
  `staging/<tag>` based on `main`, with `release.env` (`TAG`, `TARGET` as a full commit ID,
  `TITLE`), `notes.md`, `SHA256SUMS` and the files split into 95 MB parts. The workflow joins them,
  checks them, creates the pre-release and deletes the branch.

## 2026-11-01: M1 stopped at a blocked download (zlib.net)

Cloud session, scheduled run. Nothing was built for the console and nothing was released.

- **What stopped it:** the egress proxy refuses `https://zlib.net/fossils/zlib-1.3.2.tar.gz`
  (HTTP 403 to CONNECT). Two pinned dependencies fetch it in their
  `tools/setup-native-dependencies.sh` and build a host zlib from it:
  - the PS5 Native App Boilerplate (`.deps/ps5-native-app-boilerplate`). `make deps` runs this as
    the boilerplate's setup, so `make deps`, and with it `make build` and `make release`, stop here.
    `tools/package.sh` links its host `ps5-native-tool` against this zlib too.
  - PS5_Vulkan (`.deps/PS5_Vulkan`). `tools/build-radv.sh` runs it before building RADV.

  Not retried and not fetched from anywhere else. See "Blockers" for the choices.
- **What did work** (times on this 2-core machine):

  | Step | Time | Result |
  | --- | --- | --- |
  | apt packages from `docs/BUILDING.md` (plus `bison`) | 14 s | installed |
  | `pip install meson mako packaging` | 8 s | installed |
  | `deps.py fetch`, first try | 12 s | Cemu fetched; stopped at the boilerplate's zlib download |
  | `deps.py fetch` of the other 28 items by name | 125 s | all fetched, 3.2 GB in `.deps/`; every one comes from GitHub |
  | `tools/build-deps.sh` (Boost, pugixml, RapidJSON, libzip, glslang, glm, compiler-rt builtins) | 72 s | built into `build/sysroot` |

  The PS5 payload SDK itself (a GitHub release, `ps5-payload-dev/sdk` v0.42) downloaded and
  unpacked fine. Only zlib is missing.
- **Other downloads the build will make** (read in the scripts, not run):
  - RADV's Mesa build takes zlib 1.3.1 from meson's wrap (`--force-fallback-for=zlib`,
    `.deps/PS5_Mesa/subprojects/zlib.wrap`). The source comes from `http://zlib.net/fossils/…`
    (blocked) with a fallback the wrap itself names on GitHub (`mesonbuild/wrapdb` releases). The
    wrap's patch comes from `https://wrapdb.mesonbuild.com/v2/zlib_1.3.1-1/get_patch`, with no
    fallback. Whether that host is reachable wasn't tested. If it isn't, `make radv` will stop
    there next.
  - Azahar's `DownloadExternals.cmake` names Qt and MoltenVK downloads, but those are for its
    desktop frontends, which the PS5 build doesn't build.
  - Every item in `tools/deps.json` is on GitHub.
- **Title ID: keeping PPSA99360.** Giving the fork its own ID isn't a contained change. The ID is
  in about 80 lines of code and scripts: `Makefile`, `sce_sys/param.json`, `tools/package.sh`
  (and the elevation helper it builds), `port/app/paths.h`, `port/app/system_layout.h`,
  `port/retro/runtime/ecam_bridge.h`, `port/payloads/tile_installer.c`, `tools/build-eden.sh`,
  `tools/build-retro.py`, five Eden patches (with its Lapy helper's manifest), and host tests. It is
  also in user-facing msgids, so all 30 `port/lang/*.po` files would change. Both apps would still
  share `/data/ps5cemu` (settings, logs, caches). So the M1 build replaces an installed PS5-ECAM.
  The release notes will say so.
- **In-app updater:** `port/app/updates.cpp` checks `premohq/PS5-ECAM`'s latest release. Once
  premohq publishes a version newer than 5.1.1, the app will offer it, and accepting it would put
  upstream PS5-ECAM over this fork's build. Test request 1 says not to accept it.
- **M2:** not started. This run's scheduled prompt says to start M2 only if
  `third_party/ps5xr/runtime/src/xr_vulkan.c` exists, and it doesn't. While this run was going,
  `e6b5b06` changed `PLAN.md` so that M2 uses `third_party/hmd2/` and doesn't need PS5XR. The prompt
  doesn't know that yet (see Blockers). M2 also builds on the same `make release`, so it couldn't be
  built or released past the zlib blocker anyway.

## 2026-11-01: repo set up

- **Base:** `main` is premohq/PS5-ECAM `269e273` plus this project's docs,
  `patches/bettervr/0001-*.patch` and the `third_party/ps5xr/` placeholder.
- **Next:** M1, the baseline build and release (see `PLAN.md`).

## Open test requests

### Test request 5: BotW VR as its own app (M5)

**Which build.** `m5-botw-vr`. Set it up as `INSTALL.md` says: **`PPSA98720` in the HEN's app
jailbreak list**, the game in `/data/botwvr/cemu/games`, update v208 installed.

**Try:**

1. **It starts:** "Breath of the Wild VR" on the home screen, next to PS5-ECAM. It opens on the Wii
   U library alone, with no 3DS, DS or Switch side.
2. **Its own data:** `/data/botwvr/launcher` and `/data/botwvr/cemu` were made; PS5-ECAM's
   `/data/ps5cemu` and `/data/cemu` were not touched. Settings > Diagnostics: JIT memory there (the
   HEN list worked).
3. **Tile and background:** with BotW in the library, wait a minute, then look at the app's tile and
   background on the home screen. If they still show the visor, restart the console, then look
   again. If still the visor, have ShadowMount Plus put the app on the home screen again (rescan).
   Say at which step, if any, the game's pictures appeared.
4. **The game:** BotW flat, then Test request 4 (VR with the Sense controllers), with
   `bettervr.json` now in `/data/botwvr/launcher`.

**Send back:** what each step showed; from `/data/botwvr/launcher/logs/boot.log`, the `[vr]` lines
(the tile ones say "home screen tile" and "home screen background", written or not), `[paths]`
lines, any `[crash]`, and the ones Test request 4 asks for.

### Test request 4: the Sense controllers in BetterVR (M4)

**Which build.** `m4-sense`. It includes M3: do Test request 3's steps 1-4 first (flat, FPS++, M2's
screen, then BetterVR with `{"vr": true}`). If those fail, this one can't work, so send that
instead.

**Try:**

1. Turn both Sense controllers on before starting the game. Start Breath of the Wild with
   `{"vr": true}`.
2. **Hands:** in game, look at your hands. Move each controller: does the hand follow, in the
   right place, the right way round, and pointing the right way?
3. **Buttons:** walk with the left stick, turn with the right one. Jump (Circle), run (Cross), and
   crouch (L3). Open the inventory (R3). Grab something (L1/R1). Draw and use a weapon with the
   right trigger, and swing it.
4. **Rumble:** take a hit or attack something. Does a controller vibrate?
5. **If a hand points wrong by a fixed tilt,** add `"gripPitch": 30` (or −30, 60, …) and, for the
   pointer, `"aimPitch"` to `bettervr.json`. Restart, and say which values looked right.
6. Turn one controller off and on again mid-game: does it come back?
7. Quit to the library, then Settings > Diagnostics > Copy logs to USB.

**Send back:**

- What each step did: hands (position, rotation, mirrored?), each button, swings, rumble.
- From `boot.log`: every `[vr-xr]` line, in particular "Sense controller open …",
  "VrTracker2 registration …", "Touch profile: N bindings …", "Sense buttons 0x… stick …"
  (the first 40 changes) and "vibration -> 0x…". Also the `[bettervr]` lines and any `[crash]`
  lines.

**What it decides:**

- **Hands that follow and buttons that act:** M4's controller part is done. What's left of M4 is
  the frame-rate locks and performance.
- **Buttons that act but hands that don't move:** VrTracker2's controller poses
  (`register_controller` returned false, or `controller_pose` reports nothing). This is the part
  hmd2 marks unverified.
- **Hands that move wrongly:** the pose's axes. A fixed tilt is `gripPitch`/`aimPitch`; anything
  else is a code change, and the "Sense buttons" lines plus a description are enough to make it.
- **No input from the controllers at all:** the button lines say whether `scePadVrControllerReadState`
  reads anything.

### Test request 3: Breath of the Wild in stereo through BetterVR (M3)

**Which build.** `m3-stereo`. It includes M2: run Test request 2's headset part with it too (step 3
below), since M3 can only work if M2's flat screen does.

**Install:**

1. Download `PS5-ECAM-v5.1.1.zip` and `SHA256SUMS` from the pre-release and check them
   (`sha256sum -c SHA256SUMS --ignore-missing`).
2. Copy the zip's `PPSA99360` folder over `/data/homebrew/PPSA99360`. It replaces your PS5-ECAM.
   Don't accept an in-app update.

**Try, in this order** (each step tells the next one something):

1. **Flat:** no `/data/ps5cemu/bettervr.json`. Start Breath of the Wild and play a minute, then
   quit to the library. It should play as before, with no `[vr]` lines in the boot log. If
   `graphicPacks/bettervr` is there from an earlier try, it should be gone after this start.
2. **FPS++:** in Breath of the Wild's graphic packs (Options on the game > Graphic packs), check
   that **FPS++ is on**. BetterVR needs it.
3. **M2's screen, as a baseline:** put `{"vr": true, "bettervr": false}` in
   `/data/ps5cemu/bettervr.json` (FTP). Restart the app, start the game, and check that the headset
   shows the flat screen (Test request 2). Quit to the library.
4. **BetterVR:** change the file to `{"vr": true}`. **Restart the app** (the graphic pack is put in
   place when the app starts). In the game's graphic packs, **Better VR** should now be listed and
   on, next to FPS++. Put the headset on and start the game.
5. Watch: the title screen, loading, then walking around Link's area for a minute or two. Look
   around and lean, and open the inventory once.
6. Quit to the library from the in-game menu, then Settings > Diagnostics > Copy logs to USB.

**Send back:**

- **Each step:** what the headset showed. A 3D picture with depth? Does it follow the head
  (turning and leaning)? Is it the right way up, and are left and right the right way round? Is
  the HUD visible, and where? Flicker, judder, colours?
- **TV:** what it showed in step 4.
- **From `/data/ps5cemu/logs/boot.log`:** every line with `[vr]`, `[vr-xr]` or `[bettervr]`, the
  `[perf]` lines while it ran, and any `[crash]` lines. If it crashed or froze, the whole boot.log
  and the step it happened in.
- **If BetterVR didn't start:** the last `[bettervr] [ERROR]` line, which says why.

**What it decides:**

- **Stereo that follows the head:** M3 is done; M4 (controllers, frame-rate locks, performance) is next.
- **Upside down, mirrored, or turning the wrong way:** the axes in `runtime.cpp` (a sign or a
  quaternion order), an easy change.
- **A flat or broken picture with `[vr-xr] headset up`:** BetterVR's capture of Cemu's frames. The
  `[bettervr]` lines about "Clearing color image" and swapchains say how far it got.
- **Nothing in the headset, with M2's screen working in step 3:** the runtime's frame loop. The
  `[vr-xr] N frames:` pacing lines say whether frames reach Hmd2.

### Test request 2: the TV's picture in the PS VR2 headset (M2)

**Which build.** [`m2-hmd2-flat`](https://github.com/Papyszoo/botw-bettervr-ps5/releases/tag/m2-hmd2-flat).

**Install:**

1. Download `PS5-ECAM-v5.1.1.zip` and `SHA256SUMS` from the pre-release and check them
   (`sha256sum -c SHA256SUMS --ignore-missing`).
2. Copy the zip's `PPSA99360` folder over `/data/homebrew/PPSA99360`. It replaces your PS5-ECAM.
   Don't accept an in-app update.
3. **First, flat:** start Breath of the Wild without the file below, play a minute, and quit to the
   library. This shows the build still works flat (boot.log then has no `[vr]` lines).
4. With FTP (as you copy the app), put a file `/data/ps5cemu/bettervr.json` on the console
   containing exactly `{"vr": true}`.

**Try:**

1. Connect and turn on the PS VR2, and put it on (or hold it so its sensor sees a face).
2. Start Breath of the Wild (v208) from PS5-ECAM. The headset should show the game's TV picture as
   a screen in the middle of the view, with black around it, and follow your head.
3. Play about 2 minutes: the title screen, then walking around. Open the in-game menu once; it
   should show in the headset too.
4. Look at the TV: does it still show the game?
5. Quit to the library from the in-game menu. Does the headset go back to the PS5's own VR view?
   Does the library come back?
6. Settings > Diagnostics > **Copy logs to USB**.

**Send back:**

- **Headset:** did a picture appear? Is it stable, or flickering, torn or garbled? Is it black,
  green or wrong in colour? Roughly how big and sharp is it, and does it follow your head?
- **TV:** did it still show the game? Any difference from flat?
- **Quit:** did quitting work, and what did the headset show after?
- **From `/data/ps5cemu/logs/boot.log`:** every line with `[vr]` (including `[vr] hmd2: …`), the
  `[perf] … fps over 10 s` lines while VR was on, and any `[crash]` lines. If it crashed, also send
  the whole boot.log.
- **The flat run in Install step 3:** did it play as before? Two of its `[perf]` lines are enough.

**What it decides:**

- **A stable picture:** M2 is done, and M3 (BetterVR's stereo and head tracking) can start.
- **No picture, with `[vr] headset up`:** the eye-buffer hand-off. Check the RADV difference
  (`third_party/hmd2/README.md`) and the image format.
- **`VR off: hmd2 did not start`:** its step and return code say which Hmd2 call refused. The TV's
  VideoOut being open at the same time is the first suspect.
- **A crash at the first frame:** the `[crash]` lines with `ps5cemu-v5.1.1.elf.xz`.

### Test request 1: flat Breath of the Wild frame rate (M1)

**Which build.** [`m1-baseline`](https://github.com/Papyszoo/botw-bettervr-ps5/releases/tag/m1-baseline).
PS5-ECAM **5.1.1**, premohq's release, should give the same Wii U numbers, so it will do too.
Between 5.1.1 (`1e8da2d`) and this repo's base (`269e273`), the Cemu pin, the RADV pin and
`patches/mesa` are the same. The only change on the Wii U side is frame generation (#94), which is
off for this test. That comes from reading the git history, not from a console. Say which build
you used.

**Install** (only for `m1-baseline`; skip this for 5.1.1 you already have):

1. Download `PS5-ECAM-v5.1.1.zip` and `SHA256SUMS` from the `m1-baseline` pre-release and check the
   checksum (`sha256sum -c SHA256SUMS --ignore-missing`).
2. This build keeps title ID PPSA99360, so it **replaces** your installed PS5-ECAM. Copy the zip's
   `PPSA99360` folder over `/data/homebrew/PPSA99360`. Your data in `/data/ps5cemu` stays.
3. If the app offers an update, don't take it: that would install premohq's release over this one.

**Measure:**

1. Settings > Graphics > Frame rate: **120 Hz output on**, **Frame pacing off**, **Frame generation
   off**. Without 120 Hz output the display runs at 59.94 Hz, and since FIFO is the only present
   mode, the game can't go above 60. If your TV has no 120 Hz, say so and run it anyway.
2. ReShade off.
3. For the GPU-busy figure, add `"profile": true` to `/data/ps5cemu/ps5cemu.json` (it's only read
   from that file). With it, every `[perf]` line has `; GPU busy N%` after its draws a frame, and
   the sampling profiler writes `logs/profile.bin`. Without it, the lines have no GPU figure.
4. Start Breath of the Wild (v208). In the graphic packs (Triangle on the game in the library, or
   the in-game menu), set **FPS++ on, with its limit at 120 or higher**, and keep your usual
   resolution. Note which resolution you used.
5. Play about 60 s in **Kakariko Village or Hateno Village**, then about 60 s of **a fight on
   Hyrule Field**. Write down which place, and roughly when each part started.
6. Settings > Diagnostics > **Copy logs to USB**.

**Send back:**

- From `/data/ps5cemu/logs/boot.log`, every `[perf] … fps over 10 s; …` line from the game (FPS,
  long frames, GPU-thread waits, `GPU busy N%`, draws a frame). Also the `[vulkan] VideoOut surface …
  at … Hz` and `[vulkan] frame pacing …` lines.
- The build you used, the resolution, and the two places.
- What the frame rate felt like, if it differed from the log.

**What it decides:** as a rough go/no-go, VR will run at about half the uncapped flat frame rate in
those spots (`PLAN.md`, M1). By that rule, about 80 or more flat makes a 40 fps lock in VR likely,
and under 60 flat means the open world will be hard to hold at 40.

## Blockers

None open. Resolved this run: zlib.net is allowed now, and the M2 prompt follows `PLAN.md`.
If a scheduled task still has the older prompt (the one that waits for PS5XR and `m2-xr-quad`),
update it to follow `PLAN.md`.
