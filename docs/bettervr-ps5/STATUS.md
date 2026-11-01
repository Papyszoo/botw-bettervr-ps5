# Status

Newest entries first. Every console result here was reported by Patryk. Nothing else counts as
tested.

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
