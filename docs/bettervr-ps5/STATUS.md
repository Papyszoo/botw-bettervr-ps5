# Status

Newest entries first. Every console result here was reported by Patryk. Nothing else counts as
tested.

## 2026-11-18: v0.6 combined (Options hold, stall tolerance, performance, Vibration, triggers, eye tracking), built, untested on console

Branch `bettervr-1.0/v06-all`, from v0.5.0, combining the v0.6 tracks. Options on the right Sense and on the
DualSense now opens BetterVR's menu on a 700 ms hold and is the game's "+" (pause) on a shorter press; the start popup
is gone. A camera stall shorter than 250 ms keeps first person, so strafing near grass no longer flashes Link's body,
and the compositor blends the HUD premultiplied, so the weapon wheel's arrows, the minimap and the clock keep their
alpha and the HUD stops shimmering (mip chain, `textureGrad`). The frame end does fewer copies, accurate barriers are
off by default like a PC VR run, `bettervr.json` `"profile": true` prints a `[frame]` line every 10 s, and BetterVR's
System page has a VR Resolution selector (0.5x to 1.5x). BetterVR's menu has a Vibration page (nine categories, a
master switch, a headset cap and a test button for each device) over one 60 Hz mixer that also reaches the PS VR2
headset's motor and the DualSense. The bow's draw firms R2 and the shield's guard clicks L2 on the DualSense (the
Sense controllers behind `"senseTriggers": true`). The PS VR2 gaze exports are probed, exposed as
`XR_EXT_eye_gaze_interaction`, and an optional `Gaze Throw Assist` bends a throw up to 15 degrees toward where the
player looks. New patches: bettervr 0007-0010, cemu 0055. Details: `UPDATE-1.0.md`, "v0.6".

**Test request 53 (v0.6 combined).** One console run of this build (13.60), in VR, with `bettervr.json`
`"profile": true` and the Sense controllers charged; send the logs zip and the lines named under each letter.

- (a) Options, Sense and DualSense. On the right Sense, tap Options once (the game's pause/inventory opens) and tap
  again (it closes); hold it about a second (BetterVR's menu opens while the button is still held, no pause behind it)
  and hold again (it closes). Repeat with the DualSense's Options, with the Sense controllers off and third person set
  in Settings > VR, and move through the pages and rows with the DualSense's left and right stick. Say whether any
  help card or popup still appears over the game at start. Send every `[menu]` line (`Options down`, `Options up after
  N ms`, `long press -> open`/`closed`, `release after the threshold -> hold`, `short press -> pause`, `short press
  ignored, game input is blocked`).
- (b) Strafe flash. Strafe left and right with the left stick near grass and say whether Link's body still flashes for
  a frame. Send the `[pose] a short camera stall kept first person (N frames)` lines (at most one a second) and say
  whether the frame rate held.
- (c) Weapon wheel and HUD. Open the weapon wheel: the left and right arrows must be visible on the black buttons, and
  the minimap and clock must not look faded or black. Turn your head slowly while looking at the HUD and at menu text,
  menu open and closed: say whether the edges still crawl or shimmer, and whether the TV mirror differs. Send the
  `[frame]` and `[perf]` lines from two minutes in the overworld at 1x.
- (d) Vibration page. Hold Options and open the new **Vibration** page between Controls and Display; say whether it
  draws (master switch, headset cap, three test buttons, nine category rows with On / Strength / Hands / Headset /
  DualSense). **Press Headset first, alone, before taking any damage**: that is the first call to the motor's
  unverified offset and the design keeps it from being called at VR start. Then press Sense Controllers and DualSense
  and say which buzzed. Send `[vib] test headset: level 25 -> 0x...`, `[vib] test hands`, `[vib] test pad`, and the
  `hmd2: headset vibration bound ...` line (or `hmd2: sceHmd2SetVibration not found on this firmware, the headset
  stays still`).
- (e) Vibration categories. At 0 % (or Off) a sword swing, a weapon/gear equip, a bow draw, a shield raise and the
  slate's hum are each silent on their own; Game Rumble at 0 % silences the game's own rumble while BetterVR's rows
  still work; 30 % is clearly weaker than 100 %. Get hit (Bokoblin, fall, fire) at Damage Taken 100 %: both hands give
  one rumble; at 0 %, none. With a category set to DualSense, one event is felt once, not twice, and the pad still
  works normally (no rumble) when VR is not running. Send every `[vib] damage state '...'` line (up to 6), every
  `[vib] game rumble ...` line (at most 40, 150 ms apart; each has `caller=`, `state=`, the VPAD `bytes=` and the sead
  `pattern="..." repeat=` when found), and the one `[pad] rumble: BetterVR's vibration mixer drives the DualSense`
  line at VR start. Say whether the run crashed or stuttered around them.
- (f) Triggers. With the DualSense, equip the bow, hold R2 and draw: R2 must firm up as the draw grows and release when
  the arrow flies. Guard with the shield on the left hand: L2 must click around a third of its pull while guarding,
  none after lowering it. Open BetterVR's menu and the game's pause screen: both must go soft at once; quit to the
  library: nothing may stay firm. In the System page, turn Adaptive Triggers off and on and change Strength: live and
  kept after a restart. With `"senseTriggers": true` added to `/data/botwvr/launcher/bettervr.json`, repeat on the
  Sense controllers. Send every `[trig]` line: the `DualSense (handle N) L2 ... R2 ... -> 0x...` calls,
  `Sense trigger effects on`, the `Sense both hands (0x78 ...)` and/or `Sense one hand (0x38 ...)` results,
  `Sense payload ... accepted`, and the `DualSense off` / `Sense off` lines. If the Sense triggers are not felt
  although a layout was accepted, add `"senseTriggerLayout": 2` (or 1), repeat, and send the new `[trig]` lines.
- (g) VR Resolution. BetterVR's menu > System > VR Resolution: choose 0.5x, then 1x, then 1.25x. Each choice must log
  `VR resolution preset: ...` and show the restart notice. After 0.5x, restart and send the `view configuration`,
  `Found rendering resolution`, `Graphics pack presets` and `VR Resolution Multiplier` lines: the eye size must be
  smaller; after 1x it must be back to 2000x2040.
- (h) Eye tracking and throw assist. In the PS5 settings turn on Settings > Accessories > PS VR2 > Eye Tracking and run
  Adjust Eye Tracking, then play in VR. Send every `[gaze]` line: `five exports bound by name`, `ActivateGazeTracking(1)`,
  `device detected` or the device status line, `calibrated` or the calibration status line, and during the first 10 s
  `result 0x38`, `foveated 0x18`, `raw direction ... status full N foveated N`. In Controls > Motion Controls turn on
  Gaze Throw Assist, then throw something while looking left of the head direction and again while looking right; say
  which way each throw went and send the `[Throw] gaze assist bent N degrees toward the gaze` lines (or `toward the
  head`). With the setting off, the same throw must not bend.
- (i) Touchpad click. With the DualSense in game, a click of the touchpad opens the Slate (`-`) and Create opens it as
  well; moving a finger on the touchpad still moves the GamePad's cursor; touchpad + Options opens only the port's
  in-game menu, not the Slate. Start once with an older `/data/botwvr/launcher/ps5cemu.json` (no
  `touchpadClickMigrated`): `touchpadClick` must read `[2, 2, 2, 2]` afterwards. Set player 1's Touchpad click to
  Automatic in Settings, restart, and say it is still Automatic. Send the `[controls]` lines that name each player's
  mappings.
- (j) Settings and About. On the Wii U side open Settings and go through the ten pages, then About: there must be no
  PayPal or QR support card and no other donation link, About must have the one licence line naming Cemu, PS5-ECAM and
  Crementif's BetterVR (under the GNU GPL and MIT respectively) and the Wii U's own paths, and the pages must show no
  other system's games, sides or settings. Send a photo of About and say whether any page still reads as a multi-game
  launcher.

## 2026-11-18: console run 4 (37c6028, 13.60)

Reported from the console: the community graphic packs are listed again; opening and closing the inventory no longer
corrupts the scene; Sense vibration works; the in-game menu works; the game feels smooth. From the log: VR resolution
1x (2000x2040 per eye); median 50.0 fps over 39 ten-second windows in game (37.9 to 53.1); 410 packs at start, 411
with BetterVR's. Known minor issues reported: the character flashes (seems to jump between positions) while moving
left or right with the left stick; the weapon-change wheel lacks its left and right arrows. Play on TV is untested on
the 1.0.0 builds.

Released as v0.5.0.

## 2026-11-18: 1.0.0 port, console run 3 (50143ed, 1x): scene corruption after the inventory, patch cemu 0054, built, untested on console

Reported from the console: after closing the inventory the 3D scene shows coloured and black patches; it gets worse with
each open and close. Cause found in code (not seen on console): patch cemu 0015's copy clamp skipped every right-view
image copy of a texture with a view layer, so the right view's copy of the scene stayed stale. Patch 0054 counts the
view layer's slices. Details: `UPDATE-1.0.md`, "Console run 3: scene corruption".

**Test request 51 (the build of this commit, 13.60 console).** Play in VR (1x). (a) Open the inventory, close it, three
times, looking at both eyes and the TV: say whether the patches are gone, and which eye (cover one eye) if not.
(b) Send the logs zip. Cemu's boot log must have no `[copy] image clamped ... x1 layers asked, ... x0 copied (nothing:
skipped)` lines for same-size images any more (run 3 had about 100 of them). (c) If patches remain: repeat (a) with the
Graphics pack on 0.5x (Cemu's Graphic packs, BetterVR's Graphics pack, VR Resolution Multiplier) and say whether the
inventory glitches there too; send the lines `[copy] image clamped` and `[copy] image WOULD OVERRUN` of both runs.

## 2026-11-18: 1.0.0 port, console run 2 (db1f541, 13.60): BotW in VR works
Next build: VR resolution preset 1x (BetterVR's default) instead of 0.5x; built, untested on console.

Reported from the console: it works. From the boot log: Extended Memory off; VR start 1/5 to 5/5 reached (first
frame to the headset 11.1 s after the session, during the shader cache load); no `[crash]`, no ProcUI line.
Game: about 57 to 60 fps from 90 s on (10 s windows 54.5 to 60.0), 5,100 to 5,700 draws a frame, GPU thread
idle 1 to 2 %, waiting on the GPU 0 %; headset submits 57 to 60 fps. Eye images 1280x1320 (BetterVR's preset of
the 1984x2048 eye). Menu: Options down/up within 1 s toggled it open, then closed. Haptic calls arrive. Sense
vibration stayed off: libScePad's anchor at +0x1f650 is `...open [%08x]` followed by a line feed (`5d 0a 00`),
and the table's string had no line feed; fixed in 80a6a12 (built, untested on console).
Build27 for comparison: about 33 fps median in the overworld at 9,000 to 10,000 draws.

## 2026-11-18: first console run of the 1.0.0 port (build 3e8c18d), crash in Cemu's ProcUI, patch 0052, built, untested on console

Reported from the console: the PS5's play-area scan ran, then "PSVR crashed during room scan". The log: VR start 1/5 to
3/5 reached, the game's start waited about 30 s for the shader cache (0 draws), then signal 11 at 0x60 in
`proc_ui::AddCallbackInternal`: the game's allocator returned 0 for the first ProcUI callback. Why it returned 0 is not
proven (Extended Memory is the new variable, on by default in 1.0.0; optional for BetterVR, so it is now off by default, patch bettervr 0005). Patch cemu 0052 leaves such a callback out and logs
the allocator, pool and heap sizes. The libScePad anchor check now logs 64 bytes at the anchor. Nothing else changed;
details in `UPDATE-1.0.md`, "Console run 1".

**Test request 50 (the build of this commit, 13.60 console).** Play in VR again. Send the logs zip: the `[perf]` lines, the
`OSSched` line, every `ProcUI:` line in Cemu's log.txt, `VR start 4/5` and `5/5`, the `at anchor +0x1f650` line, any
`[crash]`; the `Extended Memory pack:` line in the boot log, and that Cemu's log has no `modified RAM mapping` and no `BotW_ExtendedMemory_V208` (the memory layout is 0.9.23's again). Say whether the game reached the title screen and whether the scan ran before or after the game started.

## 2026-11-18: 1.0.0 port, stage 4 (Sense controllers, menu, vibration, written blind), built, untested on console

Branch `bettervr-1.0/stage4`. The Sense controllers answer BetterVR 1.0.0's Oculus Touch bindings (sticks, grips,
triggers, face buttons, stick clicks, poses with the calibrated grip defaults 17/32/30). One short press of Options on
the right controller opens and closes BetterVR's own menu (patch 0004; the 1.0.0 default of holding Square on the left
also works); haptics go to the Sense through hmd2's vibration functions. PlayPosition starts on Seated. Details:
`UPDATE-1.0.md`, "Stage 4 progress". Stages 1 to 3's Test requests (46 to 48) and 45 are still unanswered, so a stage 4
run may stop at an earlier problem.

**Test request 49 (stage 4 build, 13.60 console).** Install the stage 4 build, Play in VR. (a) Walk with the left stick,
turn with the right stick, jump (B), run (A), draw a weapon with the grip and swing it at a bokoblin or a tree, draw
the bow (grip, then pull with the trigger) and shoot, use magnesis from the Runes (Y on the left, then pick an object
with the trigger). Say whether anything is late or missing, and whether Link's hands sit where yours do. (b) Press
Options on the right controller once, quickly: the menu should appear in the headset; change one setting (for example
Play position), press Options again to close it. Say whether the menu is readable and where it hangs, and what the
sticks and A/B do inside it. (c) Take a hit or hit something with a weapon: say whether either controller vibrates.
Send the logs zip and `BetterVR_log.txt`: every `[menu]` line, `Seated height`, `Touch profile`, `Sense controller`,
`Sense buttons`, `haptic call` and `Sense controller vibration` line, the `[vr-xr] ... frames` lines, and any `[crash]`.

## 2026-11-18: 1.0.0 port, stage 3 (stereo in the headset, written blind), built, untested on console

Branch `bettervr-1.0/stage3`. The OpenXR runtime over hmd2 now answers what BetterVR 1.0.0 asks (no depth swapchains,
one 2D layer per swapchain, the frame pump's one-ahead wait), foveation is off, the packaged pack is the checked-in
`patch_compiled.asm`, the Graphics pack starts on the 0.5x preset, and a headset that does not start ends in the
library with the reason (issue #3). Each step of the VR start logs one line (`VR start 1/5` to `5/5`). Details:
`UPDATE-1.0.md`, "Stage 3 progress". Nothing here has run on a console; stage 1 and 2's Test requests (46, 47) are
still unanswered, so a stage 3 run may stop at an earlier problem.

**Test request 48 (stage 3 build, 13.60 console).** Install the stage 3 build. (a) Play in VR with the headset on:
play two minutes at the start, then two in a village (Kakariko or Hateno). Look at both eyes: is the depth right,
is there ghosting or a doubled picture, does the head follow and does the picture stay where it is when you turn.
(b) Quit to the library, switch the headset off (not just taken off: powered down or unplugged) and start Play in
VR once more: it should return to the library with a message that says why, not a crash or a black headset that
stays. Send the logs zip (`/data/botwvr/cemu/log.txt`, the boot log) and `/data/botwvr/cemu/BetterVR_log.txt`:
every `VR start n/5` line (says how far the start got), the `view configuration`, `Creating Vulkan device ... mod
queue`, `Foveated shading is off`, `Graphics pack preset` and `Vulkan: multiview` lines, the `[vr-xr] ... frames, N
fps submitted` lines (the headset's rate), the `[perf]` fps and GPU-busy lines from the game, any `[crash]`, and for
(b) the `VR did not start` line and the text on Breath of the Wild's page. Say what the fps lines show against
build27's 31.6 to 34 fps in the overworld and 60 fps flat, and whether the TV shows the headset's view.

## 2026-11-18: 1.0.0 port, stage 2 (BetterVR 1.0.0 compiled in, no headset work), built, untested on console

Branch `bettervr-1.0/stage2`. BetterVR 1.0.0 (`db5495b`) is a host module of the Cemu fork
(`CEMU_EXTERNAL_MODULES`), with the PS5 platform layer and the OpenXR runtime over hmd2 linked in
beside it. `patches/bettervr` is one patch now (28 of the 0.9.23 patches are gone); Play in VR starts
the module as the game starts, Play on TV starts none. No stereo, controller or menu work yet: the
headset may show nothing. Details: `UPDATE-1.0.md`, "Stage 2 progress".

**Test request 47 (stage 2 build, 13.60 console).** Install the stage 2 build and start the app.
(a) Play on TV with Breath of the Wild, two minutes in Kakariko or Hateno: it runs flat as in the
stage 1 build; say whether the picture and fps match. (b) Play in VR, with the headset on, and let it
run for a minute or until it stops. Send the logs zip (`/data/botwvr/cemu/log.txt`, the boot log) and
`/data/botwvr/cemu/BetterVR_log.txt`: every `[emu] BetterVR`, `[bettervr]`, `[vr]` and `hmd2` line,
and any `[crash]`. Expected in the boot log: `BetterVR's host module starts`, `Registered host
module: BetterVR`, BetterVR's `Acquired system to be used` with the system and runtime names, its
`Graphics pack preset ...` lines, then the Vulkan instance and device lines through the module and
the first frames. Say whether the headset shows anything (a picture, black, or the system's), whether
the app crashed or returned to the library, and, if the game ended, how.

## 2026-11-18: 1.0.0 port, stage 1 (Cemu fork base, flat only), built, untested on console

Branch `bettervr-1.0/stage1`, commit 576d15a, CI run 44198974605 green (`make release`). Cemu moves
from premohq's `cc6fea1` to Crementif's fork `86168a8`; `patches/cemu` rebased (old 0050 to 0052 dropped,
already in the fork; 0053 cut to its gs copyshader half); BetterVR is not compiled in, so Play in VR
shows the TV picture in the headset. New patch 0051 logs `Vulkan: multiview: ...` and
`Vulkan: fragment shading rate: ...` at device creation (both strings checked in the built ELF).
Details: `UPDATE-1.0.md`, "Stage 1 progress". Not published yet: `testing` still carries build27
until Test request 45 is answered.

**Test request 46 (stage 1 build, 13.60 console).** Install the stage 1 build, start the app, Play on
TV, and play two minutes in Kakariko or Hateno. Send `/data/botwvr/cemu/log.txt` and the boot log:
the `Using GPU`, `Driver version`, `Vulkan: multiview` and `Vulkan: fragment shading rate` lines, the
fps and GPU-busy lines, and any `[crash]`. Expected: multiview supported with max views at least 2,
fragment shading rate unsupported. Say whether the picture and fps match build27 on TV. If multiview
is unsupported, the port stops here (`UPDATE-1.0.md`, "Stages").

## 2026-11-17: build27 (menu toggle chain and Sense vibration lookup diagnosed/fixed), built, untested on console

Build26's console run: Options reached no menu (no `[menu]` line) and no vibration was felt.

- **Menu** (patch 0028, new; 0001 to 0027 untouched): the runtime's right `menu/click` is Options and was
  already read correctly (build23's calibration used it). In BetterVR, a short press came from
  `ButtonState`, which needs a release within 250 ms; a longer press counts as a long press, which the PS5
  build ignores. The toggle now takes the mod menu action's own down and up edges: a press released within
  1 s toggles, a hold does nothing. Logs: `[menu] switch: on/off (bettervr.json's menu)` once,
  `[menu] mod menu action is active/not active` on change, `[menu] Options down` / `up after N ms`,
  `[menu] toggle -> open/closed`, and in the runtime `[vr-xr] Options down/up` (first 60).
  This is the likeliest cause, not proven; the new lines show where the chain stops if it is another.
- **Vibration** (`third_party/hmd2/hmd2.cpp`): libScePad's 13.60 anchor did not match at +0x1f650. When
  the single-table libraries (libScePad) find no match, the module's segments are logged, the bytes at the
  anchor and at the export offsets are logged where the segment is readable (execute-only code is not
  read, and logged as such), and the readable segments are searched for the anchor string. Found exactly
  once: the base moves by the difference and the table's offsets are used. Found zero or several times:
  nothing changes.

**Test request 45 (build27, 13.60 console, `"menu": 1` in bettervr.json).** Press Options on the right
controller once (short); a panel should open in the headset. Press it again; it should close. Then hit
something with a weapon and feel for vibration. Send the log: the `[menu]` and `Options` lines and the
`hmd2: libScePad` lines (segments, anchor, `anchor string found`).

## 2026-11-17: build26 (Sense controller vibration), built, untested on console

Build23's log showed `scePadSetVibrationMode` and `scePadSetVibration` returning 0x80920003 for both
Sense controllers: libScePad's DualSense setters refuse pad types 3 and 4, so nothing was ever felt.

- **Calls** (`third_party/hmd2/hmd2.cpp`, `hmd2::sense_set_vibration_mode` and `sense_set_vibration`;
  `port/bettervr/xr/runtime.cpp`): libScePad's own Sense exports, `scePadVrControllerSetVibrationMode`
  (NID Wf6-PNCyY20) and `scePadVrControllerSetVibration` (NID DGCwN1Lmmys, one intensity byte), on the
  `scePadVrControllerReadState` handle. Resolved with hmd2's `load_sysmodule` and `resolve_module`:
  `sceKernelDlsym` by name first, else on 13.60 at +0x100b0 / +0x5610 after the anchor string at +0x1f650
  matches. Other firmware: vibration stays off, one log line. Names, offsets and anchor are firmware
  facts from Ghost-VR (GPL); no code taken.
- **Use:** at controller open, mode 2 once per hand, logged as `[vr-xr] left Sense controller vibration
  mode -> 0x...`. `SetVibration(i, amplitude)` sends `amplitude * 255` as one byte, 0 stops it; the first
  one is logged as `vibration -> 0x...`. The DualSense calls for the Sense controllers are gone; Cemu's
  own DualSense rumble is untouched.
- **Backlog:** headset vibration on damage taken and on explosions, not on hits landed. The PS VR2
  headset has a single motor (`sceHmd2SetVibration(hmdHandle, 0..25)`, 13.60 offset 0x2ae00 per
  Ghost-VR's and HLA-PSVR2's export tables, unverified on hardware), so it has no direction; direction
  could only come from the two Sense controllers (left/right). BetterVR 0.9.23 only sees the game's
  generic rumble (VPADControlMotor), so damage-only or explosion-only needs a new hook into BotW's code.
  Planned, not built.

**Test request 44 (build26, 13.60 console).** Hit something with a weapon and use anything that rumbles
in the game, with the Sense controllers on. Send or let the log show the `vibration mode ->` and
`vibration ->` lines for both hands. Expected: `0x00000000` and a felt pulse. If `Sense vibration
exports not found` appears instead, send the `hmd2: libScePad` lines.

**Result 44 (build26, 13.60, 2026-11-17, from the console's log).** No vibration felt. The log has
`hmd2: libScePad: load ".../common/lib/libScePad.sprx" -> handle 0x72 (res 0)`, `found by: sandbox path`,
`libScePad.sprx: no export table for this firmware (base 8009f8000)`, `Sense vibration exports not found on
this firmware, Sense vibration stays off`, and the mode and vibration calls returning 0x80000001.

## 2026-11-17: build25 (BetterVR menu stage A behind bettervr.json's menu switch), built, untested on console

First step of BetterVR's own menu in the headset: the ImGui overlay starts and a small panel is drawn. Not the
real menu pages yet, and gameplay input is not blocked on purpose beyond what BetterVR does itself while its
menu flag is open.

- **Switch:** `"menu"` in `bettervr.json`, default 0. At 0 nothing new runs: no overlay, no Options toggle, the
  mod menu stays shut as before.
- **Patch 0027** (new; 0001 to 0026 untouched): `vulkan_imgui.cpp` gets a Linux/PS5 overlay. It is made on the
  first open, on Cemu's recording thread (the HUD hook), with one text font and no help images. It draws into
  its own render target and `LinuxCopyHudToSwapchain` blits that to the HUD quad in place of Cemu's HUD image,
  which is only read. All ImGui calls are on that one thread.
- **Queue safety:** the backend's font upload makes its own submit and waits for the queue to go idle, and goes
  to the driver directly. It is called once, in the overlay's constructor, under `ps5xr::QueueMutex()`
  (`BETTERVR_PS5_QUEUE_LOCK`), on Cemu's queue family and index. No ImGui frame creates it later.
- **Options:** in the Touch profile the mod menu action is bound to `/user/hand/right/input/menu/click` instead
  of the left X on this port, and a short press toggles `m_isMenuOpen` (the long-press toggle is Windows only).
- **Log lines** (one each): `[menu] init: <phase> -> ok/fail (...)` for context, font, descriptor pool,
  render pass, backend functions, backend, font upload, target and first draw; `[menu] open`, `[menu] closed`.
  A failed init logs `fail`, closes the menu and keeps it closed.

**Test request 43 (build25, any console).** Set `"menu": 1` in `/data/botwvr/launcher/bettervr.json`, start
Breath of the Wild in VR, press Options on the right controller, wait 3 s, press Options again. Send the
`[menu]` lines and any `[crash]` lines from boot.log, and say whether a panel with the BetterVR version and a
frame rate line appeared in the headset and where.

**Result 43 (build25, 2026-11-17, from the console's log).** `bettervr.json` has `"menu": 1`. Options was
pressed in game (short presses and one hold of about 3 s). The log has no `[menu]` line and no crash.

## 2026-11-17: build24 (calibrated hand defaults; calibration mode removed), built, untested on console

- **Defaults** (`port/bettervr/xr/runtime.cpp`): `gripYaw` 17, `gripPitch` 32, `gripRoll` 30 when
  `bettervr.json` has no key (the values build23's calibration found). Yaw and roll are mirrored for the
  right hand as before. The three keys, the `controllers: grip ...` log line and the first 20 s of
  `[vr-xr] hands:` lines stay.
- **Calibration mode removed:** build23's Options handling, input blocking, stick turning, Cross reset,
  pulses, `hands calibrated` log and `BetterVR_PS5_SaveHands` are gone; `vr_settings.cpp` is back to its
  pre-build23 content. INSTALL.md's calibration note is dropped.

**Test request 42 (build24, any console).** On a fresh `bettervr.json`, without the grip keys: do the
hands look right (pointing ahead, down, palms up)? Send the `[vr-xr] hands:` lines if not.

**Result 42 (build24 defaults, 2026-11-17, from the console).** With the default grip 17/32/30 the hands are
"better now".

## 2026-11-17: build23 (hold Options to calibrate the hands), built, untested on console

Build22's 80 degree `gripYaw` default is gone: it turned the hands about the wrong axis. All three
angles, `gripYaw`, `gripPitch` and `gripRoll`, now default to 0, which is build21's hands.

- **Rotation** (`port/bettervr/xr/runtime.cpp`, in `xrLocateSpace`): the angles are one local rotation
  after the tracker pose, in the order yaw (local Y), pitch (local X), roll (local Z), on grip and aim.
  Yaw and roll are mirrored for the right hand. `aimPitch` stays an extra on aim only. `gripPitch` now
  applies to aim as well as grip.
- **Calibration:** while Options on the right controller is held, neither controller's buttons or sticks
  reach the game. Left stick X turns yaw, left stick Y pitch, right stick X roll (45 degrees a second),
  Cross resets. Both controllers pulse when it starts and ends. On release the angles are written to
  `bettervr.json` through the VR settings page's writer, and `[vr-xr] hands calibrated: yaw Y pitch P
  roll R (saved to bettervr.json)` is logged. `[vr-xr] hands:` is logged every 1 s while held.

**Test request 41 (build23, any console).** Hold Options on the right controller and turn the hands
until they match the real controllers held pointing ahead, then release. Check pointing down and palms
up. Send the `[vr-xr] hands calibrated:` line, or let the log be read.

**Result 41 (build23, 2026-11-17, from the console's log and `bettervr.json`).** Calibration works, no
crash; the hands are "ok now, maybe not perfect, but better". Saved: `gripYaw` 17.3, `gripPitch` 31.9,
`gripRoll` 29.7; the log reads `hands calibrated: yaw 17 pitch 32 roll 30`. Holding Options while using
the right stick is awkward, since both need the right thumb; build24 changes it to a tap toggle.

## 2026-11-17: build22 (Sense hands turned inward), built, untested on console

Build21's first console run showed both of Link's hands turned inward about 80 degrees (range 70 to 95)
with the controllers neutral and pointing ahead, mirrored left and right, pitch and roll about level.

- **Yaw correction** (`port/bettervr/xr/runtime.cpp`, in `xrLocateSpace`): grip and aim poses are turned
  about the controller's vertical axis by `gripYaw` degrees, +left and -right, before the pitch. The
  default is 80, the measured value; the code shows no exact convention, so it is empirical.
- **`gripYaw` key** in `bettervr.json`, read like `gripPitch`, and logged in the
  "controllers: grip pitch ..." line. Documented in INSTALL.md.
- **Diagnostic:** every 2 s for the first 20 s of hand poses, `[vr-xr] hands: L yaw/pitch/roll ...`
  with the final grip rotation against the head, in degrees. Neutral and correct reads about 0/0/0.

**Test request 40 (build22, any console).** Hold the controllers neutral pointing straight ahead, then
straight down, then turn the palms up. Say whether Link's hands match each time, and send the
`[vr-xr] hands:` lines from the log. If the hands are still off, send a `gripYaw` value to try in
`/data/botwvr/launcher/bettervr.json`.

## 2026-11-16: build21 (upstream Cemu fixes), built, first console run 2026-11-17

Backports of small upstream fixes, each its own patch, code verbatim:

- **patches/cemu 0050** (cemu-project/Cemu `babd690`, `b044641`): graphic pack patch groups resolved in
  linear time, each group with its full code cave. BetterVR's pack has thousands of groups.
- **patches/cemu 0051** (`feb7c7b`): async Vulkan pipeline compile race.
- **patches/cemu 0052** (`d1ca2e9`): the PPC recompiler's function scan no longer reads unmapped memory.
- **patches/cemu 0053** (`5a0430b`, `934329f`): no crash on an invalid gs copyshader; a GX2 flush of an
  empty command buffer starts a bigger one.
- **patches/bettervr 0026** (Crementif/BotW-BetterVR `47ea413`): GPU memory properties read through Vulkan
  1.1 (the KHR alias is null when its extension is not enabled).
- Applied in order on the pins (Cemu `cc6fea1`, 53 patches; BetterVR `2e06b31`, 26 patches).

**Test request 39 (build21, any console).** Play Breath of the Wild in VR for about 15 minutes,
including a shrine and a fight. Send the logs zip, any `[crash]` lines, and say whether crashes or
first-time stutters changed against build20. Test requests 31 to 33 still apply to this build.

**Result 39 (2026-11-17, firmware 13.60, kstuff from ps5_autoloader with no app-jailbreak list, fresh
install after a factory reset):** the game runs, fighting worked, and nothing went wrong. Not reported
yet: the shrine and the stutter comparison with build20. The app's `boot.log`, about 5 minutes in VR
up to the time it was read, has no `[crash]` line. It says "HEN: no ... JIT available (executable
direct memory)" and "the recompiler's code is in executable direct memory (no HEN JIT grant
needed)". It also answers Test request 31 for this setup: `hmd2: libSceHmd2: found by: sandbox path`,
firmware table 13.60, `sceHmd2Initialize`, `sceVrTracker2Initialize` and `sceHmd2ReprojectionInitialize` all
0, and no `[vr] VR did not start`.

## 2026-11-16: BetterVR 1.0.0 update planned (not built)

- The work list is in [`UPDATE-1.0.md`](UPDATE-1.0.md): BetterVR 1.0.0 (`db5495b`) on Crementif's Cemu
  fork (`86168a8`), in five stages with Test requests 34 to 38. Multiview is exposed by the driver's
  source, fragment shading rate is not (foveation off). Nothing built, nothing run on the console.
- **Backlog:** premohq's Playtendoh branch (the renamed PS5-ECAM) is not merged: 75 conflicts, and its
  Cemu pin did not move. Only Cemu fixes are picked from it (see the quick wins in UPDATE-1.0.md).

## 2026-11-14: build20 (failed VR start, hmd2 load fallback, Graphics pack, menu probe), built, untested on console

- **A failed VR start ends in the library, not a crash (issue #3).** When BetterVR cannot start VR on
  the first eye clear, patch 0004 calls `BetterVR_PS5_VrFailed`, which logs "[vr] VR did not start
  (<step, rc>): back to the library", writes the reason as the game's launch error and restarts to
  the library. Breath of the Wild's page then shows "The game could not start". The game cannot go on
  flat: its pack's patches already call BetterVR's hooks. Only the first-clear start path is covered.
- **hmd2 load fallback (issue #3).** When the sandbox-path load fails, `load_sysmodule` logs euid, the
  sandbox word and which of the sandbox, `/system/common/lib` and `/system_ex/common_ex/lib` paths
  hold the library, loads the plain path that exists, and last looks the module up among the loaded
  ones by file name (as Ghost-VR does). One line `hmd2: <name>: found by: ...` says which step worked.
- **Graphics pack (issue #2).** `PS5Cemu_BetterVRApplyMode` no longer touches "Graphics (Modified
  Version For BetterVR)" in either mode, so the player's choice in Graphic packs stays. The
  `graphicsPack` key in bettervr.json is gone. New installs have it off.
- **Menu probe (issue #2).** INSTALL.md no longer promises BetterVR's R3 menu (it does not exist
  here; the in-game menu is touchpad click + Options and shows on the TV only). A log-only probe counts
  xrWaitFrame, xrEndFrame, hmd2 submits and Cemu TV presents while the in-game menu is open, to show
  whether the headset loop runs under it before any TV-image bridge is built.
- **Backlog:** the `hmd2.cpp` changes must be carried back to ps5-splat-vr (`platform/ps5/hmd2/hmd2.cpp`):
  `#include <unistd.h>`, the `sceKernelGetModuleList` extern, the `const char *found ...` block through
  the `found by` log in `load_sysmodule`, and `const int handle` -> `int handle`.
- **Built, untested on console.** Pre-release `testing`, titled build20-vr-fallback.

**Test request 31 (VR start, the issue #3 setup or any console).** Install the build and start
Breath of the Wild in VR. Send the logs zip and every line starting `hmd2: libSceHmd2:` (sysmodule
line, the `euid ..., sandbox ...` line, each `load ...` line, the `found by: ...` line),
`hmd2 did not start`, `[vr] VR did not start` and any `[crash]`. Expected: either VR starts (found by:
plain path or already loaded), or the app returns to the library with "The game could not start" and
no crash screen. Say which, and the firmware version.

**Test request 32 (Graphics pack toggle).** In Breath of the Wild's Graphic packs turn on "Graphics
(Modified Version For BetterVR)", quit to the library, start Play in VR, then Play on TV once. Send the
`graphic pack ... turned` lines and Cemu's `Activate graphic pack` lines. Say whether the pack is
still on in Graphic packs after each start, and what the VR picture looks like (earlier builds had
the view offset with this pack on).

**Test request 33 (menu probe).** Start Breath of the Wild in VR and reach the game world. Click the
touchpad and press Options to open the in-game menu, wait about 5 s, close it with O. Send the logs
zip. Expected `[vr-xr]` lines: "in-game menu open (xrWaitFrame A, xrEndFrame B, hmd2 submits C, Cemu TV
presents D so far)", about one "in-game menu still open at N ms" a second, and "in-game menu closed
after N ms". Counts near N/16.7 mean the headset loop runs under the menu; zeros mean it stalls.

## 2026-11-08: v0.1.1 (built, untested on console as a whole)

- **Contains:** hmd2 export tables for 5.50 to 13.60 (issue #2, see the entry below), the app renamed
  to "BotW VR", the updater pointed at this repository (still off: `kOwnUpdates=false`), and the
  install progress showing a file count (issue #1).
- Version 0.1.1 (Makefile `VERSION`, `sce_sys/param.json` contentVersion 00.001.100). Its VR code
  matches build19.
- **Built, untested on console as a whole.** Only build19 has a console report (Test request 29).

**Test request 30 (v0.1.1).** Install `BotW-VR-v0.1.1.zip` from the release.

1. Does the home-screen title read "BotW VR"?
2. Do VR start and play as in build19?
3. Install an update or DLC: does the progress show "files N / M", and does the install finish?
4. Send the `hmd2: ... firmware table ...` log lines and the logs zip.

## 2026-11-08: issue #2, hmd2 export tables for more firmware (untested on console)

- **Cause:** on a 12.40 console hmd2 logged "libSceHmd2.sprx: no export table for this firmware" and
  "sceHmd2Initialize not resolved". The libraries keep their functions out of the symbol tables, and
  hmd2 had offset tables for 13.60 and 7.61 only. (boot.log's "13.09" is probably a kstuff spoof.)
- **Fix:** `third_party/hmd2/hmd2_exports.h` regenerated in photo-splat-vr (`143d355`). Hmd2 and
  VrTracker2 now have 13.60, 12.40 (12.20), 11.60, 10.x (10.00 to 10.60), 9.00, 8.40, 7.61 (7.20) and
  5.50; VrHand 13.60 and 12.40. Data from PS5VR and Ghost-VR, which agree wherever both have a
  firmware; 9.00 is Ghost-VR only. `hmd2.cpp` is unchanged.
- **Untested on console.** PS5VR ran 13.60 and 12.40; the other tables were only checked against
  that firmware's libraries. PS5VR notes 12.40 playback can become choppy after a few seconds.
- No VrHand table below 12.20 (it takes other parameters there), so hand tracking is unavailable there.
- **build19** = pre-release `build19-hmd2-fw` (CI build of `6dde48b`: build18 plus these tables; the
  app still reports v0.1.0).

**Test request 29 (build19, firmware 13.60).** Install `BotW-VR-build19-hmd2-fw.zip` from the
pre-release and start Breath of the Wild in VR.

1. Does the headset start and the game play in VR as in build18? Send the line
   `hmd2: libSceHmd2.sprx: firmware table 13.60, base …` (it should still pick 13.60), and the
   `libSceVrTracker2.sprx` and `libSceVrHand.sprx` lines after it.
2. Test request 28's three checks (flashes, rune window, map) still apply to this build.
3. Send the logs zip. When this passes, the issue #2 reporter (12.40) gets the build.

**Result (Test request 29, build19, firmware 13.60), Patryk:** "Map is working better now and I
haven't noticed any wrong frames displayed. I couldn't test rune popup but it doesn't matter now."
He sent no logs, so which export table was picked is not confirmed.

## 2026-11-07 (build18): autosave flashes, the rune window's pose, and the UI blend fix (built, untested on console)

**Built, untested on console.** Three fixes, each behind a switch in
`/data/botwvr/launcher/bettervr.json`, default 1; 0 gives build17's behaviour.

- **saveHold (patch 0024, renderer_vulkan.cpp).** The flashes after chests, dialogs and cutscenes
  are BotW's autosave snapshots. While the game takes one during play, it renders that frame with
  its own flat camera, and the port published it: a mono picture on the HUD quad, or the
  game-camera frame as a stereo pair. Now that frame is not published; the last stereo pair stays
  on the submit thread and Hmd2 keeps showing it with live head tracking for 3 publishes (~50 ms
  at 60 Hz). The inventory and the Camera rune keep their mono path unchanged. The "not in game"
  term of the mono gate now waits 8 Cemu frames, so a camera hand-over no longer makes a mono
  picture; that wait is saveHold's too, so 0 leaves the raw test. One `Save snapshot: …` line per
  autosave; the `[vr-xr]` line gains `snapshot holds N`.
- **poseKeep (runtime.cpp).** When the submit thread shows the same pair again, its render pose is
  kept past the 256-entry located ring (about 0.7 s), instead of falling back to the live head. The
  frozen scene then stays in the room and a head-locked HUD drawn in that pose (the rune/tips
  window) stays on the head. If a stereo pose is still unknown, the HUD quad is placed with the
  live eyes, the same frame Hmd2 is given, instead of the projection's stale pose. Logs: one
  `render pose kept for a re-shown pair (shown N frames)` at the onset, and `kept N` in the
  `[vr-xr]` line.
- **uiAlpha (patch 0025, settings.cpp).** Upstream's UI blend fix (separate alpha `ZERO, DST_ALPHA`,
  `patch_ImproveGUI.asm`) was left to Windows by patch 0001, so on PS5 the minimap, dialogue boxes
  and other 2D elements overwrite the alpha of earlier draws and leave holes in the HUD picture: a
  rectangle of the frozen picture under the map. It now runs on PS5 too. One
  `UI blend fix: separate alpha was N` line the first time the hook fires. The same patch turns on
  upstream's debug screen log for PS5: `Screen X visible` / `Screen X hidden`, one line per change,
  keyed on `IsScreenVisible` (not `IsOpen`, whose pointers persist).

**Test request 28 (build18).** Install the build and start Breath of the Wild in VR.

1. **Flashes.** Open a chest, finish a dialog and finish a cutscene, then keep playing for ~2 s
   each. Did anything other than the game flash? If yes: a small flat picture with darkness
   around it, or the whole view jumping to a closer picture for an instant? Roughly when? Repeat
   the same three triggers with `"saveHold":0` and say whether the flash returns.
2. **Rune or tips window.** Use any shrine pedestal, or a tutorial window with a video. Keep your
   head still 2 s, turn slowly ~20° left and hold, then ~20° right. Does the window (a) run ahead
   in the direction you turn, (b) lag and drift back when you stop, or (c) stay fixed in the room?
   Does the shrine behind it stay in the room, or move with your head? Is it right for the first
   second, then wrong? Repeat with `"poseKeep":0`.
3. **Map.** Open the map. What does the rectangle look like: solid (which colour), darker or
   see-through, or a piece of scenery? Does it stay at Link's arrow when you move the cursor or
   zoom, or at one spot of the view? Does it point the way Link faces? Is it the same with each eye
   alone? Open the map once right after a cutscene and once from normal play: black in one and
   scenery in the other? Repeat with `"uiAlpha":0`; does the rectangle change or disappear? With VR
   off, is it on the TV? Check a dialogue box and the minimap with `uiAlpha` on and off: a dark
   backing over the bare 3D must not go invisible.
4. **Magnesis.** With an object held (grip held): T1 keep still and turn your head slowly 45° left
   and right; does the object swing sideways with your gaze? T2 push the right stick left/right
   with smooth turn (launcher: turning = smooth); does it move sideways? Then snap turn 45: does
   one flick jump it sideways? T3 hold the left stick (not right) left/right and tap A / face
   buttons while holding the object: does anything rotate it? Also say which turn mode you were
   on, and how you rotated objects on Wii U/Switch/PC.
5. **A/B.** Run once with each of `"saveHold":0`, `"poseKeep":0`, `"uiAlpha":0` in
   `/data/botwvr/launcher/bettervr.json` and say what comes back. The session-start lines name
   each value; `Save snapshot`, `render pose kept` and `UI blend fix` lines stop when their switch
   is 0.
6. **Send** `/data/botwvr/launcher/logs/boot.log` (or Settings > Diagnostics > Copy logs to USB):
   the session-start lines for `saveHold`, `poseKeep` and `uiAlpha`; every `Save snapshot`,
   `render pose kept`, `UI blend fix`, `Screen … visible/hidden`, `Event`, `Menu/mono
   presentation` and `Menu lock` line; the `[copy]` lines; and the last two `[vr-xr] … frames`
   lines from each run. Add the times you opened the map and the window, and which run each log is.

**What it decides.** Flashes gone with `saveHold` on and back with it 0: the autosave frame. Rune
window fixed with `poseKeep` on, `kept` growing ~540 per window and `not found` flat: the pose
mismatch. Still trailing only with `poseKeep` 0: Hmd2's half-rate correction, and the room latch is
the next step. The map rectangle gone or different with `uiAlpha` 0: the alpha hole. Rectangle on
the TV with VR off: Cemu's side, not this. Magnesis unchanged by every switch: upstream's behaviour,
not a regression of ours.

## 2026-11-07 (menus): the mono picture rides the HUD quad, menus stay fixed in the room, quads are planes (not built, not run)

**Not built, not run.** Three switches in bettervr.json, all default 1; 0 gives the old behaviour.

- **monoQuad (runtime.cpp, compose.comp).** The compositor copied the one mono image 1:1 into both
  eyes, whose display FOVs are mirrored, so the right eye saw it about a third of the eye width to
  the right: each eye its own Link. Now a mono projection is drawn on the HUD quad (the 16:9 screen,
  the same in both eyes) with black around it, and the HUD blends over it.
- **menuLock (patch 0023, renderer).** While a menu, popup (message dialog, shop) or flat view is
  open, the HUD quad is latched in the stage in front of the head (2 m, pitch limited to 25 deg) and
  stays; it follows the head again after 15 submit frames (~0.25 s) with no menu, so a flicker does
  not re-latch. A black-bar cutscene window is baked into the eye image, so it is never locked.
- **quadPlane (runtime.cpp, compose.comp).** The quad was drawn as the upright bounding box of its
  corners, so head roll stretched it. Now a per-eye homography draws the real plane (needed for a
  quad fixed in the room, seen off-axis).

Logs: session lines `menu/flat picture:`, `menus and popups:`, `HUD quad:`; `Menu lock on/off` (yaw,
position); the `Menu/mono presentation` line ends with `monoQuad=, menuLock=, quadPlane=`; the
600-frame `[vr-xr]` line gains `mono on quad N, plane quads N`.

**Test request 27 (build with patch 0023).**
1. Open the inventory and keep it open 10+ s; look around and tilt your head. Is there one Link in
   his frame, and does the menu stay put in the room, undistorted by the tilt?
2. Trigger a popup (an item get, a message box) and look around while it is up. Does it stay put?
3. Use the scope or binoculars: one picture under the reticle? (Locked in the room too; tell me if
   that is wrong for the scope.)
4. Send the `boot.log` lines `Menu lock`, `Menu/mono presentation` and `[vr-xr]` (the last 2).
5. A/B: run with each of `"monoQuad":0`, `"menuLock":0`, `"quadPlane":0` in bettervr.json and say
   what comes back.

## 2026-11-06 (inventory and scope HUD): a menu keeps its mono frame, the HUD keeps each eye's place (not built, not run)

**Not built, not run.** Two symptoms, two causes. The inventory's Link was still doubled and its
screen sat in the wrong place; the scope's HUD was offset between the eyes.

**The menu gate (inventory).** `menuMono` was `frozenFrame || flat || IsScreenVisible(PauseMenu_00)
|| IsScreenVisible(PauseMenuInfo_00)`. `frozenFrame` is the game's temporary layer copy
(`m_cameraIsCapturing3DFrameBuffer`), which only lasts while the menu is opening (~1 s in the
log); `IsScreenVisible` covers the same opening transition. The log's `pause=yes` came from
`IsScreenOpen(PauseMenu_00)`, whose pointer also exists while scoping, so it named nothing. Once
those expired, the game's mono inventory pair was published as stereo and each eye got its own
Link. Upstream keeps 3D disallowed for the whole menu with `!IsInGame()`
(`renderer.cpp:42-44`, `cemu_hooks.h:147-150`): the gameplay camera stops updating while the
inventory is open (the log is `in_game=0` for the whole menu), plus `IsScreenOpen(PauseMenuInfo_00)`
(`renderer.cpp:48-50`). Patch 0022 adds that `!IsInGame()` term, excluding an active cutscene
(which also stops the camera but is still a stereo frame), and logs the real inputs.

**The HUD place (inventory and scope).** The mono projection sent the left eye's pose and FOV in
both views so both eyes saw one picture. The runtime's `QuadInEye` places the HUD quad from the
projection views, so the right eye's HUD was placed at the left eye's position: the scope HUD was
offset between the eyes and the inventory screen sat to one side. `port/bettervr/xr/runtime.cpp`
now detects a mono projection (identical pose and FOV in both views; ~20 lines) and places the HUD
quad with the real per-eye poses and FOV from the head/eye offsets. The mono picture is head-locked
too: it is presented from the display-time head, not from the located stereo render pose (that
search ignores y, so it could match an older head height and timewarp the menu against the head).
Normal stereo and the HUD-only path are unchanged. Both eyes still get the same left picture; no
Link per eye.

**Logs.** `Menu/mono presentation on (pause=open|visible|no, frozenFrame=..., flatCamera=...,
flatView=..., inGame=..., projection=mono capture, shared pose, layer=projection + HUD quad,
head-locked)` is one line per switch of the picture, at most one a second; `... off (...)` says
`projection=stereo`. The line follows the picture, not the raw menu camera: `flatView` is the
debounced scope flag, so the 8-frame tail after the scope closes is not logged as stereo. The Flat
view line ends with `layer=mono projection, shared pose, HUD quad, head-locked` (or
`layer=opaque HUD quad` with `"flatScene":0`). `"flatScene":0` still restores the pre-0020
behaviour (old HUD-only flat quad and the stereo menu).

**Test request 26 (inventory and scope HUD).** Install the build. Open the inventory and stay in
it for 10+ s, then turn your head left and right and stand up/crouch; Link must appear once and the
menu must not be duplicated or shift between the eyes or against the head. Then enter the scope,
then the binoculars/Camera rune;
the scope HUD must be at the same place in both eyes. Send the `Menu/mono presentation ...` lines
around opening/closing the inventory, the `Flat view ...` lines for the scope and binoculars, and
say whether Link/screen/HUD stayed single and aligned. Optional control: add `"flatScene":0` to
`/data/ps5cemu/bettervr.json`, restart, and the old doubled/stereo menu and HUD-only flat quad must
return.

The twenty-two BetterVR patches apply in order on pin `2e06b31` (22/22; 0022 is new);
`renderer_vulkan.cpp` and `port/bettervr/xr/runtime.cpp` pass
`clang++ -std=c++23 -fsyntax-only -D__PROSPERO__` (0 errors, pre-existing warnings only). Not
compiled for the console.

## 2026-11-06 (shrine left-eye judder): the left eye's 3D slot is clear of the published pair (not built, not run)

**Not built, not run.** The 3D intermediates are double-buffered on the game's 0/1 magic frame
counter. The game draws R then L, so pair j lives in slot C_j. At 52-58 fps the submit thread's
60 Hz copy of pair j can be queued after the game has recorded L(j+2) into left[C_j]; the left half
then shows a two-game-frame-newer image with pair j's pose, while the right half is exact. At about
32 fps (the overworld) the left copy always happens before the overwrite, so it is smooth.

**Patch 0021.** With `bettervr.json`'s `leftSlot` on (default), the left eye's colour and depth copy
goes to the slot neither the published pair nor the completed-but-unpublished pair holds; with
three slots that is always exactly one. The right eye keeps the game's 0/1 slot, which the game
only rewrites after the next publish. The third left intermediate is 1984x2048 RGBA8 plus D32,
about 31 MB (at 2000x2040, about 31.1 MB); with `"leftSlot":0` nothing extra is allocated. Depth
follows the colour slot. The flat view / menu mono path samples the published left slot through the
same bounds, and the comfort fade has a framebuffer for the third slot. With `leftSlot` on the fade
overlay pipeline is created with the renderer, before the submit thread starts: its first shader
compile must not sit between the submit thread's pair snapshot and the copy, where a cold compile
could outlast the ring and let the game reuse the slot being read. `"leftSlot":0` keeps the old lazy
creation and the old double-buffered boot line.

**Proof counters.** The `[vr-xr] ... frames:` line now carries
`leftCopyOverwritten <n> rightCopyOverwritten <n> of <n> copies`: each recorded 3D colour write
bumps its slot's generation, the pair carries the generation it was copied with, and the submit
thread counts a copy whose slot's generation changed before it queued the copy. With
`"leftSlot":0` the left counter should rise in a shrine (the predicted race) and the right stay 0;
with the default the left must stay 0. The counter compares recorded writes, so a write recorded
but not yet submitted can over-count slightly; the right eye is the control.

**Diagnostic `eyeSource`.** `bettervr.json` `"eyeSource":1` shows the right eye's image and pose to
both eyes, 2 shows the left's, 0 is normal. One line in the projection loop, about 20 lines with its
cached read and log; it is not needed for the fix.

**Test request 25 (shrine left eye).** Install the build. In a shrine, turn your head fast left and
right for 30 s and report whether the left eye still jumps (and whether it is as smooth as the
right). Send the `[vr-xr] ... frames:` lines, the `[perf]` lines, and the session line
`left eye's 3D slot: ...`. Then add `"leftSlot":0` to `/data/ps5cemu/bettervr.json`, restart, repeat
the same head turns, and send the same lines plus whether the jump came back. Expected: default
`leftCopyOverwritten 0`; `"leftSlot":0` `leftCopyOverwritten > 0`, rising in bursts;
`rightCopyOverwritten 0` both times.

The twenty-one BetterVR patches apply in order on pin `2e06b31` (21/21; 0021 is new);
`entity_controller.cpp`, `renderer_vulkan.cpp` and `port/bettervr/xr/runtime.cpp` pass
`clang++ -std=c++23 -fsyntax-only -D__PROSPERO__` (0 errors, pre-existing warnings only). Not
compiled for the console.

## 2026-11-06 (melee hits, flat view, inventory): the weapon guard resets each game frame; flat shows the scene, a menu's mono frame is shown once (not built, not run)

**Not built, not run.** Two bugs, one cause each.

**Weapon (0019).** On Windows `BeginStereoCaptureGeneration -> RenderFrame::Reset()` clears
`ranMotionAnalysis` at the left eye's 3D color clear. The Linux path never calls it, so after one
update per hand the duplicate gate in `weapon.cpp` returned forever; the `[weapon]` line kept printing a
stale `slash 8.07 / accel 25.79`. Patch 0019 runs the same reset (every `RenderFrame` field, with
`CaptureIssue_MonoCapture` preserved, and a new stereo generation) at that same left-eye clear, once
per game frame. The `[weapon]` line now carries `upd L <n> R <n>`, the analyser `Update()` calls per
hand: before the fix it stopped at 2 for the session; after it must rise about once per game frame.

**Flat view and menu (0020).** The flat path used to suppress the 3D layer and submit the 2D capture
as an opaque quad, so the scope showed the HUD capture's cleared background (black). It now submits
the mono/left 3D capture as the flat picture, with the HUD quad blended over it by its alpha, the
way upstream composites its main background and HUD. A pause/inventory frame is a mono capture; it
is published marked mono and the submit thread presents that one image to both eyes with the HUD
over it, so Link appears once. A mono pair is never projected as stereo, and no stale stereo world
is kept behind the menu. The flat log line names the quad's source:
`Flat view on (scope=true, CameraFinder=false, quad=2000x2040 side=left)`. A switch line names the
menu state: `Menu/mono presentation on (pause=yes, frozenFrame=no, projection=mono capture, both
eyes)` and `... off (...)`. `bettervr.json`'s `"flatScene":0` restores the old HUD-only flat quad
and the stereo menu; the session line names the value:
`flat/menu presentation: ... (bettervr.json's flatScene, default on)`.

**Test request 22 (weapon).** Install the build, draw a weapon in first person and swing at an
enemy. Send the `[weapon]` lines around the swings. `upd R` must climb by about one per game frame
(not stay at 2), a swing should print `analyser slash active yes` and `[weapon] swing accepted`.
If `upd` climbs but the lock stays `none`, send the `slash`/`accel` numbers against their gates; if
`active yes` appears but the hit does not land, the sensor write is the next suspect.

**Test request 23 (scope/binoculars).** Enter the scope, then the binoculars/Camera rune. Send the
`Flat view on (...)` line. The headset must show the game scene with the scope UI over it, not a
black screen; the line must name `quad=<w>x<h> side=left`. Optional control: add `"flatScene":0`
to `/data/ps5cemu/bettervr.json`, restart, and the old black HUD-only quad must return.

**Test request 24 (inventory).** Open the inventory/pause menu. Send the `Menu/mono presentation
on (...)` line. Link must appear once, not once per eye, and the line must say
`projection=mono capture, both eyes` (or `projection=none, HUD over black` if no mono pair has
completed yet). Optional control: `"flatScene":0` should bring the doubled Link back.

The twenty BetterVR patches apply in order on pin `2e06b31` (20/20; 0019 and 0020 are new);
`weapon.cpp`, `entity_controller.cpp`, `renderer_vulkan.cpp` and `port/bettervr/xr/runtime.cpp`
pass `clang++ -std=c++23 -fsyntax-only -D__PROSPERO__` (0 errors, pre-existing warnings only). Not
compiled for the console.

## 2026-11-06 (stereo pose): one view pair for both eyes, `poseFreeze` on by default (not built, not run)

**Not built, not run.** The left eye could be rendered with one pose and submitted with a newer one.
The game builds the left camera early in its pass and copies the left image later; `StartFrame` on
the present path could promote the submit thread's `pendingViews` in between. The pair's submitted
anchor is the left copy's snapshot, so the left half was reprojected from a pose it was never
rendered with while the right half (built after the promotion) was exact. At ~53 game fps against
the 60 Hz present the promotion drifts through the frame, which is the periodic left-only jump on a
fast back-and-forth; at ~32 fps the present lands at the frame boundary and it does not show.

**The fix.** Patch 0017 adds `bettervr.json`'s `"poseFreeze"` (read once, default on). With it on,
the left `hook_BeginCameraSide` promotes the pending views once and freezes them; nothing else may
promote until the right `hook_EndCameraSide`, after the right image is drawn. Both eyes' cameras and
both copies of the pair therefore use the one view pair the pair is submitted with. `StartFrame`'s
eager latch is refused while the pair is frozen and the left pass promotes instead. `"poseFreeze":0`
runs the old per-read latch unchanged. The session-start line names the value:
`stereo pose freeze: on, one view pair for both eyes of a pair (bettervr.json's poseFreeze, default on)`.

**Proof counters.** The per-600-frame `... frames:` line now carries `leftPoseStale`,
`rightPoseStale` and `poseAgeMax` (ms): each completed pair compares the display time each eye's
camera was built with against the pose the pair is submitted with. With the freeze on, all three
stay `0`/`0`/`0.00`; the control run with `"poseFreeze":0` should show `leftPoseStale` rising during
fast yaw at ~53 fps, which is the stale left half. The same line carries `poseGuardClears`: the
StartFrame guard clears a freeze that outlived four frames, and that count must stay `0`.

**Test request 21.** Install the build, enter a shrine and move the head quickly left and right for
~10 s. Send the `... frames:` lines that contain `leftPoseStale`. Default run: `leftPoseStale 0
rightPoseStale 0 poseAgeMax 0.00 ms`, and the left image must be as smooth as the right. Optional
control: add `"poseFreeze":0` to `/data/ps5cemu/bettervr.json`, restart the game, and send the same
lines; the session line must say `off`, and the stale counters may rise.

The eighteen BetterVR patches apply in order on pin `2e06b31` (18/18; 0018 is rebased on 0017);
`camera.cpp`, `entity_controller.cpp`, `renderer_vulkan.cpp` and `port/bettervr/xr/runtime.cpp`
pass `clang++ -std=c++23 -fsyntax-only -D__PROSPERO__` (0 errors, pre-existing warnings only). Not
compiled for the console.
## 2026-11-06 (stereo god rays): the right eye's volume mask gets its own masked-light shadow, behind `stereoRays` (not built, not run)

**Not built, not run.** At the Graphics pack's Normal quality the volume mask (god rays) is drawn in
both eyes, but the right-eye pass skipped `gsys::ModelSceneShadow::calcViewMaskedLightShadow`, so the
right eye's mask was built from the left eye's masked-light shadow. `bettervr.json {"stereoRays":1}`
(default) now runs that calc for side 1 too; `{"stereoRays":0}` keeps upstream's right-eye skip
exactly. It is one calc per game frame, not extra draw calls and not a second shadow map.

**`depthShadow_updateShadowMatrix` stays skipped even with `stereoRays:1`.** It fits the cascaded
shadow-map projection to the camera, while the right eye still reuses the left eye's shadow map
(`depthShadow_drawShadowMap` is still left-only); running it would apply a right-eye matrix to a map
that was not redrawn, the mismatch the upstream comment at the top of the file warns about. The
volume mask's per-eye input is the masked-light shadow that `calcViewMaskedLightShadow` rebuilds; if
the right eye still shows a sun-shadow offset after this, the next A/B is the depth-shadow matrix (or
redrawing the map), not this switch.

The host caches `stereoRays` in `BetterVR_PS5_StereoRays()`, writes the guest `stereoRaysEnabled` flag
once at the start of the first game frame, and the session-start line names the same value:
`stereo god rays: right-eye masked-light shadow recomputed (bettervr.json's stereoRays, default on)`
or `upstream's right-eye skip (bettervr.json's stereoRays=0)`.

**Test request 20.** Set the Graphics quality preset to **Normal (Recommended)** and enter the shrine
from logs12/13. Run once with `stereoRays` absent or `1`, then add `"stereoRays":0` to
`/data/ps5cemu/bettervr.json` and run the same spot. Send the `stereo god rays:` line from both runs.
With `1`, the rays/volume mask should sit in the same place in both eyes and the binocular flicker or
soreness should be gone; with `0`, the previous one-eye behaviour must return. An outdoor sun-ray
spot is a useful second look for new shadow acne; say which eye and where if anything is still off.

The eighteen BetterVR patches apply in order on pin `2e06b31` (18/18; 0018 is rebased on 0017);
`entity_controller.cpp` and `port/bettervr/xr/runtime.cpp` pass
`clang++ -std=c++23 -fsyntax-only -D__PROSPERO__` (0 errors, pre-existing warnings only), and the
pack's address-materialization check is clean on all 32 asm files. Not compiled for the console.

## 2026-11-06 (melee swings): the hand velocity is always published, and `[weapon]` says what the analyser decides (not built, not run)

**Not built, not run.** The runtime's `xrLocateSpace` used to attach `XrSpaceVelocity` only when
`ReadHands` had two poses within 1-200 ms; otherwise the struct kept `velocityFlags = 0`, which
BetterVR accepted anyway because `openxr.cpp` tested the velocity bits in the **location** flag word
(their values are `POSITION_VALID`/`ORIENTATION_VALID`, already required, so the test always
passed). Every hand now keeps its last three poses; the locate publishes linear velocity from the
position change and angular velocity from the quaternion delta, with `dt` from Hmd2's display
timestamps, light smoothing over the two intervals, rotated into the locate call's base space, and
both valid bits set whenever the pose is valid. BetterVR patch 0016 checks `velocityFlags` (and
clears it before the locate), so a zero is no longer silently accepted as valid.

**`[weapon]` (INFO, always on; `LogControls` can stay off).** At most one line a second, plus one line
when the analyser locks a swing or lets a motion end without locking. The line has first person
yes/no, the hand and whether the weapon is hand-bound, the weapon type, both hands' linear speed in
m/s (current and the max over the last second), and the analyser's decision against the slash and
stab gates.

**Test request 19.** Install the build, draw a weapon in first person and swing at an enemy. Send the
`[weapon]` lines around the swings. `speed L … R …` must be nonzero while the hand moves (a zero
there is a real measured zero or no pose, never a hidden "velocity invalid"); the `analyser` field
shows `slash`/`stab`/`none` and `active yes/no`, and the current values against the `gate` values
show whether a swing misses on speed or acceleration. A `swing accepted` line means the analyser
armed the hitbox; a `swing rejected` line means the motion ended without it.

The sixteen BetterVR patches apply in order on pin `2e06b31` (16/16); `weapon.cpp`, `weapon.h` and
`openxr.cpp` pass `clang++ -std=c++23 -fsyntax-only -D__PROSPERO__`, and
`port/bettervr/xr/runtime.cpp` passes with its pre-existing `-Wreturn-type-c-linkage` warning
(0 errors). Not compiled for the console.

## 2026-11-06 (VR presentation): the game fade is a ramp, the scope is its 2D capture (not built, not run)

**Not built, not run.** Patch 0015 keeps upstream's flat view, cutscene bars and comfort fades, and
changes two of them where the last build was wrong. **The game fade is never sampled from the 2D/HUD
capture any more.** The submit thread ramps a solid black overlay over the eye image: while
`IsAnyFadeScreenVisible()` is true the target is 1, when it ends the target is 0, and the applied
value moves at most 1/12 per presented fresh 3D pair (a full swing in 12 frames, ~0.2 s at 60 fps).
The target is the stronger of that game fade and the snap-turn/roomscale amount, so a fade screen
that appears and disappears every frame cannot strobe the scene fade; the on/off decision is still
taken once per Cemu frame. The HUD quad is unchanged and never faded. `bettervr.json`'s `"fades": 0` (read once,
default on) turns the game fade off; `BetterVR_PS5_Fades` serves the same cached value to the
renderer and the session-start line (`game comfort fades: on/off`).

**The HUD quad is deterministic.** The old Linux path took the first `captureIdx==2` image of either
eye and reopened the once-a-frame window at every 3D color clear, so the two 2D images could swap.
It now takes the **left eye's first** capture per presented frame, exactly where upstream accepts
`CaptureMask_Hud` and rejects the rest (`framebuffer.cpp:262-290`, `TryAcceptCapture`), and the only
reset is `QueuePresentKHR` (`framebuffer.cpp:680`). The once-per-fade line reports what the quad
carried: `[fade] game fade <start>-<end>s (first-frame clock): max applied <v>, HUD capture idx 2
<w>x<h> image <ptr>, changed <n> time(s)`. A next log with `changed 0` shows one image and size for
the whole fade.

**The scope is the 2D capture.** In the scope and the Camera rune the eye image is no longer baked
with a window from the 3D eye (that capture is black in this mode). The 2D capture is submitted as
the **opaque** HUD quad at the normal 2.0 m/1.0 m window size and no projection layer, as upstream's
flat camera presentation does; normal play keeps source-alpha blending. Black-bar cutscenes keep the
baked window. A flat frame is still never reused when the next frame publishes only 2D/HUD content.

**Test request 18.** Install the build. Enter a shrine (the Magnesis one was the strobe) and send
the `[fade] game fade …` line; with the fix it should say `changed 0`, and `max applied 1.00` if the
fade lasted more than ~0.2 s. Look through the scope and the Camera rune: the scope picture should
be opaque, centred, at the normal HUD size, and the screen should not go black; normal play's HUD
must look as before. Optional control run: add `"fades": 0` to `/data/ps5cemu/bettervr.json` and
confirm the session line says `game comfort fades: off` and shrine fades no longer darken the scene.

The fifteen BetterVR patches apply in order on pin `2e06b31` (15/15); `renderer_vulkan.cpp`,
`entity_controller.cpp` and `port/bettervr/xr/runtime.cpp` pass
`clang++ -std=c++23 -fsyntax-only -D__PROSPERO__` (0 errors, pre-existing warnings only). Not
compiled for the console.

## 2026-11-06 (hook host cost): the hot hooks stop copying, and `[diag]` says what they cost (not built, not run)

**Not built, not run.** Patch 0014 keeps every hook's decisions the same and removes the per-call work
around them: `hook_RouteActorJob` reads only the actor name (0x4C bytes, in place, compared without a
`std::string`) instead of copying `ActorWiiU` (0x538 bytes) and building two strings per actor job;
`hook_ModifyBoneMatrix` compares the model name without a `std::string` and keeps the bone name as a
`string_view` (heterogeneous skeleton map), so a non-player bone no longer allocates;
`hook_CheckIfCameraCanSeePos` reads the XR views and the gameplay base pose once per call instead of
once per eye and passes those views into the eye pose; `hook_SetActorOpacity` reads only
`modelOpacity`; `IsTrackedPlayerBody` reads three player fields instead of copying `PlayerBase`
(0x12A8 bytes) per rigid-body setter, and the ROOMSCALE setters return before reading when that log
category is off; `ApplyStoredActionFloatParamOverrides` asks `IsFirstPerson()` once a frame; the
profile hooks return at once when the profiler is off.

**`[diag]` timing.** Every 64th call of the five hottest hooks is timed with `steady_clock`, and the
existing `[diag]` line ends `hooks ms/s route … bone … cam … prof … vpad … calls/f …` (`prof` is
begin+end). The next console log must show which of the five dominates; expected in the current
31.6 fps world: `cam` and `bone` the largest, `route` and `prof` next, `vpad` last. `calls/f` says how
many times each hook runs per game frame. No game logic, gating, default or 0013 word changed.

**`fastTls` (report only, default unchanged).** Every HLE call returns through
`PPCInterpreter_getCurrentInstance()`, a `thread_local` read; with `fastTls` off that is a libkernel
`pthread_getspecific` per hook call, so it adds to all five. `fastTls=1` would help most where
`calls/f` is highest, but it stays off until a console A/B measures it.

**Test request 16.** Install the build, play the same world spot for one minute, and send the first
and last `[diag]` lines carrying `hooks ms/s` plus the `[phys]` line. The five `ms/s` values and
`calls/f` are what the ranking lacked; the game must behave as in the previous build.

The fourteen BetterVR patches apply in order on pin `2e06b31` (14/14); `settings.cpp`,
`skeleton.cpp`, `camera.cpp`, `controls.cpp`, `entity_controller.cpp` and `profiler_hooks.cpp` pass
`clang++ -std=c++23 -fsyntax-only -D__PROSPERO__` (0 errors, pre-existing offsetof/memcall warnings
only). Not compiled for the console.

## 2026-11-06 (player physics): the gate is armed once a game frame, and `playerOnce` is on by default (not built, not run)

**Not built, not run.** Patch 0013 replaces the host frame latch with Opus's guest-memory design.
`custom_sead_GameFramework_procFrame` arms `playerVelocityUpdateDue` where it writes
`currentEyeSide = 1` for the right-eye pass (after `playerVelocityUpdateDueFrame` gets the game
frame). `OnlyRunPlayerUpdateJobOnce` takes that word with an `lwarx`/`stwcx.` loop: the first call
after the arm runs the player's velocity update and clears the word, whichever pass or core asks
first, and a call that arrives in a later frame still consumes that frame's due instead of being
dropped. `bettervr.json {"playerOnce":0}` writes 0 into the guest `playerOnceEnabled` flag once and
the asm then runs upstream's `currentEyeSide != 0` test exactly; the default is on. The host caches
`playerOnce` in `BetterVR_PS5_PlayerOnce()`, writes the flag at the first `hook_PhysFrame`, and the
session-start line names the same cached value, so restarting only the game cannot mislabel the gate.
The `0x0344DCF8` tail hook is removed, so the roomscale entry's registers (r5 included) are the
upstream ones again. `OnlyRunPlayerUpdateJobOnce` saves the caller's r3-r12, CR and CTR in its own
frame and puts them back before the `bctrl`, so the player's update sees the registers and CR0
upstream's hook gives it even though the take, the host hook and the run/skip branch sit in between.

**`[phys]` (2 s, at most 1200 lines).** The line is now only what decides the fix:
`[phys] t=… frames=… mode=once|upstream entries L=… R=… runs=… skips=… runs/f 1.00 late=…
cores entries 0:… 1:… 2:… runs 0:… 1:… 2:…`. `frames` is the game-frame count from
`physFrameCounter`; `runs/f` is the one number: about 0.02-0.04 before the fix, 1.00 expected after;
`late` counts runs that took the due word at least one game frame after the frame it was armed for
(a take in the same or next frame is on time, `frame - dueFrame` 0 or 1, so `late` stays near 0).
The old tail, lag, gap, expected-pass and zero-run instrumentation (review findings 2, 3, 4, 7) is
gone.

The thirteen BetterVR patches apply in order on pin `2e06b31` (13/13, tree identical to the
patched tree); `entity_controller.cpp` and `controls.cpp` pass `clang++ -std=c++23 -fsyntax-only
-D__PROSPERO__` (0 errors), and `runtime.cpp` passes with its pre-existing `-Wreturn-type-c-linkage`
warning. Test request 15.

## 2026-11-05 (seated height): the calibration runs on the first in-game frame if the loading screen was missed

**Not built, not run.** Build 4e0676d has no `Seated height calibrated` line in logs8 or logs10. The
only automatic request is the first `LoadingWeapon_00` screen, and the submit thread that calls
`UpdateSeatedHeightCalibration` starts at 9.2 s, after the game (7.0 s) and the PS5's boot load;
logs8's first `in_game=1` is at 41.2 s with no calibration line, so the trigger never fired while the
submit thread was running. Patch 0011 now also requests the calibration on the first in-game frame with a
valid head pose when Seated and not yet calibrated. The stage space moves once, so the view and the
hands share it and the 1.65 m lift stops; Standing keeps the real height. The log gains
`Seated height calibrated: head at ...m above the tracker origin, stage origin moved by ...m; views
and hands share it`, and `Seated height calibration skipped: ...` once per reason (Standing, waiting
for the game, no valid head pose, the runtime's recentre time). `port/bettervr/xr/runtime.cpp` queues
no `XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING` and hmd2 has no recentre call, so a PSVR2
recentre does not re-run it; the upstream handler is already there if a runtime sends the event. The
next log must show one calibration line before the first `[diag] ... in_game=1`, a measured head
height near the player's seated tracker height, and hands at the view's stage offset with no 1.65 m
lift afterwards.

## 2026-11-05: the TV shows the headset's view during VR (not built, not run)

**Why the TV was black (PROVEN for the code and the log, INFERRED for the console's routing).**
BetterVR's runtime started Hmd2 with `Config::mirror = false` (`port/bettervr/xr/runtime.cpp:937`
before this change), so nothing ever asked Hmd2 for its social screen. The logs agree: VR mode was
enabled (`logs8:152`, `logs9:152`, `ReprojectionEnableVrMode -> 0x00000000`) and no `TV mirror` line
was written: with the flag false the mirror exports were never looked up (`hmd2.cpp:992-1002`) and
`mirror_start()` never ran (`hmd2.cpp:1142`). Flat play makes Cemu's own VideoOut surface at 30.295 s
(`logs6:80-81`, `[vulkan] VideoOut surface 3840x2160 at 59.94 Hz`), and the VR runs make none after
4.38/4.64 s: the bus-32 port opened at 8.912/9.114 s is the headset's, not the TV's. Cemu's present
still runs (`framebuffer.cpp:672-704`), and the Linux desktop mirror is off unless
`BETTERVR_DESKTOP_MIRROR` is set (`linux_desktop_mirror.cpp:301-310`), so neither is the TV path.
What is inferred is the console's own part: once VR mode is on, the TV gets Hmd2's social screen
(`hmd2.h:80-85`), the path both reference runtimes use with option word `{1}`
(`hmd2.cpp:855-917`, "as Horizon does"), and with it off the TV has no source. photo-splat-vr in VR
mode shows nothing on the TV for the same reason (`STATUS.md`, M2).

**The change (not built, not run).** `"tv": 0` in `bettervr.json` turns the TV image off (default
on), read once at session start like `reproject`. The runtime passes it as `Config::mirror`, so
Hmd2's compositor puts the headset's view on the TV; the app adds no blit, no present and no queue
use, so it cannot block the submit thread or `ps5xr::QueueMutex()` (port-audit section 2 row 19's
concern). The session-start line says `TV mirror on: Hmd2's social screen shows the headset's view
on the TV ...` or that Hmd2 did not take it, and the 600-frame `[vr-xr]` line ends with
`TV mirror on, shown N skipped M`. No BetterVR file changed, so `patches/bettervr` stays 0001-0012;
all twelve apply in order on the pin. `port/bettervr/xr/runtime.cpp` passes
`clang++ -std=c++23 -fsyntax-only` against OpenXR 1.1.49's headers and Vulkan-Headers 1.4.363, with
the pre-existing `-Wreturn-type-c-linkage` warning on `ReadAction`. Test request 14.

## 2026-11-05 (fifth run): the offset is gone; the physics is late, not slow; a roomscale switch

**Reported by Patryk (build 45a0bc3, Test request 12, Graphics pack off).** "Offset is gone,
tracking is really good now and very smooth. Sense works, I can control the game." The physics did
not change: "Phisics - still very off." Flat play is the control: "runs super stable 60fps", and its
physics is normal. The pack-on step of Test request 12 was not reported.

**What the 193 `[diag]` lines show (logs7; proven from the log).** The body is moved late and by the
wrong amount, not slowed. Forward from 79.38 s: x does not move until 80.28 s (0.9 s), then 5.3 u/s
at once; from 82.86 s the first motion is at 84.3 s (1.45 s); another start 57.74 -> 59.43 s (1.7 s).
The delay differs each time, so it is not a fixed 2-4 frame pipeline (60-120 ms). After release at
63.72 s the body keeps moving to about 69 s at 4.7 u/s; the ~11 u it covers is about the distance the
held period would give, i.e. the input is played back late. With no stick, y changes at constant
1-5 u/s: 102.9-104.9 s rises 197.8 -> 199.7 at 1.0 u/s; 126.5-128.1 s falls 159.3 -> 152.0 at
4-5 u/s; 5 s samples 160 -> 181 -> 190 -> 182 -> 155 -> 167 -> 192 -> 209 over 136-172 s. The
highest y is 208.75 against a ground near 171.5 (37 u up), the lowest 24 u below the start. The
position logged is the actor's world matrix, the one the camera follows, and the animation answers
at once, so the camera follows a body being moved by something else, not a slowed clock. FPS++ reads
~31-33 fps and anim ~0.9-1.0; the hook sees ~32 VPADReads/s; `[clock]` ratio is 1.000. The guest eye-pass
structure is host-independent: `currentEyeSide` is written only by the pack's asm, and the Linux
renderer never writes it. The only BetterVR code that writes Link's physics body is the roomscale
drive; a warp whose support probe lands on the wrong surface can raise the body 0.10 m per step and
overwrite y while gravity integrates. That fits the vertical constant rates and the launches, but no
log line shows a warp and the delay is not explained by it. **No cause is claimed; the physics stays
open.**

**Patch 0010 (not built, not run).** `bettervr.json` `{"roomscale":0}` turns the roomscale body drive
off (default on, as now; read once at the first use, so the app must be restarted after the file
changes). The launcher reloads `bettervr.json` before a write, so Play in VR/TV and Settings > VR
keep the key. The `[diag]` line gains `rs on/off b/c/w` (hook calls, ray casts, warps written to the body
since the previous line), the player's actor jobs per eye pass, and the passes that ended; lines are
also written every 4th VPADRead while Link's height changes 15 cm, and the cap is 2400 lines (8/s
while active, so bounded). A warp count >0 during a launch or while the stick is idle is the
roomscale drive writing the body; 0 with the same movement clears it.

**Patch 0011 (not built, not run).** The submit thread now runs `UpdateSeatedHeightCalibration` as
`renderer.cpp`'s `StartFrame` does, every thread locates in the current `m_stageSpace`, and the
per-frame 1.65 m lift is gone after the calibration; before it the old lift stays, so the start
height is the one earlier builds had, and the first in-game frame requests the calibration if the
first `LoadingWeapon_00` screen passed before the renderer existed (newest entry). The calibration
moves the stage space, so the hands move with the head instead
of sitting 0.65 m too low; the reprojection lookup matches x and z and is unaffected, and the render
pose passed to Hmd2 is the raw head pose as before. The PS5 default `PlayPosition` is Seated (one
line in `mod_settings.h` under `__PROSPERO__`); `PlayPosition=STANDING` in `bettervr_settings.ini`
keeps the real height above the tracker's origin. The launcher's Settings > VR shows Seated as that
default. Hands, the view height, and whether `Seated height calibrated` appears are Test request 13.

**Patch 0012 (not built, not run).** The Linux renderer's constructor now creates an ImGui context,
sets `IniFilename` null, calls `InitSettings`, and loads `bettervr_settings.ini` from
`/data/botwvr/launcher` (the same path the launcher writes), once, before any frame. Nothing is drawn,
no input is taken, and the file is not written back; BetterVR's ImGui is its own local copy, so
Cemu's context is not touched. An absent file leaves the defaults; a malformed line is logged and
skipped (an invalid enum resets to its default), so the next log decides. Settings > VR now reaches
the game.

**CI compile failure (fixed; still not built, not run).** Run 42079383036 stopped in `renderer_vulkan.cpp`: patch 0011's submit and pose threads read `OpenXR::m_stageSpace` directly, and it is private (only `RND_Renderer` is a friend), so patch 0011 now adds `OpenXR::GetStageSpace()` beside the other getters and the five reads go through it; the twelve patches apply in order on the pin, and no other error was found by reading the files the run did not reach against their declarations.

**Test request 13, runs 1 and 2 (build f05d80d; reported by Patryk).** Roomscale on (run 1): 3734
hook calls, 254 ray casts and 90 warps written to the body; roomscale off (run 2): 1224 calls, 0
casts, 0 warps. The body's y spans 171.5 to 208.5 in both, and the player says "same gravity glitches
happen in both": a run with no warp and the same launches rules the roomscale drive out as their
cause. Run 1 has no `Seated height calibrated` line, so `ReplaceStageSpace` never ran and the old
1.65 m lift stayed; the player says "My view height is accurate". The hands are joined: "joined
together no matter what I do but they react to rotating controllers". The cause is
`port/bettervr/xr/runtime.cpp`: `xrCreateActionSpace` stored `HandOf(subactionPath)` while the hand
paths were cached only at the first `xrSyncActions`, which comes after creation, so both hand spaces
held `hand == -2` and `xrLocateSpace` returned the first valid pose binding, the left grip, for both.
`HandOf` now reads the hand name from the runtime's own path table, complete at creation, and the
600-frame `[vr-xr]` line prints both hand spaces' located positions (`hand spaces left ...`, then
`right ...`, 3 decimals) so the next log shows they differ. Not built, not run.

**Accurate barriers (not built, not run).** The VR override is removed after Test request 13: the
player says "I think there were more visual glitches" with the override on, and the fps did not
change (31.6 before and after). A BotW VR start no longer sets Cemu's `vk_accurate_barriers` false,
`SaveConfig` and the flat-start restore are gone, and every `settings.xml` save writes the player's
value again, as before `45a0bc3`; the boot log has no `[vr] Cemu's accurate barriers off for VR`
line. The `[ingame] accurate barriers` toggle still shows and changes the player's own value.

**Backlog:** nothing is displayed on the TV during VR; Patryk tried to record a video and got a
black screen. Not investigated here.

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
hand poses are the tracker's (logs5: hands y 0.59-0.61, head 0.98). The lifted head is about 0.67 m
above the tracker head the hands are measured from, so a hand at the real shoulder or mouth is
0.7-0.85 m outside the 0.35/0.2 m radii; those two gestures may not fire. This needs a decision about
which frame they should use, so it is left for the next console log, not changed here.

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

**Reported by Patryk on his PS5** (firmware 13.60, `m5-botw-vr`, BotW EUR v208 with DLC, unpacked):

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

### Test request 15: the player's physics gate, on by default (this build)

**Which build.** The next console build of `fix/vr-first-frame-sigill`, with `patches/bettervr`
0001-0013. The fix is on with no key: at VR start the boot log should have `player update gate:
one run a game frame, whichever pass asks first (bettervr.json's playerOnce, default on)`, and the
first `[phys]` line appears within a few seconds of `in_game=1`.

**Run 1, normal play, about 3 minutes.** Keep `{"vr": true}` in
`/data/botwvr/launcher/bettervr.json` and leave `playerOnce` out. Play normally: walk forward and
back, jump from a flat spot, fall off a small ledge, and swim. Note the boot log's times for the
jump, the fall and the swim. A jump should peak about 1-1.5 m above the ground and land within
about 1 s, and the body should answer the stick at once, not 0.9-1.7 s later.

**Run 2 (optional control), `"playerOnce":0`.** Quit the app fully, add `"playerOnce":0` beside
`"vr": true`, restart, and repeat the same walk, jump and fall. The boot log should now say
`player update gate: upstream's, the right-eye pass only (bettervr.json's playerOnce=0)` and the
old 30 m jump and delayed motion should return. Remove the key afterwards.

**Send back:** the `player update gate:` line from each run; every `[phys]` line (at most one every
2 s, capped at 1200; the lines around the jump, fall and swim are the ones needed); the `[diag]`
lines with `pos` around the jump and fall; and the `[perf]` lines from the same times. Say roughly
how high the jump went, how long before the body moved, and whether it kept moving after the stick
was released.

**What `[phys]` decides.**

- **The fix working:** `mode=once` and `runs/f` ≈ 1.00 (one update a game frame), with `late` near
  0 (a take in the same or next frame is on time; `late` counts takes two or more frames after the
  arm). The jump peaks about 1-1.5 m and lands within about 1 s.
- **The gate still starving the update:** `mode=once` but `runs/f` far below 1.00, or `entries`
  well below `frames` on both sides: the calls themselves are missing (the job queue), not the
  due word.
- **The fix running but the physics still wrong:** `mode=once`, `runs/f` near 1.00, and the jump
  still huge. Then the gate is no longer the owner, and the next step is the velocity writes
  after the update.
- **The control run:** `mode=upstream`, `runs/f` about 0.02-0.04 and the old behaviour; this shows
  the two gates really differ on the console.

### Test request 14: the TV during VR (this build)

**Which build.** The next console build of `fix/vr-first-frame-sigill`. `patches/bettervr` is still
0001-0012; the change is `port/bettervr/xr/runtime.cpp`, so the boot log should contain a
`TV mirror on: Hmd2's social screen shows the headset's view on the TV` line at VR start.

**Try.** Start the app and Breath of the Wild in VR as usual, with no `tv` key in
`/data/botwvr/launcher/bettervr.json` (the default is on). Watch the TV during the game: it should
show the headset's view, not black. Say whether its shape is right (not stretched, with black bars
where the view and the TV differ). Play for a minute and note whether the headset frame rate feels
the same as before. Then quit the app fully, put `"tv":0` beside `"vr": true`, start it again, and
check that the TV is dark again while the headset still runs; remove the key afterwards.

**Send back:** the `TV mirror …` line from VR start; one or two `[vr-xr] N frames: … TV mirror on,
shown N skipped M` lines for each run; the `[perf]` lines from the same times; whether the TV
picture matches the headset and whether it is stretched; and, if the PS5's own recording records the
TV, a short video. If the video is black while the TV is not, say so: the system recording may use
a different path from the TV.

**What it decides:** `TV mirror on, shown` rising by about 600 between lines means every headset
frame reached the TV through Hmd2; `skipped` rising means Hmd2 refused the mirror (the `hmd2:` lines
above say why). A TV that stays black with the on line is a console result this change does not
explain, and the next step is Hmd2's mirroring option or its work areas, not Cemu's present.

### Test request 13: the roomscale switch, with and without (this build)

**Which build.** The next console build of `fix/vr-first-frame-sigill`: `patches/bettervr` 0001-0012,
including 0010 (the roomscale switch and counters), 0011 (the seated height moves the stage) and
0012 (the ini is read), and Cemu's accurate barriers off for VR. It is not `45a0bc3` and not m9.
The boot log should have `[vr] Cemu's accurate barriers off for VR`; `VR Settings Loaded` appears
only if `bettervr_settings.ini` exists.

**Try, in this order.** The switch is read once per app process, so quit the app fully between the
two runs, not only the game.

1. **Default.** Keep `{"vr": true}` and remove `roomscale` (or set it to 1). Play the same spot as
   in logs7 for about 3 minutes: walk, jump, fall, and note the boot log's time when a launch or a
   fall starts. Send the `[diag]` lines around it.
2. **Roomscale off.** Quit the app, put `"roomscale":0` in `/data/botwvr/launcher/bettervr.json`,
   start the app and the game again, and play the same way at the same spot for the same time. Send
   the same lines.

**Send back:** the `[diag]` lines around each launch and fall, with the `rs on/off b/c/w` fields; the
`[vr] Cemu's accurate barriers off for VR ...` line; `Seated height calibrated` or its absence; and,
if the ini exists, `VR Settings Loaded`. Say whether the view height at the start is as before and
whether the hands stay with the head when you crouch or lean.

**What it decides:** `rs ... w` > 0 during a launch or while the stick is idle means the roomscale
drive wrote Link's body; 0 with the same movement means it did not. The two runs decide whether the
switch changes the launches and the stick delay. `jobs` should be one per pass and `pass` one per
VPADRead call; any other ratio means the eye-pass structure is wrong. The physics cause stays open
until the warps and the A/B say it is or is not roomscale.

### Test request 12: the XR input, the view's aspect, and the Graphics pack switch (this build)

**Which build.** The next console build of `fix/vr-first-frame-sigill`: BetterVR patches 0008 (the
overlay optional, so the XR input reaches the game) and 0009 (the game camera's aspect, the
`graphicsPack` and `reproject` switches). It is not m9 and not an earlier artifact.

**Result (build 45a0bc3, Graphics pack off).** Passes for the picture, tracking and Sense:
"Offset is gone, tracking is really good now and very smooth. Sense works, I can control the game."
The physics is still off; the pack-on and pack-off-again steps were not reported.

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
