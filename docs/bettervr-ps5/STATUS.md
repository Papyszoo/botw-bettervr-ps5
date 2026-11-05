# Status

Newest entries first. Every console result here was reported by Patryk. Nothing else counts as
tested.

## 2026-11-05 (fourth run): the XR input reaches the game, the view's aspect, and the Graphics pack off by default

**Not built, not run.** This merges the game branch (`ae0a9ad`, BetterVR patch 0008) and the
performance branch (`851503c`, patch 0009 with the `graphicsPack` and `reproject` switches) into
`fix/vr-first-frame-sigill`. Both branches added a 0008; the overlay patch keeps 0008 and the aspect
patch is 0009, so the series has one file per number. `patches/bettervr` 0001-0009 apply in order on
BetterVR's pin `2e06b31` through `tools/bettervr-patches.sh apply`, on a fresh clone and on a tree
with 0001-0007 (`applied 2 new patches`). `port/bettervr/xr/runtime.cpp` passes
`clang++ -std=c++20 -fsyntax-only` against the pinned OpenXR 1.1.49 and PS5_Mesa's Vulkan headers,
with the pre-existing `-Wreturn-type-c-linkage` warning on `ReadAction`. Nothing was compiled for the
console, packaged or run.

**Why the in-game Sense never worked (logs5; proven by the code and the log).** `hook_InjectXRInput`
began with `renderer->EnsureFrameInputLatched(); auto* imguiOverlay = renderer->m_imguiOverlay.get();
if (!imguiOverlay) return;`. `m_imguiOverlay` is made only in `framebuffer.cpp` under `_WIN32`, and
the Linux backend's `ImGuiOverlay` methods are stubs, so this port never creates it: the hook
returned before the Sense sticks and buttons, the hand gestures, the game state, the haptics and the
motion bridge. The log agrees: both poses and buttons reached the runtime (`Sense pose from
VrTracker2` at 18.5 s, `hands posed 1 1`), but no Sense input reached the game and no `[diag]` line
ran. Patch 0008 makes the overlay optional; the mod menu is skipped without it, so it cannot take
input and the Plus hold no longer toggles a menu that cannot exist. The hook ORs the XR buttons into
the DualSense's own VPAD and adds the XR sticks (the upstream merge), so centred Sense sticks add
zero and do not mask the DualSense; with both sticks pushed the sum can exceed 1, as upstream. The
right Sense's motion replaces the DualSense's motion when its pose is valid; at rest the game's
motion input follows the Sense, which is the upstream design but new on this console. A hand that
`xrLocateSpace` could not place is now left out of the body-slot gestures (its zeroed location is
the tracker's origin, where the waist slot matches), so a grip press cannot open a slot with a hand
that is not tracked. The hook runs on the PPC thread; `m_input` and `m_gameState` are copied under
their own `AtomicState` mutexes, no lock is held across another, and the runtime's haptic lock is
taken only while a rumble is active. The `[diag] VPADRead hook` line is one line per 5 s; the other
`[diag]` block keeps its 900-line cap.

**What the new path still does not have.** BetterVR's own settings (`bettervr_settings.ini`) are
loaded in the ImGui overlay's constructor, which is Win32-only: the log has no `VR Settings Loaded`,
so the hook, the gestures and the cameras use `ModSettings`' defaults and Settings > VR's BetterVR
options (the ini) have no effect; its `bettervr.json` options (3D/flat, the Sense angles) do.
This was true before 0008 but only now does the input path depend on those settings; not changed in
this build. The gesture comparison also uses the head pose the renderer lifts to 1.65 m while the
hand poses are the tracker's (logs5: hands y 0.59-0.61, head 0.98); the shoulder and mouth radii are
then about 0.7 m short, so those two gestures may not fire. This needs a decision about which frame
they should use, so it is left for the next console log, not changed here.

**The view after the Graphics pack (logs5; proven by the code).** The pack was active at 1280x1320
and the view was offset. BetterVR's game camera projects for `m_gameRenderAspectRatio`, which the
Windows path sets from the image and the Linux path left at 16:9, while the crop already took the
aspect from the image. Patch 0009 sets it from the display 3D target, not from a target the 3D path
refuses (its own "skipped non-display target" log line), and does so on every 3D clear, so it
follows the pack. At 1280x720 the value is the same 16:9 as before.

**Graphics pack off, and reprojection.** `"graphicsPack": 1` in
`/data/botwvr/launcher/bettervr.json` turns the pack on; without it the pack is off. That is the file
`BetterVR_PS5_Setting` reads (`ps5paths::Root()/bettervr.json`, and `Root()` is
`/data/botwvr/launcher`, as the log's line says). On a console whose `settings.xml` holds the pack
active with a preset, `PS5Cemu_BetterVRApplyMode` sees the exact name `Graphics (Modified Version For
BetterVR)` enabled while `graphicsWanted` is false and toggles it off; `SaveState` writes the
`_disabled` entry because the pack's rules have `default = 1`. The launcher now reads
`bettervr.json` again before a write (`vr_settings.cpp`), so a `graphicsPack` or `reproject` key
added over FTP while the app runs survives Play in VR/TV and Settings > VR; without that, the copy
the home screen read at its first draw would have dropped it. Turning the pack off drops its saved
preset; turning it on again starts at the 0.5x default. This is code reading; the next log's
`graphic pack … turned off for VR` and Cemu's `Activate graphic packs` decide. Reprojection
stays on; `"reproject": 0` in the same file submits every frame as rendered from the display time's
pose, as before `54e7944`, and the session-start line prints `reprojection … on/off`.

**Gravity is still open, and the hook is not cleared.** The game report called gravity independent
of this hook because the hook returned early in every session that showed it. That inference does not
hold: the hook body never ran in those sessions, so the newly enabled path (XR input and game state,
gestures, haptics, the motion bridge) has never been present with the symptom and may be a cause or
change it. The guest clock is ruled out (`[clock]` ratio 1.000, 30.9 game frames per guest second).
No cause is claimed; Test request 12 asks for the lines that would show it.

## 2026-11-05 (third run): the render pose, the Graphics pack, and the frame-rate session

**Not built, not run.** This is the merge of the game branch (`c85f3ca`, `a12b994`) and the
performance branch (`abb880d`, `eae7d67`, `54e7944`) into `fix/vr-first-frame-sigill`, with the
review of the changes since `40f77a0`. `port/bettervr/xr/runtime.cpp` passes
`clang++ -std=c++20 -fsyntax-only` against the pinned OpenXR 1.1.49 and PS5_Mesa's Vulkan headers;
`patches/bettervr` 0001-0007 apply in order on BetterVR's pin `2e06b31` through
`tools/bettervr-patches.sh apply`. Nothing was compiled for the console, packaged or run.

**The render pose (`54e7944`).** BetterVR adds a seated-height offset to the views it renders from
before it submits them, so the old x, y and z match never held and `xrEndFrame` sent Hmd2 the pose of
the display time instead of the render pose: no reprojection of an image 50-80 ms old. The match is
now x and z only, the ring holds 256 located views (about 1 s at BetterVR's 250 Hz pose thread), and
the 600-frame `[vr-xr]` line ends with `render pose found N, not found M`. This is a code reading,
not a console result: whether Hmd2's reprojection then follows the head is Test request 11.

**The frame rate and the resolution (logs3, logs4).** The playable stretch is 31.6-34 fps at about
9,000-10,300 draws a frame; the later 60 fps / 1,800-2,900-draw stretch is a light screen, not the
world. The GPU thread is 70% busy and waits on the PS5 GPU about 0%, so the emulated CPU side is the
limit (inferred; `"profile": true` would decide it). No Graphics pack was active, so BotW drew
1280x720 16:9 and about 755x720 of it was stretched over each 1984x2048 eye. `0007` ships BetterVR's
Graphics pack with the PS5 eye sizes, 0.5x (1280x1320 per eye) by default; the app copies it to
`graphicPacks/bettervr` and turns it on for VR and off for the TV. Not run on the console; see Test
request 11 for what to look for.

**The game run (logs3, logs4).** The in-game Sense pads open, their buttons and sticks arrive, and
BetterVR merges them with the DualSense; whether VrTracker2 gives them a pose was still unknown, so
the runtime now logs the first pose (or its absence) and the `[vr-xr]` line carries the head position
and which hands are posed. Because the launcher now opens the pads first, the runtime takes that
handle instead of opening a second one, and closes only a handle it opened itself; otherwise VR's
stop could close the launcher's pad. The port's Wii U in-game menu still reads only the DualSense; `a53b97b`
changed the launcher's menus, not that one. Nothing in the code read so far advances game time twice
per frame or queues right-eye work, and FPS++'s speed factor at 31.6 fps is 0.95; the cause of the
slow/late physics is **not known and remains open**, with the tracker's origin against the floor as
an unproven candidate. `[clock]` and `[diag]` (patch `0006`) now log the guest clock against host
time, Link's position against the sticks and FPS++'s values.

**The 180FPS preset is a console-only edit, not in this repository.** It removes Cemu's
`Mismatching vsync frequency 180` line by making FPS++ agree with BetterVR's own 180; it changed
nothing in the observed frame rate and is not a fix.

**Installing over an old build.** The 0005 build was installed at 14:11 through Prospero Manager, not
copied over by hand. Copying a build over `/data/homebrew/PPSA98720` does not refresh the
`param.json` the system registered for the app: the PS VR2 flags in `/system_data/priv/appmeta` and
`/user/app` stayed stale from `m5` until the tile was removed and ShadowMount Plus re-added it. That
re-add is what made the tile's icon right and cleared the Sense intercept bit in the logs (set at
51.927 s, clear at 53.542 s in the m9 session). `INSTALL.md` section 5 now says so.

## 2026-11-05 (second run): the reported crash was m9; 0005 then passed it; the Sense controllers were never linked

**Reported by Patryk:** the Sense controllers still did not move the launcher's menus, and Play in VR
crashed again.

**The crash he reported (13:58) was m9, not 0005.** At that time the console's
`/data/homebrew/PPSA98720/eboot.bin` was 45,314,393 bytes, SHA-256 `d6b34476…` (m9's), and every file
in the folder still had its 10:51 install time. The crash was the same `0x800102040` SIGILL:
`OpenXR::UpdateActions` → `__atomic_load(size=0x6c0)` on the submit thread (`rdi=0x6c0`, return
`0xaa3a22`), which only the m9 build can emit. **0005 had not been installed when that session ran.**

**The 0005 build was installed at 14:11 through Prospero Manager and passes the crash.** `eboot.bin`
is now 45,314,777 bytes, SHA-256 `543407d3…` (mtime 14:11:12). Its 14:12 session
ran for at least 312 s: `OpenXR session begun successfully!`, `Hmd2 live at frame 2`, **6,000
headset frames by 121 s (5,999 flips shown, 0 errors, 0 missed, 0 queue-full, 0 repeated), 60.0 fps
by 262 s**, about 10,335 draws a frame, with no `[crash]`, no `VR off` and no `session ended`;
`worn 1` for the first 72 s. **Test request 9 passes for the crash on console.** The headset/TV
picture is not reported by Patryk; the log shows frames were shown, not what they looked like.

**The Sense controllers: `SensePS5.cpp` was never linked.** `port/ui/input.cpp` declared
`PS5Cemu_SenseMenuButtons` weak for the PC preview, and `CemuPS5` is a static library: a weak
reference does not pull `port/bettervr/SensePS5.cpp` out of the archive. Both release ELFs (m9's and
0005's) carry the symbol as `w` (weak undefined), have no `SensePS5.cpp` DWARF unit and none of its
strings, and neither boot log has a "Sense controller … menus" line: the menus never tried to open a
pad. The DualSense path is separate and works. The 0005 in-game runtime opened both Sense pads with
the same user (`0x161c8e85`) and read L1/R1 (`0x400`/`0x800`) and stick changes, so the pads and the
user are fine. The `0x80000000` intercept bit appeared at 51.927 s and cleared at 53.542 s: it is the
system menu's ownership bit, not stuck and not a required init.

**The fix (not in 0005, not built):**

- `port/ui/input.cpp`: the weak declaration and the null-check exist only under
  `PS5CEMU_LAUNCHER_PREVIEW`; the console has a strong reference, which pulls `SensePS5.cpp` in.
  An ELF link test with a static archive confirmed the mechanism (weak: member left out; strong:
  linked), and both files pass `clang++ -std=c++20 -fsyntax-only`, with and without the preview
  define.
- `port/bettervr/SensePS5.cpp`: the first failed open per hand now logs the user and both
  `scePadOpen`/`scePadGetHandle` return codes; the first failed read logs its code and the connected
  bit; the success line says the pad opened, not that the focus moved. Whether the console allows
  `scePadOpen` types 3/4 before a VR session is the next unknown; the runtime's opens happen only
  after Hmd2 starts, and 0005 never tried the menu path.

**Backlog:** the same weak reference hides `PS5Cemu_BotwTileArt` (strong in m5, weak since m8), so
the home screen tile and background have not been written by m8, m9 or 0005; not fixed here.

## 2026-11-05: the first VR frame's SIGILL, and the large input state through a mutex

**Reported by Patryk** (`m9-launcher`, VR on): the session reached `OpenXR session begun
successfully!`, the submit thread's bootstrap and both Sense controllers, then `[crash] signal 4 at
0x800102040`; no frame statistics and no clean shutdown.

**The crash, symbolized.** The release's `ps5cemu-v5.1.1.elf.xz` (SHA-256 `3acc3f47…`, the ELF
`6ed7bfba…`) and `BotW-VR-v5.1.1.zip`'s eboot are the installed build (the zip's eboot is
byte-identical to the console's, SHA-256 `d6b34476…`). `tools/symbolize-crash.py` places the
eboot 0x400000 above the ELF; the chain is `SubmitThreadMain` → `OpenXR::UpdateActions` →
`__atomic_load`. The machine context is the call's arguments: `rdi=0x6c0` (size),
`rsi=0x20093cf950` (source), `rdx=0x4062d2810` (destination), `rcx=5` (seq_cst); the first stack
word `0xaa3a22` is the instruction after the `__atomic_load` call at `0xaa3a1d`. That call is
`InputState newState = m_input.load();` (`openxr.cpp:564` in the built source; 522 in the pinned
upstream before `0001`–`0004`), where `m_input` is `std::atomic<InputState>` and `InputState` is
0x6c0 bytes.

**The cause (proven for the call, inferred for the instruction).** libc++ lowers a non-lock-free
`std::atomic<T>` to `__atomic_load`/`__atomic_store` with `sizeof(T)`. The ELF has exactly 19 calls
to those two imports; every one passes 0x90 (`GameState`) or 0x6c0 (`InputState`), and none passes
16. The clean-room `sce_module/libc.prx` exports neither NID (`+iy+BecyFVw`, `sV6ry-Fd-TM`), and
the app's `DT_NEEDED` names `libSceLibcInternal.sprx`; a public 3.20 `libSceLibcInternal` stub list
has both, so the imports bind to the system `libSceLibcInternal`. The call traps there; that it
rejects the 0x6c0 size rather than every call is inferred: the module is encrypted, so the
instruction cannot be read, and no earlier session demonstrably ran a generic `__atomic_*` call
(flat play had VR off; m5–m6 VR never reached OpenXR). `0x800102040` is neither the eboot nor
`libc.prx`: in `libc.prx` offset 0x102040 is read-only zero data (its text ends at 0xc8092), while
`libSceLibcInternal`'s text is 0x124e2e bytes. The address is in the system-module range; that
the module starts at `0x800000000`, making the offset 0x102040, is inferred, since the base is
not logged. The call, the size and the arguments are proven.

**Not the causes:** the first `hmd2::submit`/`sceVideoOutSubmitFlip` (the stack and registers are
`UpdateActions`/`__atomic_load`, and no frame submit line was reached); a wild indirect call
(`__atomic_load` is the normal PLT import, with the correct arguments; the `query_pose` address on
the stack is a stale return address from the bootstrap `xrWaitFrame`); a missing symbol (no "not
resolved" line; `__atomic_load` is found); the Sense `controller_pose` path (skipped by the
0x80000000 intercept bit).

**The fix:** `patches/bettervr/0005-PS5-the-large-input-and-game-states-through-a-mutex.patch`
replaces `std::atomic<InputState>`, `std::atomic<GameState>` and `std::atomic<RumbleParameters>`
with a mutex-backed `AtomicState<T>`; `load()`, `store()` and each state's layout stay the same
(the wrapper adds the mutex). `m_inputCameraRotation` (16 bytes) is left alone: it has no use in
this source and no generic call in the ELF, so it is not part of this crash. `m_rumbleParameters`
(24 bytes) has no use today either; the patch wraps it for the same class.
The patch applies after `0001`–`0004`; **not built in this session**, so no console result.
Test request 9.

## 2026-11-05: m8-botw-home released, and m9: a launcher for one game

- **[m8-botw-home](https://github.com/Papyszoo/botw-bettervr-ps5/releases/tag/m8-botw-home)**
  (`84f3f1a`, built, untested on console): m7's VR fix, BotW's page as home with Play in VR and Play
  on TV, the Sense controllers in the menus.
- **Patryk, after m8 was planned:** the UI still reads as a repurposed emulator; he wants a launcher
  for one game, with its settings in it, and BetterVR's configuration without editing JSON.
- **m9, on `main` (`486695d`):**
  - **Home** (`frontend/shell/home.cpp`) in place of the Library: the game's own boot screen across
    the screen (from his dump), its name with a VR mark, the update and DLC, and Play in VR, Play on
    TV, VR settings, Graphic packs, Settings. Without the game: where to put it, and Look for it
    again. Everything that went back to the Library comes back here.
  - **Settings > VR** (`frontend/shell/vr_settings.cpp`): BetterVR's options, written to its own ini
    (`bettervr_settings.ini`, section `[BetterVR][Settings]`, read as VR starts), and the app's
    (3D or flat screen, the Sense hand and aim angles) in `bettervr.json`. Every other key and
    section in both files is kept.
  - **Settings** has this game's pages only, VR first; no sides, themes, play time or PS5-ECAM
    updates. No What's new or Make it yours; the Setup check only when something fails.
  - **A theme of its own**, BotW VR, deep teal like the home and the tile art, the default.
  - The 61 new strings in all 27 full languages (`lang.py check` passes). The PC preview showed every
    screen, in English and Polish (`docs/bettervr-ps5/screens`).
  - Not changed: the in-game menu (touchpad + Options) is still PS5-ECAM's.

## 2026-11-05: the green screen's cause (m6-vr-diag's log), m7-vr-layer, and the launcher for BotW

**Reported by Patryk** (`m6-vr-diag`, VR on): the green screen again. His boot log:

- `Cemu's Vulkan device through BetterVR's layer: 0`, `Cemu takes its device functions from
  BetterVR's layer`, `Cemu's first vkCmdClearColorImage`, and three `Cemu clears with BetterVR's eye
  colour` lines: the graphic pack works, and Cemu's clears reach the stand-in.
- No `[bettervr] [INFO] PS5: the layer's ... runs` line, for any hook: none of BetterVR's device
  hooks ran.

**The cause:** vkroots' `vkGetInstanceProcAddr` answers instance functions only and passes every
other name, `vkGetDeviceProcAddr` among them, to the next one down: RADV's. So Cemu's device
functions were RADV's, and BetterVR never saw a clear, never started OpenXR, and the eye colours
stayed on screen. A loader takes device functions from the layer's `vkGetDeviceProcAddr`, the one
negotiation gives; the stand-in now does (VulkanLayerPS5.cpp, `194b6e5`), and routes the four device
functions Cemu takes through the instance (`vkQueuePresentKHR`, which BetterVR hooks,
`vkDeviceWaitIdle`, `vkDestroyDevice`, `vkSetDebugUtilsObjectNameEXT`) through it as a loader's
trampolines would.

**[m7-vr-layer](https://github.com/Papyszoo/botw-bettervr-ps5/releases/tag/m7-vr-layer)** (built,
untested on console): that fix alone. Test request 7.

**Next build (m8), on `main`:** what Patryk asked for besides VR.

- **BotW's page is the app's home:** it opens on start, with **Play in VR** and **Play on TV** in
  place of Play (`frontend/shell/hub.cpp`). They set `bettervr.json`'s `"vr"` and start the game; the
  file's other settings stay.
- **No restart to switch:** BetterVR's graphic pack is always installed, and turned on (VR) or off
  (TV) as Breath of the Wild starts, with FPS++ on for VR (`PS5Cemu_BetterVRApplyMode`).
- **The Sense controllers move through the menus:** their buttons and, as they have no d-pad, their
  sticks (`port/bettervr/SensePS5.cpp`, `ui/input.cpp`).
- **The home screen pictures** log what they were made from, or why not: `m5`/`m6` logged nothing
  there.
- **The launcher's PC preview** links again (stand-ins for ES-DE and the libretro cores), and showed
  the page with the new buttons (`build/shell-preview`).

## 2026-11-05: Patryk's first console results (m5-botw-vr), and m6-vr-diag

**Reported by Patryk on his PS5** (firmware 13.59, `m5-botw-vr`, BotW EUR v208 with DLC, unpacked):

- The app installs and starts as "Breath of the Wild VR", with its data in `/data/botwvr`. No HEN
  was asked: JIT and `/data` were there without one.
- **Flat: works**, with FPS++ at 120: a steady 60 fps in game.
- **VR (`{"vr": true}`): a green screen.** The game ran at 60 fps on 98 draws a frame and never got
  further.
- The tile has no PS VR2 mark.
- The Sense controllers don't move through the app's menus; only the DualSense does.
- He finds the emulator's launcher too big for a BotW app: he wants something made for BotW, keeping
  the community graphic packs, but only BotW's.

**What his boot log says about the green screen.** BetterVR started, created the instance and the
device through its layer, then logged nothing more. No `[vr-xr]` line at all: BetterVR never called
`xrCreateInstance`. It does that on the first `vkCmdClearColorImage` its layer sees
(`hooking/framebuffer.cpp`). Its README: a green screen means the layer isn't seeing the game's
clears, so the eye colours the graphic pack clears with stay on screen. Reading the code
(VulkanLayerPS5.cpp, vkroots, Cemu's PS5 loading) didn't show why the clears miss the layer.

**[m6-vr-diag](https://github.com/Papyszoo/botw-bettervr-ps5/releases/tag/m6-vr-diag)** (built, untested on console; zip SHA-256 `bcaac3d7…db6d`):

- **The PS VR2 attributes** in `param.json`: `attribute 0x62000000`, `attribute3 0x40c0c40` (photo-
  splat-vr's and HLA-PSVR2's `0x4080c40`, with PS5-ECAM's own bits). photo-splat-vr's research:
  without them "Hmd2 reported the headset not worn and never consumed a frame". So even with the
  layer fixed, `m5-botw-vr` couldn't have shown a frame in the headset.
- **Boot-log lines** for where Cemu's Vulkan calls go: the device through the layer, Cemu's
  `vkGetDeviceProcAddr` (the layer's, or another instance's), its first clear and BetterVR's eye
  colours on their way to the layer (VulkanLayerPS5.cpp); and in BetterVR (patches/bettervr/0004),
  the first call of each hook, and VR's start or the exception that stopped it.

## 2026-11-04: M5, BotW VR as an app of its own (Patryk's choice)

**Built, untested on console.** Patryk chose an own title ID for the whole app and the other
emulators taken out.

- **Release:** [`m5-botw-vr`](https://github.com/Papyszoo/botw-bettervr-ps5/releases/tag/m5-botw-vr),
  a pre-release on `6bf0a6a`. `BotW-VR-v5.1.1.zip` is 45 MB (PS5-ECAM's was 77 MB), SHA-256
  `7b6377bb…2f26`. Test request 5 below.
- **Build time:** about 20 minutes: the port's 72 changed files, then Cemu's link (12 minutes).
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

### Test request 12: the XR input, the view's aspect, and the Graphics pack switch (this build)

**Which build.** The next console build of `fix/vr-first-frame-sigill`: BetterVR patches 0008 (the
overlay optional, so the XR input reaches the game) and 0009 (the game camera's aspect, the
`graphicsPack` and `reproject` switches). It is not m9 and not an earlier artifact.

**If upgrading from a build before m6**, remove the home-screen tile and let ShadowMount Plus add the
app again after copying it; a copy over `/data/homebrew/PPSA98720` alone leaves the old registered
`param.json` (INSTALL.md section 5).

**Try, in this order:**

1. **Default: pack off.** Start the app and Breath of the Wild in VR, and play about 3 minutes in the
   world. Walk with the DualSense; press every Sense button (A/B/X/Y, L1/R1, both sticks, the grips,
   the triggers and Plus); and hold a grip while raising each hand to your shoulder, your waist and
   your mouth. The view should be the 1280x720 one from the earlier builds, and the first-person
   hands should be at the controllers (say if they sit at the waist/knees instead). The boot log
   should have a `[diag] VPADRead hook: N calls in 5 s` line every 5 s, and `[diag] calls=…` lines
   while you move.
2. **Pack on.** Quit to the library and put `{"vr": true, "graphicsPack": 1}` in
   `/data/botwvr/launcher/bettervr.json` (keep `"bettervr": false` if you use the flat screen).
   Start the game again, not the app. Cemu should log the pack on, the picture should be sharper, and
   the view must not be offset. If it is offset, quit, add `"reproject": 0` to the same file, start
   again, and say whether the offset goes.
3. **Pack off again.** Set `"graphicsPack": 0` (or remove the key), start the game, and check that
   the 720p view comes back.

**Send back:** the `[diag] VPADRead hook` and `[diag] calls=` lines; the `Sense pose`/`hands posed`/
`head` lines; the app's `graphic pack … turned` lines and Cemu's `Activate graphic pack` lines for
each step; `reprojection of the game's frames`; any `[crash]`; what the Sense buttons and gestures
did; where the first-person hands appeared; whether the picture was right in each step; and whether
the slow/late physics and gravity still
happen (say when, against the boot log's clock). No console result is claimed here.

**What it decides:** whether the hook now runs and injects at all (the `[diag]` lines), whether the
aspect fix removes the offset with the pack on, whether the switch really turns the pack off with the
console's `settings.xml`, and whether `reproject 0` is needed. It also says whether the gravity
problem survives the first session in which the hook body runs.

### Test request 11: the Graphics pack, the render pose, and the physics lines

**Which build.** The first build after the merge above: `patches/bettervr` 0001-0007, the x-z render
pose match (`54e7944`), the Sense-pose and head logs (`c85f3ca`), and `[clock]`/`[diag]`. It is not
m9 and not the 0005 artifact. The boot log should show `BetterVR's Graphics pack in …`; when the pack
had to be turned on, `graphic pack Graphics (Modified Version For BetterVR) turned on for VR` follows.

**If upgrading from a build before m6**, remove the home-screen tile and let ShadowMount Plus add the
app again after copying it; a copy over `/data/homebrew/PPSA98720` alone leaves the old registered
`param.json` (INSTALL.md section 5).

**Try:** first start flat, open BotW's Graphic packs and check the new pack is listed (off for the
TV, with FPS++ on as before), then play in VR for about 3 minutes in the world. Turn your head
slowly and quickly, use both Sense controllers, and walk, jump and fall once. Then run once with
`"profile": true` in `/data/botwvr/launcher/ps5cemu.json`.

**Send back:** the boot log, with the `BetterVR's Graphics pack in …`, `graphic pack Graphics
(Modified Version For BetterVR) …`, Cemu's `Activate graphic pack` and `rules.txt error` lines, the
`[vr-xr] … render pose found N, not found M; head x y z, hands posed L R` line, the `Sense pose from
VrTracker2` (or `VrTracker2 gives none`) lines, and the `[clock]` and `[diag]` lines. Say whether the
picture is sharper, whether the HUD, shadows and first-person hands look right, whether head turning
is smooth, and whether the slow/late physics and gravity still happen.

**What it decides:** whether the x-z match finds the render pose on every frame (a large `not found`
count means it does not), whether the Graphics pack activates at the expected size, whether VrTracker2
gives the hands a pose, and whether the guest clock or FPS++ is away from the real frame rate. The
gravity cause stays open until the head-position and physics lines say something concrete.

### Test request 10: the Sense controllers in the menus, linked at last

**Which build.** The next build after the change above: a strong `PS5Cemu_SenseMenuButtons` reference
in `port/ui/input.cpp` and the open/read lines in `port/bettervr/SensePS5.cpp`. The existing 0005
artifact does **not** have this change (its ELF still has the weak symbol). Check the installed
`/data/homebrew/PPSA98720/eboot.bin`'s SHA-256; it must not be m9's `d6b3447…`.

**Try:** both Sense controllers on, the headset connected, the app on its home screen. Stay there for
10 seconds, then push each controller's stick in each direction and press the right controller's
Cross, Circle and Options, and the left controller's Square, Triangle and Create. Say what the focus
did for each, and which buttons did nothing.

Then run Test request 9's VR steps with the same build, if it is the one with 0005.

**Send back:** `boot.log` and what the focus did. The new lines are `[vr] the left/right Sense
controller opened for the menus (handle …)`, `[vr] no left/right Sense controller for the menus
(user …): scePadOpen -> 0x…, scePadGetHandle -> 0x…`, and `[vr] the left/right Sense controller's
state cannot be read yet (0x…, connected …)`. The first with the focus moving means the menus work;
the failure line's codes say whether the console refuses types 3/4 outside VR, and the next step
would be to open them only when the system offers them.

**What it decides:** whether the missing link was the whole cause, or whether the Sense pads also
need a VR session/Hmd2 before the launcher can open them. The read line separates "opened but no
state" from "not opened".

### Test request 9: the first VR frame after the input-state fix (0005)

**Which build.** A build with `patches/bettervr/0005-…` (the next release after `m9-launcher`). **Check
the eboot first:** m9's is 45,314,393 bytes, SHA-256 `d6b3447…`, and crashes at the same
`__atomic_load` call; the 0005 CI artifact is 45,314,777 bytes, `543407d3…`; a newer build with 0005
and the Sense fix has its own hash, but must not be m9's. The boot log's first line is the same in
all of them, so it cannot tell them apart.

**Result (2026-11-05, 14:12 session, 0005 CI artifact):** passed for the crash. The session ran at
least 312 s; by 121 s it had 6,000 headset frames with `flip shown 5999`, 0
errors/missed/queue-full/repeated, and by 262 s it was at 60.0 fps; no `[crash]`, no `VR off` and no
`session ended`; `worn 1` for the first 72 s. What is still open is the visual half: what the headset
and TV showed in that run.

**Try:** `{"vr": true}` in `/data/botwvr/launcher/bettervr.json`, the headset on and connected, both
Sense controllers on. Start the game and wait through the first shader compilation (a few minutes),
then look at the headset and the TV for a minute. Move the controllers and the sticks; the game
should respond as it did before.

**Send back:** what the headset and the TV showed, and `/data/botwvr/launcher/logs/boot.log`. The
`[crash] signal 4 at 0x800102040` should be gone; the log should pass `OpenXR session begun
successfully!` and the Sense lines into frames. If it still crashes, send the `[crash]` line and
its machine context.

**What it decides:** the crash half is answered: the large `std::atomic` load was the first frame's
only blocker, and 0005 removes it. A different `[crash]` in a later build names the next problem; the
visual result says whether the m7/m9 VR path is finally correct in the headset.

### Test request 8: the launcher for one game (m9)

**Which build.** `m9-launcher`, over the installed app.

**Try:**

1. The app opens on the new home: the game's own picture behind it, Play in VR first. Move with the
   DualSense and with a Sense controller's stick.
2. **VR settings:** change a few (camera, turning, height), leave, come back: are they kept?
3. **Play in VR** (Test request 7's checks: what the headset shows). Did the settings you chose
   apply in the game (BetterVR's own menu, long-press R3, shows them)?
4. Quit, then **Play on TV**: the game flat, without BetterVR.
5. Anything that still looks like the emulator's launcher, or that you would want in or out.

**Send back:** what you saw, `/data/botwvr/launcher/logs/boot.log`, and
`/data/botwvr/launcher/bettervr_settings.ini`.

### Test request 7: VR through BetterVR's layer (m7-vr-layer)

**Which build.** `m7-vr-layer`, over the installed app.

**Try:** `{"vr": true}` in `/data/botwvr/launcher/bettervr.json`, the headset on and connected, both
Sense controllers on. Start the game, give it a few minutes (the first start compiles shaders), then
quit to the library.

**Send back:** what the TV and the headset showed, and `/data/botwvr/launcher/logs/boot.log`. Now
there should be `[bettervr] [INFO] PS5: the layer's ... runs`, `PS5: the first clear through the
layer`, then `[vr-xr]` lines (the OpenXR instance, the session, the headset), or the error that
stopped them.

### Test request 6: why BetterVR's layer misses the clears (m6-vr-diag)

**Which build.** `m6-vr-diag`. Copy its `PPSA98720` over the installed one; everything in
`/data/botwvr` stays.

**Try:**

1. **The tile:** after installing, does the home screen show the PS VR2 mark on it? (ShadowMount
   Plus may need to put the app on the home screen again first.)
2. With `{"vr": true}`, headset on and connected, start the game, wait a minute on whatever shows,
   then quit to the library.
3. Say what the TV and the headset showed.

**Send back:** `/data/botwvr/launcher/logs/boot.log`, whole. The new lines to look for start with
`[vr] Cemu's`, `[vr] vkGetDeviceProcAddr`, `[bettervr] [INFO] PS5:` and `[vr-xr]`.

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
