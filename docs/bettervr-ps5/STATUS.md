# Status

Newest entries first. Every console result here was reported by Patryk. Nothing else counts as
tested.

Test request 77 (v0.8 haptics) is withdrawn with the haptics work; the requests after it move up one, so
the console keyboard is 77, the tutorial shrine 78 and the cutscene height 79.

## 2026-11-22: a long frame and a drained AX output now say what the emulated cores were doing; built, untested on console

The 16:52 session's warp-in windows hold one 80.2 ms and one 70.4 ms frame (`snd-report.md` §1), and
the audio in flight at the device is 58-64 ms (48 ms queue, a 5.3 ms grain and 5-11 ms in AudioOut).
Cemu queues one 3 ms AX frame per turn of **core 1's idle loop** only (`coreinit_Thread.cpp`'s
`__OSCheckSystemEvents`, `ax_out.cpp`'s `AXOut_update`, `ax_ist.cpp`'s `AXIst_QueueFrame`,
`PS5AudioAPI.cpp`'s target), so a frame on that core 16-22 ms longer than the queue drains it.

**The any-core pump is removed.** It was tried on this branch and is not in this build: the AX
thread is created with attributes `0` (`ax_ist.cpp:981`), so it is affined to the core that called
`AXInit` (core 1 here, `coreinit_Thread.cpp:474`), and only that core queues its messages. Another
idle core's pump could queue one frame but could not produce a block while core 1 sat in a host
call, which is the case the arrival needs. AX pumping is therefore Cemu's own again, byte-identical
to `c73d352`: `__OSCheckSystemEvents` (the pump's only caller) and `ax.h` are the same bytes,
`__OSThreadCoreIdle` differs only by the diagnostics hook above the run-queue check, and
`AXOut_update` differs only by the feed-gap counters below. No `audioAnyCore` key exists.

**What this build adds (cemu 0065, diagnostics only).**

1. The `[stall]` line: one line per frame over 34 ms, at most one every 200 ms, with only this
   frame's numbers: the GPU thread's time waiting for the game, the stereo pass's record and replay,
   the six GPU waits, and the AX feed gap since the last frame and block. The 5 s `[draws]` line
   zeroes the record and replay counters on the same thread later in the frame; a swap just after
   that zeroing now takes the counter as the delta instead of subtracting across it, so the line can
   no longer print a stereo pass of 1.8e13 ms.
2. The `[stall] cores` line, sampled at that instant from threads that already exist: for each
   emulated core, executing the guest's code (its emulated thread's name and priority), inside a
   host call (the HLE export's name, how long it has been inside), idle, or blocked (a GPU
   timestamp, a coreinit primitive, file I/O, as the export's name says); and whether the AX
   thread's frame is queued and not taken, which is the AX thread runnable and not scheduled.
3. The `[audio]` line in the boot log: when no AX frame has been fed for 40 ms, the audio output
   thread (already running every grain, 5.3 ms) prints one line with the same per-core sample. A
   drain at an arrival is then visible even when no GPU-thread frame crossed 34 ms.
4. The 10 s `[perf]` line keeps the AX feed gap: frames fed, the longest gap between frames and
   between blocks, and schedule skips.
5. The AudioOut queue line, every 10 s unconditionally: current fill, lowest fill, longest wait for
   a whole grain, blocks dropped and grains padded.

**Cost.** A few relaxed atomic stores and one TSC read per HLE call, a 40-byte name copy per thread
switch; the `[audio]` sample runs once per grain; the two `[stall]` lines are formatted only when
they print. Nothing on the guest's path waits, takes a lock or allocates.

**Checks.** `tools/tests/test-core-diag.cpp` (the states, the thread name and priority, the export's
name and how long, the wait kinds and the names that classify them, the AX thread's timestamps),
`tools/tests/test-audio-queue.cpp`, `python3 -B tools/lang.py check`, and
`tools/cemu-patches.sh apply` and `tools/bettervr-patches.sh apply` with `PS5CEMU_RELEASE=1` (every
patch ID matching) pass. No host build compiles Cemu, `PS5AudioAPI.cpp` or the Cemu hooks, so the
"BetterVR build" run is theirs. Built, untested on console.

**Test request 82 (v0.8 audio at an arrival, 13.60 console).** Install the `BotW-VR-v...zip` from the
green "BetterVR build" run on this head; VR on, 1.25x as in the last run, `bettervr.json` as it is
(no key to add).

1. **Towers and shrines, five or more times.** Fast travel between towers and to shrine entrances,
   with a 2D or loading screen before as usual, and listen at each arrival for crackle, a dropout,
   or sound that stays behind the picture. Write down roughly where and when (mm:ss in the session)
   each arrival crackled, and which were clean.
2. **One pause menu** (the tower pause) and **one cutscene**, for the same; say if either crackled.
3. **No setting to change this time.** The pump is gone; there is nothing to compare against.

**Send**, from `/data/ps5cemu/logs/boot.log`:

- `LC_ALL=C grep -a "\[stall\]" boot.log`
- `LC_ALL=C grep -a "\[audio\]" boot.log`
- `LC_ALL=C grep -a "\[perf\]" boot.log`

and from `/data/botwvr/cemu/log.txt` (or `cemu.txt` after Copy logs to USB):

- `LC_ALL=C grep -a "PS5 AudioOut: queue" log.txt`

plus the list of arrivals that crackled, with their approximate times.

**What it decides.** On the same second as a crackle: `lowest 0-5 ms` and `grain late by 16-22 ms`
on the queue line, with `[audio] no AX frame for 40+ ms`, is the drain. The `[stall] cores` line then
splits the two cases the owner asked about. If **core 1 is executing the guest's code** through the
gap, the AX thread was runnable and only a scheduling point was missing: a 3 ms frame source that is
not tied to core 1's idle loop (a host timer posting the AX message, not an alarm the idle loop
checks) would feed it with no added latency, so a no-latency fix exists. If **core 1 is inside a
host call** (a `gx2.*` readback, a driver submit) for most of the gap, nothing on core 1 can queue a
frame, and no fix without moving the AX thread to another core exists; the queue could only be
deepened, which is the added latency the owner rules out. If **core 1 is idle with a thread waiting
on a GPU sync**, the idle loop was running and so was the pump: a gap then points at the pump's
cadence or the output thread, not a busy core. If the queue stays at `lowest 40 ms` or more through
every crackling arrival, the audio never drained and the search moves off the audio path.

## 2026-11-22: a repeated `[copy]` note is a hash, and every compose-mode edge says its cost; built, untested on console

The 13.60 Hateno report (`fps-report.md` 2233 row) has one 416.6 ms frame at the first
`Flat camera mode started` (2230.46) with 52 `[copy]` lines in 0.3 s and `compose mode mono`, then one
frame of 21-55 ms at each later flat-mode start/end. The boot log's own wait counters read
`waiting on the GPU 0% (the previous frame 0, an image 0, its fence 0, a present 0, presenting 0, other 0)`
in every one of those windows and the GPU thread is 4-12 % idle, so none of it is a
`vkDeviceWaitIdle`/`vkQueueWaitIdle`/fence wait.

**Whose cost it is.** Ours creates nothing and waits for nothing at an edge: `xrEndFrame` only switches
`ComposeMode` and logs (`port/bettervr/xr/runtime.cpp`), and the compose pipeline, descriptor set,
samplers, HUD mip image and eye buffers are made once (`EnsureHudMips` reuses by size and format). Cemu
pays the rest, because the edge is the game's own screen change: the camera/dialogue screen's images and
pipelines are used for the first time (`[draws] ... pipelines made 0.08` against 0.00 in the windows
before and after: ~21 pipelines, compiled on the asking thread), the game copies its screen (the 52
copy kinds), and the stereo view layer re-seeds the right view's copies of every render target written in
the mono section. The "zero copies" at the later edges is only the `[copy]` line's dedup: a kind is said
once, so the same copies are there but not printed again.

**What this branch changes, ours.** Patch cemu 0064 (amends 0014/0015): the `[copy]` check used to build
two formatted strings and insert two `std::string`s into a `std::set` under a mutex for **every** copy and
readback before it could know the line was said. The kind is now a 64-bit hash of the values the line
names and the line is formatted only when it will be said; a repeated copy costs one mix and one
`unordered_set` lookup, and a collision can only leave a line unsaid. No copy, readback or line text
changes. The compose-mode line now carries the edge's cost:
`compose mode mono (frame N): ...; 54.4 ms after the previous frame reached Hmd2` (one line per change, the
cost at most once every 250 ms; a normal frame reads 16.7 ms).

Expected: the 416.6 ms first entry stays (Cemu's first-use pipelines; precompiling them is a bigger
change), and the later edges drop by the port's per-copy formatting, measured by the new line; if it still
reads 40-55 ms, the rest is Cemu's own copies/seeds and is the next job, not this one.

Checks: `tools/cemu-patches.sh apply` and `tools/bettervr-patches.sh apply` (both with `PS5CEMU_RELEASE=1`,
every patch ID matching), `python3 -B tools/lang.py check`, `tools/tests/test-bettervr-triggers.sh`,
`tools/tests/test-texunit-memo.sh` pass; no host test builds the runtime or Cemu's Vulkan path, so the
"BetterVR build" run is theirs. Built, untested on console.

**Test request 81 (v0.8 flat-camera edges, 13.60 console).** Install the `BotW-VR-v...zip` from the green
"BetterVR build" run on this head, VR on, 1.25x as in the last run.

1. **The lab.** Go to the Hateno Ancient Tech Lab and talk to the director so the dialogue camera switches
   several times; also walk out and in again, and open the Sheikah camera rune (`CameraFinder`) and put it
   away a few times — it is the same flat-camera path. Say for each hitch whether it was the first camera
   opening of the session or a later one, and whether the picture came back in stereo after each exit.
2. **Another flat-camera dialogue**, if one comes up (the director's own talk, or any NPC where the
   picture goes flat), same check.
3. **No change elsewhere.** Normal play, a pause menu and a shrine cutscene look as before, and the first
   frame of a flat picture is never the previous 3D frame left over.

**Send**, from `/data/ps5cemu/logs/boot.log`:

- `LC_ALL=C grep -a "compose mode\|Flat camera mode" boot.log`
- `LC_ALL=C grep -a "\[perf\].*the longest" boot.log`

and which of the two steps each hitch came from.

**What it decides.** A `compose mode` line of 16-25 ms at a later camera opening means the port's share is
gone and the edge is what Cemu's own screen work leaves; 40-55 ms means our per-copy cost was not the cost
and the next job is Cemu's copies/seeds (the line's number decides which). A first entry of ~400 ms is the
first-use pipeline compilation, expected, and is not what this request asks to fix.

## 2026-11-22: v0.8 keeps v0.7.0's vibration, and each Vibration row's On box has its own ID; built, untested on console

The v0.8 haptics work is dropped: the director, the audio bands, the two-motor split, the new Vibration rows and their
patches are not in this branch. The owner's decision is that the extra effects would discharge the controllers faster,
so v0.8 keeps the vibration exactly as v0.7.0 has it. The haptics branch, `bettervr-1.0/v08-haptics`, stays as the
archive of that work. One fix from that run is kept, because the bug is v0.7.0's own: the Vibration page's On boxes.

**The bug.** `DrawVibrationPage` drew every row's On box as `ImGui::Checkbox("##on")` with no `PushID`
(`imgui_menus.cpp`, the line patch bettervr 0008 added at line 775), so all rows shared one ImGui ID: nav focus drew
on every On box at once and the pad could not reach one row alone. Each box now pushes its row's label, as the device
columns already do, and pops it after. Rows, strengths, devices and defaults are untouched.

New patch bettervr 0046 (`imgui_menus.cpp`); no Cemu, runtime or asm change. `tools/bettervr-patches.sh apply`,
`tools/cemu-patches.sh apply`, `python3 -B tools/lang.py check` and `tools/tests/test-bettervr-triggers.sh` pass.
Built, untested on console.

**Test request 80 (v0.8 Vibration page focus, 13.60 console).** Install the `BotW-VR-v...zip` from the green
"BetterVR build" run on this head. In game, open BetterVR's Vibration page and move the pad's focus down the On
column: exactly one box should show the focus frame, and the confirm press should toggle only that row. Repeat across
the Hands, Headset and DualSense columns.

**Send.** Which row and column were focused, whether the confirm press toggled one row or more, and whether any row
changed twice. No log is needed.

**What it decides.** One focus frame and one toggled row per press decides the fix; a shared focus frame or a second
row moving leaves the ID scope wrong.

## 2026-11-22: a third-person cutscene camera starts at the game's height, not 1.65 m above it; built, untested on console

The 13.60 report (seated, first person with third-person cutscenes, Height Adjustment +34 cm) says the view "often
lands in the ceiling" in third-person cutscenes. The investigation found upstream 1.0.0 adds the whole stage-space
head pose to the game's or the held event camera (`BuildMiddlePose`,
`camera.cpp:438`; `hook_UpdateCameraForGameplay`, `camera.cpp:594`). Seated calibration puts that pose at 1.65 m,
so every third-person event camera sits that far above the game's, and the game's collision check only sees the base
camera.

**The fix.** Patch bettervr 0047. `GetAppliedHeadsetOffset` (`camera.cpp:290-307`) subtracts the head's stage y
latched on the event's first third-person frame. The event-start hook arms it with the event's name and the
event-end hook clears it (`Camera::BeginThirdPersonEventLift`/`EndThirdPersonEventLift`, `camera.cpp:74-82`;
`game_state.cpp:207` and `:242`). It applies only while an event is active with the cutscene camera in third person,
and neither flat camera mode nor the black-bars/2D path is active. The camera therefore starts at the game's or the
held camera's height and keeps the head's motion from there on every axis: head turn and lean still move the view.
Held events (0037/0043/0045) and non-held events both take it; third-person gameplay, first person, the 2D path and
every other mode keep upstream's code path and values. No new setting, no Cemu, runtime or asm change.

One INFO line at the latch: `Event camera lift removed for '<event>': <y> m`.

Checks: `tools/bettervr-patches.sh apply`, `tools/cemu-patches.sh apply`, `python3 -B tools/lang.py check`,
`tools/tests/test-bettervr-triggers.sh` passes. No host test builds the
BetterVR camera, so the camera change is checked by the BetterVR build. Built, untested on console.

**Test request 79 (v0.8 third-person cutscene height, 13.60 console).** Install the `BotW-VR-v...zip` from the green
"BetterVR build" run on this head. Seated if possible, with **Camera Mode First Person**, Comfort > Advanced >
**Cutscene Camera Third**, **2D Cutscene Screen Off**, and **Height Adjustment +34 cm** (or the current value).

1. **Pick-up in a low room.** In a shrine, take a chest item for the first time (`Demo002_0`). The cutscene camera
   must start at the room's own height, not in or through the ceiling. Turn your head: the view turns. Lean or sit
   up: the view moves by that small amount, not by 1.65 m.
2. **Guardian appearance.** In a Test of Strength shrine, watch the guardian mini appear (`Demo026_0`). Same check.
3. **Shrine entry.** Enter a shrine and watch the entry cutscene. Same check.
4. **Height 0 vs +34.** Repeat one of the cutscenes at Height Adjustment 0 and +34 cm: the cutscene camera height
   must be the same; only the first-person view before and after may step.
5. **First person and third-person gameplay, unchanged.** Pick up an item in first person and walk in third person
   with no event: they must look as v0.7.0.
6. **Standing, if possible.** Repeat steps 1-3 standing: the cutscene height must be the same as seated.

**Send**, from `/data/ps5cemu/logs/boot.log`:

- `LC_ALL=C grep -a "Event camera lift removed\|Event '\|Camera hold\|Player Height\|Seated height calibrated" boot.log`
- which events and Height Adjustment values were used, seated or standing, and whether any camera entered a ceiling.

**What it decides.** One `Event camera lift removed for ...` line per event in third person decides the latch;
steps 1-3 decide the height and the head motion; step 4 decides the height setting is not in the cutscene; step 5
decides first person and third-person gameplay are unchanged; step 6 decides standing matches seated.

## 2026-11-22: the tutorial shrine's demos hold the third-person camera and keep their tutorial text; built, untested on console

The rc5 report (13.60, first person, Comfort > Advanced > Cutscene Camera Third) says the Kakariko combat-training
shrine, Ta'loh Naeg's Teaching, shows the fault the shrine respawn had: the tutorial cutscenes swing a third-person
camera around the room from high, the picture changes angle every moment, and the tutorial text and the "Side Hop"
card float over it. The pulled boot log (`logs-rc5/boot.log` 2900-3300) has
`Event 'Demo026_0' started at 'Guardian_Mini_Practice_Appearance'` at 02:06:48.330,
`Event 'TutorialDungeon' started at 'SideStepWarning_Npc_TutorialDungeon_StepStart'` at 02:07:53.683, and four
`Event 'Demo719_0' started at 'StartTutorial_BackStep'`, `'StartTutorial_GuardJust'`, `'StartTutorial_SpinAttack'`
and `'ClearTutorial'` between 02:08:36.056 and 02:10:22.963, all `(first person ON, ignore camera rotation ON,
game-animated hands OFF)`. None of them has a `Camera hold` line, while `Demo002_0` in the same run has
`Camera hold starts for 'Demo002_0' ...` (02:10:41.703) and the respawn has its own three lines at 02:56:34.463-472.

**Which path they took.** All three ids are upstream's table exactly as logged: `Demo026_0` "Combat shrine: enemy
appearing demo" (`patch_Settings_Cutscenes.asm:95`), `Demo719_0` "Tutorial dungeon" (`:251`) and `TutorialDungeon`
(`:949`), all `FP_ON,PAN_OFF,HND_OFF,CTRL_OFF`. With Cutscene Camera Third, `GetActiveEventMode()` is Third for the
whole event, so `GameState::IsFirstPerson()` is false and `hook_ShouldSkipEventCamera` returns 0
(`camera.cpp:725-730`): the demo's own `CameraEvent` cues run, and `hook_UpdateCameraForGameplay` follows the event
camera every frame, so the base pose steps cue by cue. `ResolveGameplayBasePose` returns the game camera unless
`Events::IsEventCameraHoldActive()`, which 0037/0043 set only for `Demo002_0`, `Demo048_0` and `Demo048_1`; these ids
were not in that list, so nothing was latched. Cutscene Camera is Third and 2D Cutscene Screen is off, so the event
is the normal 3D path with the HUD/2D layer as its quad (`compose mode stereo ... the HUD quad over them` throughout).

**The fix.** Patch bettervr 0045 extends 0043's one id list: `HoldsEventCamera` now covers `Demo719_0`, `Demo026_0`
and `TutorialDungeon` as well, so `Camera::LatchEventCameraHold` latches the player's pose at each event start and
`ResolveGameplayBasePose` holds it while the event runs in third person, exactly as for the pick-up and the respawn.
Head tracking still applies, first person is unchanged (it already skips the event camera), and every other event
keeps the game camera. The tutorial text and the move card are untouched: they are 2D content and the quad is not
changed.

**Why the fade-quad rule is not extended to them.** The white wash in the report does not show a second panel:
the report's own recording (`cap-rc5/frames/f001-f017.png`) has no 16:9 rectangle edge anywhere, and the only 2D
content is the tutorial text and the card's own rounded rectangle. The wash follows the room's depth (the near floor
is the brightest area, the far walls darker, the dust motes stay crisp), which reads as the shrine's fog in the 3D
picture seen from a camera high in the room. The only path of ours that can wash the whole 3D picture is the game's
fade (`Screens::IsAnyFadeVisible()` and the 2D layer's corner texel in `present_depth.frag`), and 0043's rule leaves
the *whole* 2D layer quad out while that flag is up (`renderer.cpp:387-392`). Here that quad also holds the tutorial
text and the card, so extending the rule would hide the tutorial with the fade. The quad therefore stays, and one
line, `Fade panel kept for '<event>' (the 2D layer also holds the tutorial's text and card, so it stays)`, says when
a fade screen is up during one of these demos, so Test request 78 settles whether any part of the wash was a fade. If
it was, the next step is a narrower rule that leaves out a fade-only quad, not this one.

**Sibling events.** Included, as the same kind (a scripted event camera in third person around the training shrine's
teaching): `Demo719_0` and `Demo026_0` — the table has no other `Demo719_x` or `Demo026_x` — and `TutorialDungeon`.
Not included: `Npc_TutorialDungeon` (`:821`), the shrine NPC's dialogue event, not a demo with a scripted camera, and
absent from the rc5 log; `Demo049_0` "Controls guide demo" (`:120`), the plateau's control guide; `Demo033_0`
"CDungeon Monk Demo", the shrine-clear monk.

**Cause.** The camera is upstream's: in third person the event camera runs and upstream's skip is first-person-only.
The hold that covers it is ours, and 0037/0043's list named only the pick-up and the respawn.

New patch bettervr 0045 (`game_state.cpp/.h`, `camera.cpp`); no Cemu, runtime or asm change. BetterVR build run
`44969067687` on `d932c39` succeeded.

**Test request 78 (v0.8 tutorial-shrine camera, 13.60 console).** Install the `BotW-VR-v...zip` from the green
"BetterVR build" run on this head. Before step 1, set Comfort > Advanced > **2D Cutscene Screen** to **Off** (the
Gentle preset has it On; with it On a third-person event takes the black-bars path, no 3D picture is presented, and
this test cannot decide the fix), Camera Mode to **First Person** and Comfort > Advanced > **Cutscene Camera** to
**Third**.

1. **The guardian's appearance.** Enter Ta'loh Naeg's Teaching in Kakariko and watch the guardian mini appear
   (`Demo026_0`). Is the 3D picture steady, with no scripted angle change, and does the view stay where you stood?
   Turn your head: the view must still turn.
2. **Each tutorial.** Watch every tutorial cutscene in the shrine: the side-hop message, then the backstep, guard
   just, spin attack and clear tutorials (`Demo719_0`, four starts). For each: is the picture steady, does the camera
   stay in place, and are the tutorial text and the move card ("Side Hop" and the others) readable and in front of
   you? Say which tutorials you saw.
3. **The wash.** Say whether the room's washed-out look matches what the same cutscene shows on the TV with VR off,
   and send the `Fade panel kept` line if it appears.
4. **First person, unchanged.** Set Cutscene Camera to First and repeat one tutorial: it must look as v0.7 did, with
   no `Camera hold` line for it.
5. **Another cutscene, unchanged.** Trigger a cutscene that is not from this shrine (a memory, a tower or a chest):
   no `Camera hold` line may name it, and it must look as before.

**Send**, from `/data/ps5cemu/logs/boot.log`:

- `LC_ALL=C grep -a "Event 'Demo719_0'\|Event 'Demo026_0'\|Event 'TutorialDungeon'\|Camera hold\|Fade panel kept" boot.log`
- `LC_ALL=C grep -a "compose mode" boot.log`
- which Camera Mode, 2D Cutscene Screen and Cutscene Camera settings were used, and which tutorials and events were
  seen.

**What it decides.** One `Camera hold starts for ...` per event in third person and none in first person decides that
the three ids take the hold; steps 1-2 decide whether the picture is steady and the text/card readable; step 3 and
the `Fade panel kept` line decide whether any of the wash was a fade (and so whether a narrower fade-only rule is
worth building); steps 4-5 decide that first person and the other events are unchanged.

## 2026-11-21: in VR the software keyboard is the console's own, so a stable's horse name can be typed where the player is looking; built, untested on console

**The report this acts on.** A console report (13.60, VR, Sense) of the stable's naming screen: the keyboard never
showed and the game waited. The cause is the port's own keyboard sheet: Cemu hands its swkbd to `PS5Cemu_DrawKeyboard`
(`port/app/ingame.cpp`, patch cemu 0039), which draws it into Cemu's TV picture. In BetterVR's stereo frames that
picture is neither the headset's nor, with the TV mirror on, the TV's: the stereo path skips Cemu's TV game picture
and the TV shows Hmd2's social screen, so the sheet is drawn where nobody sees it. With the TV mirror off it is on
the TV only, never in the headset. Flat play was and is fine, and the game's own wait has no end: Cemu's swkbd has
no cancel and no timeout.

**The fix, on the PS5 side only.** While stereo runs with the headset up (`ps5vr::Stereo()` and `ps5vr::HeadsetUp()`),
`PS5Cemu_DrawKeyboard` opens the console's own keyboard (libSceImeDialog, new `port/app/ime_dialog.cpp`) once per game
appearance, with the form's text and the limit the game set (UTF-16 units, as swkbd counts them; a game's larger limit
is clamped to the dialog's 2048). It polls the dialog once a frame on the thread `swkbd_render` runs on, and on OK
writes the typed text into the form and returns done, exactly as its sheet did, so Cemu's `swkbd_finishInput` decides
it to the game. A cancel, a dialog the system abandons, one it refuses, and one left up for 90 seconds without an
answer all decide the text the game gave: swkbd has no cancel a game could take, and the game must never wait on a
keyboard nobody can see. A game that dismisses its own swkbd while the dialog is up ends it from the next TV frame
(`PS5Cemu_RenderOverlay`), so the pad is not left idle. If the headset failed to start, `ps5vr::Stereo()` stays true
but Hmd2 is not up: the TV shows Cemu's own picture, where the sheet is visible, so that case keeps the sheet. Flat
play and the M2 flat screen keep it too.

**The game's input while it is up.** The console's keyboard is the system's, so the pad is its: patch bettervr 0044
has `hook_InjectXRInput` write an idle VPAD status while `BetterVR_PS5_GameInputBlocked` is set
(`port/bettervr/xr/runtime.cpp`) and holds the pad's and the Sense's Options from the game's pause. An Options press
that runs into or through the dialog ends with it: its release is not a pause after the keyboard closes. The state
above that write still runs, so a button held when the dialog closes is not read as a fresh press. The DualSense never
reached the emulated GamePad while swkbd is active (`OverlayTakesInput`), as before.

**Logs, capped at 24 lines each run, never the text.** `[ime] swkbd asked for a keyboard: N units, limit M
(form|keyboard); the console's opened (0x0)`, or `... it did not open (0x..., N tries): the text it gave is decided`;
`[ime] the console's keyboard: OK, N units`; `... cancelled; the text it gave is decided (swkbd has none to take)`;
`... no answer in 90 s; ended, the text it gave is decided`; `... ended without an answer (...)`, with the close
status and the length, never the text; and `[ime] the game dismissed its keyboard; ended, the text it gave is
decided` when the game closes its own prompt.

**Where the layout comes from.** The SDK has no header for this dialog, and none is used: the 96-byte parameter
block, its 16-byte result, the statuses (0 none, 1 running, 2 finished) and the outcomes (0 OK, 1 cancelled, 2
aborted) are the ones public PS5 homebrew opens it with (ps5-payload-dev's offact, prospero-win's pw_osk_ps5.c),
which the PS4 reimplementations share. Its text is 16 bits a unit whatever the toolchain's `wchar_t` is. The module
stub is one of the SDK's, which `tools/link.sh` already links.

**What is not known until the console run.** Whether the system draws this dialog in an hmd2 session's headset at
all: it is a system 2D layer composited over the title's TV output, and the headset gets Hmd2's own eye buffers. That
is Test request 77's first question and this design stands or falls on it. Where it does not show, Circle cancels it
even unseen, and a dialog left unanswered ends itself after 90 seconds: either way the game takes the text it gave
instead of waiting forever.

Patches: bettervr 0044 (the idle pad, and Options ending with it), cemu 0063 (the hook is told form or keyboard-only,
for the request line); new files `port/app/ime_dialog.h/.cpp`; no GPU, frame-path or record change.

**Test request 77 (v0.8 console keyboard in VR, 13.60 console).** Install the `BotW-VR-v...zip` from this head's
green "BetterVR build" run and start Breath of the Wild in VR with the headset, both Sense controllers and the
DualSense on. Load a save that can reach a stable (or ride to one), register a horse and reach the naming screen.

1. **Does the console's keyboard show?** Say whether the PS5 keyboard appears in the headset, only on the TV, or
   nowhere. This is the question the design rests on.
2. **Does the name reach the game?** Type a name and accept it: does the naming screen close, and does the name show
   in the stable and the pause menu? If it does not, say whether the game went on with the name it gave (the
   fallback) or stayed waiting.
3. **Cancel.** Show the keyboard again (rename the horse, or any other text prompt) and press the dialog's Circle
   cancel, whether or not you can see the dialog: the game should continue with the text it gave, not wait.
4. **An unanswered dialog.** Show it once more and leave it alone, best done while it is not visible: after about 90
   seconds the game should go on with the text it gave. Say whether it did, and send the `no answer in 90 s` line.
5. **The game's own prompt dismissed.** With the console keyboard up, back out of the game's prompt instead of
   answering it: the game should be playable again, and the log should hold `the game dismissed its keyboard`.
6. **Options across the dialog.** Hold Options while the dialog is up, then release it after the dialog closes: the
   game must not open its pause screen.
7. **Flat, unchanged.** Play on TV and name a horse: the port's own sheet must still be the one that shows and types.

**Send**, from `/data/ps5cemu/logs/boot.log`:

- `LC_ALL=C grep -a "\[ime\]" boot.log`
- which of steps 1-7 happened, where the keyboard was seen in each, and whether the pad moved Link while it was up.

**What it decides.** Step 1 decides whether the console's dialog is visible in the headset; if it is TV-only, the
next step is a keyboard drawn in BetterVR's own overlay. Step 2 decides that the typed text reaches swkbd. Steps 3-5
decide the cancel, the time limit and a game that dismisses its own keyboard. Step 6 decides that the pad returns
without a pause. Step 7 guards flat play and the M2 flat screen.

## 2026-11-21: the shrine respawn holds the third-person camera and its fade reaches the 3D picture without the 16:9 panel; built, untested on console

The rc4 report (13.60): falling into the void in a shrine, the respawn cutscene showed the same third-person camera
jump as the first-time pick-up, with a dark semi-transparent 16:9 rectangle floating over the 3D picture, then about
2 s of black with a small dot, then the third-person shot of Link on the shrine floor with the rectangle again. The
pulled boot log (`logs-rc4b/boot.log` 2880-2920) has `Event 'Demo048_0' started at 'Demo048_0' (first person ON,
ignore camera rotation ON, game-animated hands OFF)` at 19:52:29.230 and 19:52:46.131, ended at 19:52:34.668 and
19:52:51.518.

**Which path it took.** `Demo048_0` is "Respawn demo" in upstream's table (`patch_Settings_Cutscenes.asm:118`),
FP_ON/PAN_OFF. It is not in patch 0032's 2D-only list, and black bars are off, so `UseBlackBarsDuringEvents()` is
false: the event takes the normal 3D path, with the HUD/2D layer as the 16:9 quad. In this run the event is third
person (`GetCutsceneCameraMode()` is Third Person; the same log holds `Demo002_0` in third person), so
`hook_ShouldSkipEventCamera` returns 0 and the demo's `CameraEventAnim`/`Move`/`GameOver` actions run.
`hook_UpdateCameraForGameplay` follows the event camera every frame, so its base pose steps the way `Demo002_0`'s did;
`ResolveGameplayBasePose` had no hold for this id.

**The dark rectangle.** The game draws the respawn's black fade into the 2D layer as a semi-transparent full-screen
rectangle. `Layer2D` reaches the headset as the 16:9 HUD quad at `hudDistance`, so the fade shows as a floating panel;
the 3D present shader already samples the same layer's corner texel and applies that fade to the 3D picture
(`present_depth.frag`), so the panel is a second copy. This is upstream's HUD-quad presentation of the game's fade,
not a 0032/0038 regression: 0038 only changes the black-bars path, which this event does not take.

**The fix.** Patch bettervr 0043 keeps one event-id list in `game_state.cpp`: the camera hold now covers `Demo002_0`
and the respawn demos `Demo048_0`/`Demo048_1`, and `ResolveGameplayBasePose` holds the pose latched at the event start
in third person (first person already skips the event camera). While a respawn demo has a fade up,
`PublishPresentation` sets `suppressFadeQuad`, and `renderer.cpp` leaves the 2D quad out of the composition when a 3D
picture is presented; the 3D fade is unchanged and the panel is gone. The first-person camera hold is unchanged (first
person already skips the event camera); the fade panel is dropped in both camera modes. Every other event, the
black-bars path and the 2D-only `Demo003_0` presentation are unchanged.
Logs: `Camera hold starts for 'Demo048_0' (third person, held at ...; head tracking still applies)` and
`Camera hold ends for 'Demo048_0' ...` in third person, and `Fade panel hidden for 'Demo048_0' (the 3D picture
takes the fade from the 2D layer's corner)` when the fade is up.

**Cause.** The camera jump is upstream's: the event camera runs in third person, and upstream's skip is
first-person-only. The hold that would have covered it is ours, and 0037 listed only `Demo002_0`. The dark panel is
upstream's HUD quad showing the game's own fade; none of patches 0032, 0037 or 0038 changed that path.

**Sibling events.** `Demo048_0` is the respawn demo for the shrine and open-world void falls; drowning and lava
deaths are expected to run the same flow, but that is not confirmed, and the table has no separate id for them.
`Demo048_1` is the same demo for the Vah Ruta battle. Both are included. `Demo019_0` "Retry Demo" and `Demo006_0`
"Game Over Demo" are the game-over/retry flow, `Demo005_0/1` are the warp demos, and `Demo037_0`/`Demo038_0` are
the Lost Woods zone-out/safety reposition; they are different events and are not included.

New patch bettervr 0043 (`game_state.cpp/.h`, `camera.cpp/.h`, `game.h`, `renderer.cpp`); no Cemu, runtime or asm
change. BetterVR build run 44939688206 on `645e425` succeeded.

**Test request 76 (v0.7 respawn camera and fade, 13.60 console).** Install the `BotW-VR-v...zip` from the green
"BetterVR build" run on `645e425`. The report's run was Camera Mode First Person with Comfort > Advanced > Cutscene
Camera set to Third; the steps below use the camera mode to set the same thing. Before step 1, set Comfort > Advanced >
**2D Cutscene Screen** to **Off** (the Gentle preset has it On; with it On a third-person event takes the black-bars
path, no 3D picture is presented, and this test cannot decide the fix).

1. **A shrine void fall, third person.** Set Camera Mode to Third Person, with 2D Cutscene Screen off. In a shrine,
   walk off into the void. Watch the whole respawn: is the 3D picture steady, with no scripted camera step, and does
   the black fade cover the view without a floating dark rectangle? Does the view stay where it was until the event
   ends?
2. **An open-world void fall, third person.** Do the same from a cliff or a hole in the open world (a fall that
   respawns rather than a normal landing). Same questions.
3. **A shrine void fall, first person.** Set Camera Mode to First Person and Comfort > Advanced > Cutscene Camera to
   First; 2D Cutscene Screen stays Off. Repeat step 1. Does the 3D picture stay steady, and is the dark rectangle gone?
4. **An open-world void fall, first person.** Repeat step 2 in first person.
5. **Vah Ruta, if a save has reached it.** In the Vah Ruta battle, trigger the respawn demo (`Demo048_1`), for example
   by falling into the void there. Same questions as step 1. This is the only step that reaches `Demo048_1`.
6. **Normal play and another cutscene, unchanged.** After a respawn, play about a minute and trigger a cutscene that
   is not a respawn (a memory, a tower, or a chest): it must look as it did in v0.6, with no camera hold line for it
   and no missing panel.

**Send**, from `/data/ps5cemu/logs/boot.log`:

- `LC_ALL=C grep -a "Event 'Demo048_\|Camera hold\|Fade panel hidden\|2D-only presentation" boot.log`
- `LC_ALL=C grep -a "compose mode" boot.log`
- which Camera Mode and 2D Cutscene Screen setting were used for each step, and whether each fall was a shrine, a
  cliff, water or lava.

**What it decides.** Steps 1-4 decide whether the respawn now holds the third-person camera and drops the 16:9 fade
panel in both camera modes. Step 5 decides that the Vah Ruta id takes the same path. Step 6 decides that the hold
list and the fade suppression did not reach any other event.

## 2026-11-21: Eye Aim Assist is five steps with one 30-degree cone and a row that reads, and the hand's Magnesis pushes, pulls and follows an object above the player; built, untested on console

**The v0.7rc3 run these act on (13.60, 1.25x).** In the recording a right press on Eye Aim Assist stepped the value
Off, 25 %, Off, 75 %, Off, and the label showed `97%` followed by junk characters: the row was a slider, and
`DrawFloatSlider` handed its finished label to `ImGui::SliderFloat`, which takes a printf format. The display
formatted the value through "97%" and read bytes past the `%`; the rounding pass sanitized the trailing `%` to an
empty format, formatted nothing,
read 0 back, and every step landed on Off. The saved strength was whatever the broken slider last left (the log's
one shot below Full read `39% strength`), so the displayed step and the strength in use never agreed.

**The row (ours).** Eye Aim Assist is the Controls page's existing segmented row, not a new widget: **Off, 25 %,
50 %, 75 %, Full**, plain ASCII, with the strength set to exactly 0, 0.25, 0.5, 0.75 or 1. `EyeAimAssistSetting`
snaps every value it takes, saved ones included, to the nearest of the five, and reads an old `EyeAimAssist=true` as
25 %; the retired rc2 key `EyeGazeThrowAssist=true` is read as 25 % in `mod_settings.cpp` too. A right press on the
row now only moves the focus between the five buttons; the value changes on select, so one press does one thing.
`DrawFloatSlider` also doubles a `%` in its label, which fixes the garbled text and the snapped value on every other
row that shows a percent (the DualSense strength row among them). The menu's navigation input is upstream's and is
unchanged: the left Sense thumbstick feeds `navigate`/`move`, a diagonal picks one axis by `|x| >= |y|`, a held
direction refires every 0.5 s, the right stick is not read by the menu, the DualSense's sticks are added only when
no Sense is connected, and `HostWindow::QueryPointer` returns nothing on PS5, so no hand pointer hovers rows.

**The steps (ours).** The throw keeps the raw angle times the strength, with Full the whole angle. An arrow now has
one cone below Full, **30 degrees**, wide enough that the 10-25 degree shots the log shows are helped at every step;
the old gate was 5 degrees / strength (20 at 25 %, 10 at 50 %, 6.7 at 75 %), so 50 % and 75 % did nothing on exactly
those shots. Full is unchanged: the arrow follows the gaze from any angle. Each step bends its share of the angle
(25 % a quarter, 50 % half, 75 % three quarters). A shot outside the cone now logs at INFO
(`[BowShot] eye aim assist: arrow, 44.6 degrees to gaze, 50% strength, outside the 30 degree cone, unchanged`) so a
shot with no visible change says why.

**Hand Magnesis distance (ours).** With both eye rows off the hand path still steers as upstream, but the right
stick's up/down now pushes and pulls the distance in both hand paths, through the eye path's own
`PushPullDistance` and the same input remap (`IsSteeringByHand` joins `IsSteeringByGaze`). The arm-reach stick keeps
its upstream behaviour, and a pull stops at 1.5 m. The offset is re-clamped against the hand's own distance every
frame, so a full pull followed by the hand coming in or tilting up keeps the object in front at that floor instead
of letting the distance go negative and turn it around. The `hand steer` line gains `stick` so the next run can tell
the stick from the hand. The game's own range is respected: when it did not take the last distance write, the next
push or pull counts from where it stopped, so reaching the end does not leave a dead push to undo.

**An object above the player (ours in both hand paths, upstream's composition in one).** The upstream hand path
built a spherical yaw and pitch and clamped the pitch at ±80.2 degrees (`MagneMaxSwingPitch`): above that the hand's
tilt did nothing and then swung the object outward, which reads as the controls doing the opposite. The gaze-pick
path took its pitch axis from the object's horizontal offset, which shrinks to noise when the object is overhead.
Both now turn the grip-time vector by the hand's yaw and pitch since the grip (`TurnLeverByHand`), with the axis
taken from the grip's own yaw, so no angle is clamped or singular. At normal heights this is the same rotation as
before (checked against the spherical formula to 1e-14 m); the log's `hand steer` lines all read `path gaze pick`
and reached 80-84 degrees of pitch, so they are the gaze-pick path and not the upstream clamp, whose region the new
both-eye-rows-off run asks for.

**The balloon challenge was not running fast.** `[clock] guest clock 10.077 s over 10.077 s of host time (ratio
1.000); 59.8 game frames per guest second` runs through the session, with 59.7-60.0 fps submitted; 1.25x is the VR
resolution, not a speed. During the Korok balloon challenge (421-429 s) the shots were 23.5, 20.4, 10.7, 5.5, 11.9,
8.7 and 7.8 degrees to the gaze at 100 % strength, and each took its whole angle, so Full did what it does; the
challenge is harder in VR rather than sped up. Nothing changed for it.

New patch bettervr 0042 (`controls.cpp`, `bow.cpp`, `magnesis.cpp/.h`, `imgui_menus.cpp`, `mod_settings.cpp/.h`); no
Cemu change. Test requests 73 and 74 carry the new steps.

## 2026-11-21: v0.7 draws: index lists kept across frames, unchanged view arrays kept, texture units kept while their SQ_TEX registers are unchanged, and the render passes begun and submits counted, to cut the 1.25x record; built, untested on console

**The console run these act on (the v0.7rc2 build at 1.25x, before patches 0061 and 0062).** The console log averaged
58.0 fps, median 59.2, with 40 of 113 gameplay windows at exactly 60.0; 60 held to 5,309 draws, and the wall is still
the record at a budget of about 15.6 ms: 0.7 ms to cut at 5.3-5.6k draws, 1.25 at 5.6-5.9k, 1.4 at 5.9-6.3k, about 2.0
at 6-7k and 2.8-3 at 7-9k, with the GPU at ~86-91% once at 60 (about 2 ms of headroom). The kept index lists fell to
10-12 decoded a frame with 99.4% reused and 0 changed, but the record fell only 0.5-0.6 ms: the per-flush re-hash
gives back about two-thirds of what the decodes were worth, and the view arrays could not be credited (no counter).
Threaded recording is Test request 72's run 2 and needs no patch.

The v0.6.0 logs at 1.25x VR Resolution (2480x2560 an eye, asyncCompose on) averaged 55.5 fps against 58.2 at 1x: the
GPU was about 80% busy and the GPU thread's record set the rate, 16-19 ms at 5-9k draws. Holding 60 there needs the
record about 1.0 ms shorter at 5-6k draws, 1.5 ms at 6-7k, 2.3 at 7-8k and 3.0 at 8-9k. The record split at 1.25x
(5.3k and 8.6k draws a frame, about 1,500 sequences either way) puts about 8.7 us into each draw sequence (setup 1.8,
the first draw's uniforms 0.9, indices 0.95, buffers 0.7, pipeline and sets 0.6, binds and the draw 2.1, the commands
between 1.7) and about 0.8 us into each continued draw. Index lists were decoded about 1,800-2,000 times a frame
(6-7 MB), 1.7-1.9 ms, because Cemu's eight-list index cache is emptied at every GPU wait and every finished command
buffer, a dozen times a frame. Descriptor sets and pipelines are not made during play (0.1 and 0 a frame), and Cemu
already skips unchanged pipeline, vertex buffer, index buffer, viewport and scissor binds, so the binds that remain are
the driver's recording of new state.

**Index lists kept across frames (patch cemu 0059, `bettervr.json`'s `indexCache`, on).** A decoded index list is also
kept by its pointer, count, primitive mode, index type and restart index. Until Cemu's next flush it is trusted as the
cache trusts its own entries; after one, its bytes are hashed with the vertex cache's page hash and the list is used
again if the hash is the same, or decoded again into new memory if not (the GPU may still read the old memory, which
goes once the command buffer is done). Lists unused for 60 frames go, the kept lists hold 32 MB at most, and the
immediate draws' index array, which Cemu rewrites between draws, is never kept. The index heap's reservations keep
their heap address, so letting one go no longer searches every live allocation. No key or `"indexCache": 1` runs it;
`"indexCache": 0` gives Cemu's own cache. The boot log says `[bettervr] Index cache: index lists kept across frames...`
or `... decoded again after each of Cemu's flushes ("indexCache": 0)`.

**View arrays kept when nothing changed (patch cemu 0060).** A multiview shader's uniforms hold both views, and every
continued draw built the right view's half again for each stage, also for a stage the draw did not touch. The build
reads the stage's left uniforms and their buffer registers, which the draw's dirty flags cover, and the right view's
data, whose epoch every change moves on; a continued draw with none of them changed keeps the arrays, which is what the
build would have written.

**Texture units kept while their registers are unchanged (patch cemu 0061, `bettervr.json`'s `textureMemo`, on).** A
sequence's first draw resolves every texture unit its shaders use through the view lookup cache: the unit's seven
`SQ_TEX` resource words to a view, by scanning the bucket the registers' address, size and pitch hash to, about 1,500
sequences a frame at 1.25x and 2 us of setup each. A unit's answer is now kept per stage and unit: the same words, the
same depth comparison and no view added to or removed from the lookup cache since bind the view again and skip the
format reconstruction and the scan. The texture's data hash, swizzle, mip address, dynamic textures, the barrier check
and the effective scale are still read from the texture on every sequence, so a texture update or invalidation needs no
notice; the lookup cache's generation, moved on by every view added or removed, catches a view destroyed and another
made at the same address. `"textureMemo": 0` resolves every unit through the cache, and the boot log says which way it
runs.

**Render passes begun and submits counted (patch cemu 0062, `bettervr.json`'s `passCounts`, on).** A copy or a render
target switch ends the render pass and the next draw begins it again, and every submit ends the command buffer and waits
for a free one; how often that happens was not counted. The `[draws]` line now says `render passes begun N, submits N`
per frame. No batching ships with the counter: Cemu already batches the buffer-cache copies it can (disjoint uploads in
one `vkCmdCopyBuffer`), and the pairs left over overlap, which is exactly why they flush (regions in one batch run in no
particular order), so none could be shown to need no barrier between them. `"passCounts": 0` stops counting and the two
read 0.0.

**The index cache's per-flush re-hash is left as it is.** The console log showed the re-hash giving back about
two-thirds of what 0059 saves (about 5.8 MB hashed a frame). Skipping it needs proof that the source bytes cannot have
changed since they were last hashed, and there is no write path to hang that on: the emulated CPU writes guest RAM with
direct stores from recompiled code and the interpreter, and HLE code writes it through raw pointers, with no hook in any
of them; instrumenting every store would cost the game's own CPU time. `changed 0` for a whole session is encouraging,
not proof: a missed write hands the GPU stale indices, which is wrong geometry, so the re-hash stays.

**The `[draws]` line** adds `| kept index lists used again N, changed N (N KB held) | texture units reused N |
render passes begun N, submits N`. `index lists` still counts the lists decoded. Expected at 1.25x if most lists are
static: `index lists` from about 1,900 to a few hundred a frame, `indices` in the record split from 1.7-1.9 ms to under
1 ms, `uniforms` 0.2-0.5 ms lower at the same draw counts, and the record 0.6-1.4 ms shorter at 5-7k draws. That
reaches 60 at 5-6k draws, not reliably above.

**RADV threaded recording, not made default.** The rest of a sequence's cost is mostly the driver recording new
pipeline, descriptor, vertex and index state at the draw (`binds and the draw`, about 2.1 us a sequence and 0.24 us a
continued draw). The driver's own layer (`RADV_THREADED_RECORDING=1` in `ps5cemu.json`'s `radvEnvironment`) queues
each `vkCmd*` with its arguments copied and records them on a worker thread; `vkEndCommandBuffer` waits for the worker
(about a dozen submits a frame), destroys and frees wait for it, and the worker spins 50 us on the clock before it
sleeps. If the worker gets a core of its own, the GPU thread keeps only the copying, an estimated 1.5-3 ms off the
record at 5-9k draws; on a busy core it can cost instead. It has never run in a game, so it stays off by default and
Test request 72 runs it. It does not conflict with 0059-0062: those cut Cemu's own work, which stays on the GPU
thread either way, and kept lists keep their index buffer offsets, so fewer index binds reach the queue.

New patches: cemu 0059, 0060, 0061, 0062; no BetterVR patch.

**Test request 72 (v0.7 draws at 1.25x, 13.60 console).** Install the `BotW-VR-v...zip` from this head's green
"BetterVR build" run and start Breath of the Wild in VR with the headset and both Sense controllers. Keep
`"profile": true` in `/data/botwvr/launcher/bettervr.json` with no `"indexCache"`, `"textureMemo"` or `"passCounts"`
key, and set BetterVR's menu > System > VR Resolution > 1.25x (restart the game). Use two spots for every run, the
same each time: the busy outdoor spot of Test request 69 (grass and trees in view; the 1.25x log had 7-9k draws there),
and a calm spot (inside a shrine or a house, 3-5k draws). At each, stand still 30 s, then walk a small circle for 30 s.

1. **Defaults.** The boot log must have `Index cache: index lists kept across frames`, `Texture unit memo: a unit with
   unchanged SQ_TEX registers keeps the view it resolved to` and `Pass counts: render passes begun and submits counted`.
   Say whether anything looks different from the last build in either eye: missing or stretched triangles, flickering
   grass or foliage, an object in one eye only, a wrong shape after a load or fast travel, or a texture that stays the
   old one after an event, a video or the inventory.
2. **Threaded recording.** Add `"radvEnvironment": {"RADV_THREADED_RECORDING": "1"}` to
   `/data/botwvr/launcher/ps5cemu.json` and restart the app; the boot log must have
   `RADV_THREADED_RECORDING=1 (radvEnvironment in ps5cemu.json)`. Same two spots. Say whether it stutters or feels
   different. Then remove the key again.
3. **Optional, index cache off.** Only at the busy spot, set `"indexCache": 0` in `bettervr.json` and restart; the boot
   log must say `("indexCache": 0)`. Then remove the key.
4. **Optional, texture memo off.** Only at the busy spot, set `"textureMemo": 0` and restart; the boot log must say
   `("textureMemo": 0)` and the `[draws]` line's `texture units reused` must read `0.0`. Same route; the fps and the
   record should fall back toward the last build's at the same draw counts. Then remove the key.
5. **Optional, pass counts off.** Only at the busy spot, set `"passCounts": 0` and restart; the boot log must say
   `("passCounts": 0)` and the `[draws]` line must end `render passes begun 0.0, submits 0.0`. It decides nothing about
   speed; it shows the switch works. Then remove the key.

Send each run's `boot.log` (Settings > Diagnostics > Copy logs to USB):
- `LC_ALL=C grep -a "Index cache:\|Texture unit memo:\|Pass counts:\|RADV_THREADED\|compose wait:\|\[draws\]\|\[frame\]\|\[perf\]" boot.log`
- `LC_ALL=C grep -a "fps submitted\|radv/ps5 submissions\|VR start" boot.log`

**What it decides.** Run 1 against the v0.6.0 1.25x log at the same draw counts (5-6k: record 16.1-16.6 ms, indices
1.7-1.8, uniforms 2.0-2.1, 56.5 fps; 8-9k: 18.9, 1.9, 2.7, 51.0 fps): `index lists` and `indices` must fall and
`kept ... used again` must carry most lists, with `KB held` under 32,768 and no visual change; then `indexCache` stays
on. `texture units reused` should be most of the units a frame's sequences resolve, about ten a sequence (so 12,000-18,000
a frame at 1.25x), and `sequence setup` in the split should fall by 0.5-1.2 ms with no visual change; if the count or
the split does not move, `textureMemo` goes. `render passes begun` against `sequences` says whether the copy and flush
splits are worth a batching patch: near one pass a sequence means almost every sequence splits, a few dozen a frame
means there is nothing to batch. Run 2 decides threaded recording: if `binds and the draw` and the record fall and the
fps rises with no stutter and `[perf]`'s GPU thread idle stays sane, a later build sets it for BotW VR; if the record
does not fall or frames over 20 ms rise, it stays off. Run 3, if sent, is the same-session control for run 1; runs 4
and 5 are the switches' off controls.

## 2026-11-21: the PS VR2 features on more firmware: the motor and gaze on 9.00, the 12.40 trigger row, and the four Sense setters as link-time imports; built, untested on console

The goal is every PS VR2 feature on as many firmware versions as possible, and never a call to an
address that has not been checked. This entry adds data rows, one more table and a second resolution
route, with no change to what a 13.60 console does beyond new log lines.

- **Headset motor and gaze on 9.00.** PS5VR 1.3.4's `vr_exports_900.h` gives `sceHmd2SetVibration`
  0x2aa90 and the gaze exports 0x35ec0/0x31870/0x31890 under the anchor our 9.00 table already uses
  (`0x13b4e2`, "Not Initialized"). Both are now in the optional tables, so the motor and the gaze
  probe cover 13.60, 12.40, 11.60, 10.00, 9.00, 8.40, 7.61 and 5.50 when that build's anchor sits
  exactly where the table says. 9.00 and 9.05 have not run this build on a console.
- **Honest labels for shared builds.** PS5VR reports that 13.00, 13.20, 13.42 and 13.60 ship one
  libSceHmd2 build, and 11.40 one with 11.60's. Selection stays by anchor; the log line now names the
  other versions, e.g. `firmware table 13.60 (13.00, 13.20 and 13.42 share this build)`. The labels
  are libSceHmd2's only.
- **Sense triggers on 12.40.** HLA-PSVR2 PR 5's row, `scePadVrControllerSetTriggerEffects` 0x84c0,
  sits behind its own anchor `"/dev/ctrlp_sync"` at 0x1b630, never a moved 13.60 one (a 12.40
  console's 13.60 anchor was 0x4050 lower, and a call at the moved offset aborted, issue #2). The PR
  author had not run it on a console with triggers on.
- **The four Sense setters are also link-time NID imports.** `scePadVrControllerSetVibrationMode`
  (`Wf6-PNCyY20`), `SetVibration` (`DGCwN1Lmmys`), `SetTriggerEffect` (`6Cdc9bbjrRY`) and
  `SetTriggerEffects` (`v8P+9PRqg10`) are imported from libScePad. Per function: the table whose
  anchor matches wins (13.60 for the three rows, 12.40 for the both-hands trigger), else the import
  is used only when the loader's address lies in libScePad's executable segment, else the setter is
  off as before. One line per setter:
  `hmd2: Sense <name>: table <addr|-> (+0x...), import <addr|->, using <table|import|off>, import
  matches|differs from the table|no table row for this build`. The two vibration names are absent
  from the SDK's libScePad stub, so `tools/gen-scepad-stub.py` makes the stub the link and packaging
  use (the SDK's export list plus those two, same SONAME); it changes no behaviour by itself. The
  names' NIDs are checked on a PC (`tools/tests/test-bettervr-nids.py`).
- **What this does not do.** The headset motor and gaze are not imports: the research report gives no
  NIDs for them and the SDK has no libSceHmd2 stub. No kernel-side lookup and no elfldr helper is
  used. On a firmware whose libScePad lacks one of the four NIDs, the app relies on the loader
  leaving the slot unbound rather than refusing the title; that is public evidence (PS5VR, Backport),
  not a result from this toolchain, and the executable-segment guard is what keeps an unbound slot
  from being called. 13.60 remains the only firmware this port has run on a console.
- **The old binding lines are gone.** `Sense vibration exports bound (...)` and `Sense trigger effects
  bound (...)` are replaced by the four `hmd2: Sense <name>: table ...` lines; the earlier test
  requests' expected-log lists that named them should read these instead. The `headset vibration
  bound ...` and `[gaze] bound ...` lines are unchanged apart from the table labels.

**Test request 75 (v0.7 firmware coverage, 13.60 console).** Install the `BotW-VR-v...zip` from this
head's green "BetterVR build" run. With the PS VR2 and both Sense controllers on, start the game,
play about a minute with the Sense controllers in VR so vibration and adaptive triggers are used
(the bow's draw and release, or the Vibration page's test), then quit and copy the logs.

1. **Send the four lines.** `LC_ALL=C grep -a "hmd2: Sense " boot.log`. Each of
   `scePadVrControllerSetVibrationMode`, `SetVibration`, `SetTriggerEffect` and `SetTriggerEffects`
   prints one line with the table address and the import address.
2. **Say whether the two match.** For the three that also have a 13.60 table row
   (`SetVibrationMode`, `SetVibration`, `SetTriggerEffects`), does the line read `import matches the
   table`? For `SetTriggerEffect` there is no table row, so the line should read `no table row for
   this build` and the import address should look like the other three (same libScePad range).
3. **Say whether the feel is unchanged.** Vibration in both Sense controllers and the bow's trigger
   must feel as they did in v0.6; a fall to `off` or a wrong address is the failure this test is for.

**What it decides:** if the three import addresses equal the table addresses, the link-time route is
proven on 13.60 and the same build can be sent to the 12.40 reporter (issue #2) to see whether the
imports bind there. If they differ, the table is still what 13.60 uses; the import route then needs
the 12.40 report before it is trusted on any other firmware. If any address is outside libScePad or
the setters read `off` on 13.60, that setter's line names it and the table path has to be checked
again.

## 2026-11-20: Eye Magnesis and Eye Rune Pick: runes pick what the eyes look at, and a held metal object goes there; built, untested on console

Two new rows under Controls > Motion Controls, after **Eye Aim Assist**, both off by default and drawn only while the
runtime offers a gaze pose: **Eye Magnesis** (`EyeMagnesis` in `bettervr.json`) and **Eye Rune Pick** (`EyeRunePick`).
They act in first person only. With a switch off, or without a valid gaze sample, every rune behaves as in v0.6:
the target is the one in the middle of the view and a held object follows the hand while the right grip is held.

- **The pick.** BotW chooses the Magnesis and Stasis target on the GPU: a pixel shader tests one point of the picture,
  passed in a 0x20-byte pixel uniform block, and the game reads the result back. Upstream BetterVR already moves that
  point from the picture's middle to the left eye's optical centre (`hook_ModifyPixelUniformBlockData`, call site
  0x038B2B2C). With **Eye Magnesis** on and the Magnesis rune out with nothing held, or **Eye Rune Pick** on and
  Magnesis or Stasis out, the point becomes where the gaze falls in the left eye's picture, through the projection the
  foveation code already uses. The grab is still the right trigger (or a short grip press), as before. Whether the
  game accepts a point away from the middle is the first thing to check (step 1 below). **Cryonis is not included:**
  its pillar is placed from the gameplay camera's ray, not from this screen point, so moving the point cannot move
  the pillar; the row leaves Cryonis to the camera and one INFO line says so.
- **Steering (Eye Magnesis).** While a body is held, the hold point is where a steering ray from between the eyes
  meets the circle of the game's own Magnesis distance around Link: the eyes set the direction and the height. The
  steering starts on the object at the grab, follows the gaze once it is more than 2.5 degrees away (so a gaze
  resting on the object leaves it still), and is kept within 60 degrees of level. The game's distance, height and
  direction fields are written by the same aim hook (0x020D858C) and through the same release-threshold clamps as the
  hand's aim, so the object chases the point rather than jumping and the game does not drop it from our side.
- **Push and pull (Eye Magnesis).** While the eyes steer, the right stick's up/down no longer reaches the game (where
  it raises and lowers the object) and instead pushes it away or pulls it in, at up to 6 m/s with full deflection
  (the deflection squared, for fine control near the middle), starting from the game's own distance each frame so
  the game's range stays its own. A pull stops at 1.5 m unless the object is already closer. Turning on the stick's
  left/right is unchanged.
- **Turning.** Unchanged: the right grip held and the wrist's turn since the squeeze rotate the object (patch 0013's
  spin path). The hand no longer moves it while the eyes steer.
- **Hand steering (Eye Rune Pick on, or Eye Magnesis with no gaze sample).** A grip starts the hand's aim from where
  the object is, not from a hand-to-object lever: the hand's turn since the grip turns the object around Link, and the
  hand's travel since the grip relative to Link moves it, so a grab after the eyes steered does not swing it from
  11-14 m away. The wrist's twist turns the body only; it never changes the object's distance, which the old
  reach-squared stick did. With both eye rows off the hand path is upstream's own: the object hangs on the stick from
  the hand, and holding the hand further out pushes it further away.
- **The gaze.** Each sample goes through a low-pass whose cutoff rises with the gaze's speed (2 Hz at rest), in
  stage space, so a head turn while looking at one spot does not swing the ray. A blink keeps the last direction for
  0.6 s; a longer loss hands Magnesis back to the hand.

Logs, all INFO: `[magnesis] rune out: N (Magnesis 2, Stasis 3, Cryonis 4)` whenever the rune changes, whether or not
a row is on (the indices 2-4 are inferred from the game's rune order, camera 5 is upstream's); with Eye Rune Pick on
and Cryonis out, one `[magnesis] Cryonis places its pillar from the camera's ray...` line says why it is left alone;
`[magnesis] eye pick on/off (rune N)`, `[magnesis] eye pick at X Y (centre X Y), N blocks` at most twice a second
while picking, `[magnesis] eye steering on (distance D)` per hold, and `[magnesis] eye steer: gaze off A deg, distance
game G (wrote W) now N, stick S, height H` at most twice a second while steering. The hand path adds `[magnesis] hand
steer: dir Y P deg, object D m, reach Rx, move M m, path upstream|gaze pick` at most twice a second; `move` is the
hand's travel since the grip relative to Link, so his own walk is not counted. `game` against the previous line's `now`
shows whether the game clamps the distance at its range. A new `HandAim::TryGetGazeWorldRay` places the gaze ray where
the camera puts the eyes, and `GameState::GetActiveRune` reads RuneMgr's selected rune.

New patch bettervr 0041 (`magnesis.cpp/.h`, `stereo_view.cpp`, `controls.cpp`, `hand_aim.cpp/.h`, `game_state.cpp/.h`,
`mod_settings.h`, `imgui_menus.cpp`); no Cemu, runtime or asm change.

**Test request 74 (v0.7 Eye Magnesis, 13.60 console).** Install the `BotW-VR-v...zip` from this head's green
"BetterVR build" run. On the PS5, Settings > Accessories > PS VR2: eye tracking on and **Adjust Eye Tracking** run
once. Put the headset on before starting the game, play in first person, and open BetterVR's menu > Controls > Motion
Controls: turn **Eye Magnesis** and **Eye Rune Pick** on.

1. **Off-centre pick (first; it decides the feature).** Stand where two metal objects are about 20 degrees apart left
   and right (the Magnesis shrine's first slabs or crates). Take Magnesis out. Keep the head still and look with the
   eyes only at the left object, then the right one, then one above or below the middle of the view. Say which one
   highlights each time. Pull the trigger while looking at one that is not in the middle: is it the one grabbed?
2. **Steering.** Holding it, keep the head still and look left, right, up and down: does it follow the eyes, and how
   quickly? Look straight at it for 10 seconds: does it stay put, or sink or drift? Turn the head and walk: it should
   come along.
3. **Push and pull.** Right stick up and down: does it go away and come closer? Push it to the far end, then pull:
   does it answer at once? Then pull it all the way in to the 1.5 m stop and, still holding, bring the hand to your
   chest or tilt it up: the object must stay in front at no less than 1.5 m (or the hand's own distance if that is
   closer), never jump behind you or stop answering. Does left/right still turn as before? The `hand steer` line's
   `stick` shows the deflection the hand paths used.
4. **Turning.** Hold the right grip and twist the wrist both ways: it must turn in place and not slide. Let go of
   the grip quickly once: say if the object is dropped.
5. **Blinks and closed eyes.** Blink normally while steering: no jerk? Close the eyes for 2 seconds: the object should
   stop following and the hand (with the grip held) aims as before; open them and it follows the eyes again.
6. **A door or sliding block.** Move one by looking: does the game let go of it?
7. **Hand steering, Eye Magnesis off.** Turn **Eye Magnesis** off and keep **Eye Rune Pick** on. Take Magnesis, look at
   an object off the middle and grab it with the grip held: does it come to the gaze point? Now move the hand left,
   right, up and down: the object must move the same way from where it is, not the opposite way. Turn the hand to
   point elsewhere: it should turn around you, not swing on an 11-14 m arm. Hold the grip and twist the wrist both
   ways: it must rotate and keep its distance. Say if any move goes the wrong way.
8. **An object above you (both hand paths).** With Eye Rune Pick on and Eye Magnesis off, grab a metal object high
   above the player (a crate on a ledge, or lift one and keep it there). Tilt the hand up and down slowly: the object
   must follow the tilt the same way at 60, 80 and 89-90 degrees (point at the sky for the last), with no dead spot
   near straight up and no swing the other way; 89-90 is what reaches the old gaze-pick near-zero guard. Do it once
   more with both eye rows off, again through 89-90 degrees: that run is upstream's path apart from this tilt and is
   what shows the old clamp's region. Send the `hand steer` lines' `dir` values and `path` for both.
9. **Stasis.** With Stasis out, look (eyes only) at an object off the middle: does it highlight, and does the trigger
   freeze that one?
10. **Cryonis.** With Eye Rune Pick on and Cryonis out, look at water off the middle: the pillar preview is expected to
    stay where the camera points, and the log must say why. Say whether it moved at all.
11. **Both off, then the gaze pick.** Turn both **Eye Magnesis** and **Eye Rune Pick** off: Magnesis picking and
    steering must behave as in v0.6, with the hand's own stick as upstream has it, and the right stick's up/down now
    pushes and pulls instead of raising and lowering. Then turn **Eye Rune Pick** back on with **Eye Magnesis** still
    off and repeat step 7: that is the new hand steering. Try both and say which feels right.

**Send back:** what each step did, and from `/data/ps5cemu/logs/boot.log`:
`LC_ALL=C grep -a "\[magnesis\]\|Supports Eye Tracking" boot.log`.

**What it decides.** Step 1 whether the game takes an off-centre pick point (if not, the pick needs a different
hook and steering still stands); the `rune out` lines the rune indices and that they show without a row on; step 2
the dead zone and smoothing; step 3 the stick speed and whether `game` follows `wrote` at the range end; step 4 that
turning no longer moves the object; step 5 the hand-over; step 6 the release thresholds; step 7 the hand's direction
and that a wrist twist keeps the distance (`hand steer`'s `reach` may still move with the hand, but `object D m` must
not change on a twist); step 8 that the turned lever follows a hand above an overhead object with no clamp or
reversal; step 9 Stasis; step 10 that Cryonis is left to the camera and its line is clear; step 11 the upstream hand
path with both rows off, the stick's push and pull there, and which of the two hand steers feels right.

## 2026-11-20: eye tracking is offered to the game, and one strength bends a throw and an arrow; built, untested on console

The 13.60 gaze run behind Test request 60 showed a stream that can be used: about 51.5 new samples a second, unit
directions in the firmware's headset frame (`-z` forward, `+x` right), and a blink or the headset off reported as an
invalid sample. `hmd2::gaze_available()` therefore no longer answers false in this build. It binds the three gaze
exports at the firmware table whose anchor matched and answers true before a session, when only the table can be
checked; after `InternalGazeStart` it is true only while the stream runs. With it true the runtime offers
`XR_EXT_eye_gaze_interaction`, builds the eye-gaze profile and its pose space, and rotates each sample by the frame's
head pose into stage space; BetterVR logs `Supports Eye Tracking: Yes`. A firmware with no verified table offers nothing;
a session whose start returned nonzero hides the row and leaves the gaze space untracked, as in v0.6.

**Eye Aim Assist, one strength row.** Controls > Motion Controls has one row, **Eye Aim Assist**, a 0-100% slider
from **Off** to **Full**, off by default and drawn only while the runtime can offer a gaze pose. It replaces v0.6's
hidden **Gaze Throw Assist**, and its setting is `EyeAimAssist` in `bettervr.json`; `EyeGazeThrowAssist` is no longer
read, so a leftover true cannot turn it on. A saved on/off `true` now reads as 25%. With a strength and a valid
sample:

- a released throw is bent toward the point 10 m along the gaze ray from the head, around the hand the throw left from
  (the left hand when the object was in it), by the raw angle to that point times the strength and at the hand's
  speed. Full takes the whole angle, so the throw goes straight at the gaze point; there is no 15-degree cap;
- a player's arrow is pulled toward the point 100 m along the same ray, from the bow's own origin, by the raw angle
  times the strength. The cone it acts in widens as the strength falls (20 degrees at 25%, 10 at 50%, none at Full):
  a gaze further off changes nothing, so below Full the assist cannot pull an arrow away from where the bow points.

One log line per assisted throw or shot, at INFO so it needs no debug switch:
`[Throw] eye aim assist: right hand, 12.4 degrees to gaze, 25% strength, 3.1 applied` and
`[BowShot] eye aim assist: arrow, 2.1 degrees to gaze, 25% strength, 0.5 applied`. The first number is the raw angle
between the released direction and the gaze, then the strength and what was applied; no line means no valid sample,
no strength, or an unchanged shot.

New patch bettervr 0040 (`controls.cpp`, `bow.cpp`, `mod_settings.h`, `imgui_menus.cpp`); no Cemu change. The offered
extension and the hidden state are in `port/bettervr/xr/runtime.cpp` and `third_party/hmd2/`.

**Test request 73 (v0.7 Eye Aim Assist, 13.60 console).** Install the `BotW-VR-v...zip` from this head's green
"BetterVR build" run and start Breath of the Wild in VR with the headset and both Sense controllers. Keep eye tracking
on and calibrated in the PS5's Settings > Accessories > PS VR2.

1. **The row and the strength.** Open BetterVR's menu > Controls > Motion Controls. **Eye Aim Assist** is one row of
   five buttons: **Off**, **25%**, **50%**, **75%**, **Full**. It must start on the nearest saved step: with this
   head's rc3 settings file the logged 39 % shows **50%**, and a fresh file shows **Off**. To check the retired rc2
   key, stop the game, set `"EyeGazeThrowAssist": true` in `/data/botwvr/launcher/bettervr.json` with no
   `"EyeAimAssist"` key, and start it again: the row must show **25%**. A right press must only move the focus from
   button to button, never change the value; press select to set one. Set **25%**, quit to the library and start the
   game again: it must still be 25%. (A firmware with no verified gaze table, or eye tracking off, must show no row.)
   Leave it at 25% for steps 2 and 4, then use 50%, 75% and Full in steps 3 and 5.
2. **A throw at 25%.** Hold something throwable (a bomb or a weapon) and look 10-20 degrees left of where the head
   faces, at a tree or an enemy; throw it at that target. Do it once looking left and once looking right, so a
   mirrored sign shows. At 25% the throw should bend a quarter of the way to the gaze, not the whole way.
3. **The strength on a throw.** Repeat step 2 at 50%, 75% and Full. The object should land nearer the target at each
   step, and at Full it should fly straight at the point you look at, not at the hand's direction.
4. **Arrows across the aim.** At 25%, draw the bow and shoot a target just left of the bow's aim, then one just right,
   each within about 20-25 degrees of the aim. Each arrow should bend a quarter of the way toward the gaze and land
   visibly nearer that target. Repeat at 50% and 75%: each step should bend more. At Full: an arrow should fly along
   the gaze even when the bow points 40 degrees away.
5. **A gaze outside the cone.** Below Full the cone is 30 degrees. At 25%, aim at a target, look 35-40 degrees away
   from the bow's aim, and shoot: the arrow must go where the bow points, and the log must say
   `outside the 30 degree cone, unchanged`. At Full there is no gate: the same shot must follow the gaze.
6. **The same throw, Off.** Set the row to Off and repeat step 2: the object must go where the hand throws, and no
   `[Throw] eye aim assist` line may appear.
7. **A blink.** Blink or close your eyes as a throw is released and as an arrow is shot: no assist line, and the throw
   or arrow goes where the hand or bow pointed.

**Send back:** whether the row showed five buttons, started on the nearest saved step (the rc3 39 % as 50 %, an old
`EyeGazeThrowAssist` true as 25 %), moved only its focus on a right press and kept its step across a restart; what the
throws and arrows did in steps 2-6; and from `/data/ps5cemu/logs/boot.log`:

- `LC_ALL=C grep -a "Supports Eye Tracking\|gaze: XR_EXT\|eye aim assist" boot.log`
- the `[gaze] ... valid 1` and `valid 0` lines around the throws and shots (the first minute has two a second, then
  one every 10 s), if the grep above does not already show them.

An assisted line reads `... degrees to gaze, N% strength, M applied`, and a shot outside the cone reads
`... outside the 30 degree cone, unchanged`: send those, with the step they came from.
**What it decides.** Step 1 the five-step row, that a right press only moves the focus, the nearest saved step (the
rc3 39 % as 50 %) and the retired `EyeGazeThrowAssist` true as 25 %; step 2-3 that the throw's bend is the raw angle
times the strength, with Full taking the whole angle; step 4 the quarter, half and three-quarter bends on an arrow;
step 5 the one cone and its line; step 6 the off path; step 7 that a blink does not bend anything.

## 2026-11-20: first person with the DualSense follows the first-person Snap Turn step; built, untested on console

The cd60daf report (13.60, DualSense as player 1, Sense controllers connected): in first person with First Person
Snap Turn at 45, the right stick still turned smoothly; third person's snap and smooth turning both worked. The
pulled boot log (`logs-turn2`) has `- Camera Mode: First Person (Recommended)` and `[turn] mode: first person snap, 45
degrees` from 34.997 s in the latest run.

**Cause.** The first-person snap latch read only the OpenXR right stick. A DualSense's right stick reaches the game
as its VPAD sample, and the game applies that horizontal itself; first person withholds only the OpenXR part, so the
step was never requested and the game's smooth yaw was what turned the view. Upstream's first-person path reads only
the VR stick in the same way, so the smooth turn is upstream behaviour, not a change from patches 0012 or 0036; the
third-person snap added by 0012 reads the physical stick and had no first-person counterpart.

**The fix.** Patch bettervr 0039 adds the physical stick to the first-person latch, the same sum the third-person
path uses, and withholds the physical horizontal from VPAD while a first-person snap is active (not in flat camera
mode); one push is one step at the angle as set. With Snap Turn off nothing is withheld and the DualSense turns the
game's camera as before. The first-person `SnapTurn:` line is INFO, as the third-person one is, so a console log
shows each step.

**Test request 70** in the next entry, step 4, covers this case.

## 2026-11-20: a black-bars 2D layer reaches the quad as the game drew it; built, untested on console

The v0.6.1 report (13.60, new game): the first rune's 2D-only panel is alone on black again, but its picture looks
like a stretched window, the same look the first-time pick-up panel had while it was 2D-only. The pulled boot log
(`logs-rune3/run1`) has `2D-only presentation starts for 'Demo003_0' (the 2D layer alone, black around it)` and
`Event 'Demo003_0' started at 'Item02' (first person ON, ignore camera rotation ON, game-animated hands OFF)` at
1150.574, then `compose mode mono (frame 63602): no projection layer or eye picture this frame; the 2D picture alone
in front of the player, black outside it` at 1150.580. The last `Camera Mode:` line before it is `First Person
(Recommended)` (350.330): this run is first person, and patch 0032's check comes before the first-person gate.

**Sizes.** `view configuration: two views of 1984x2048` (6.630), `Found rendering resolution 2480x2560` (19.911) and
`HUD mip chain: 2480x2560, 2 levels` (20.295): the 1.25x VR Resolution preset makes the game's surface, the Layer2D
swapchain and the HUD image 2480x2560, an aspect of 0.969. `Layer2D::FinishRendering` submits one quad of 16:9
(`aspectRatio = 16/9`, `hudSize`), and the runtime maps the whole image rect onto it.

**Why it stretched.** With black bars, the 2D layer takes the opaque ImGui path in `Capture::OnHud`:
`ImGuiOverlay::Render` draws the HUD image into a centered 16:9 region of the 2480x2560 surface
(`CalculateCenteredAspectRegion`, called with `centerTo16x9 = true`), so the picture is squeezed into 2480x1395 rows,
0.545x vertically, before it reaches Layer2D. The quad then maps the full 2480x2560 image onto 16:9, which undoes
that 0.545 by stretching the picture 1.835x horizontally: a 16:9 window about 1.8x too wide. Normal play does not
take that path: the PS5 shortcut added by patch bettervr 0007 leaves the game's HUD image as the layer, and the same
16:9 quad restores the game's own 2D scaling. The runtime's compose, the game and Cemu are not involved.

**The fix.** Patch bettervr 0038 changes patch 0007's PS5 condition so a black-bars frame also keeps the game's own
2D image; `DrewOverlay()` still takes the opaque path when a BetterVR overlay is up. The quad is unchanged, so the
panel is the picture the game drew at its true aspect. Normal stereo, the HUD, menus, `Demo002_0`'s camera hold and
the runtime's compose are unchanged.

**Before patch 0032, and why not the 0037 hold.** In v0.6.0/RC6 the rune sequence showed the 2D panel over a 3D
picture for about 29 s: the game kept drawing and BetterVR kept submitting fresh stereo pairs, so the compose stayed
stereo with the quad over the pair; the report called that 3D picture bad, not the panel. The runtime's
mono/no-fresh-pair path only took over for the last ~4.4 s, when the panel was alone on black (`logs5-out.txt`
section 4). Patch 0032 removed the 3D part for the whole event. Patch 0037 is not a substitute: it latches only for
`Demo002_0` (`Events::IsFirstTimeItemGetActive()`) and only in third person, while this run's `Demo003_0` started in
first person; holding the player's own camera would put the open world behind the panel, not the black surround, and
`Demo003_0` is a 2D screen, not the pick-up's moving third-person camera. The `useBlackBars` path remains the right
presentation for it.

## 2026-11-20: the first-time pick-up holds the third-person camera where it was; built, untested on console

The v0.6.0 report (13.60): the first Bird Egg pickup ran `Demo002_0` while the item panel was up
(`play7/logs/boot.log` 1607-1609, 610.976-614.392; also 446.345-448.214 and 63.021-65.380). The panel was sharp and
steady, but the 3D picture behind it jumped to a different camera angle almost every frame (behind, side, above): a
violent strobe in the headset. Test request 71's build showed the panel alone on black in third person, but the
panel looked stretched, so `Demo002_0` is back in 3D and the event camera is held instead.

`Demo002_0` is "Pick up Get Demo" in upstream's table (`patch_Settings_Cutscenes.asm:60`), started directly or at
`GetAnotherInnerItem`; the game runs it when an item type is picked up for the first time. In play3-play7 the three
starts in `play7/logs/boot.log` are at `Demo002_0` and play3/play6 also log it at `GetAnotherInnerItem` (21 starts).
Chests use other ids: `Demo000_0` "Treasure Chest Get Demo" (52 starts in play3-play7) and `Demo000_2` "Treasure
Chest Get Demo (inventory full)" (11), both first person and `PAN_OFF` like `Demo002_0`; cooking is `Demo001_0`. The
runes are `Demo003_0` at `Item01`-`Item03` (9 starts).

**Why the camera jumped.** In third person `GameState::IsFirstPerson()` is false for the whole event, so upstream's
event trampoline (`patch_FirstPersonMode_Events.asm`) does not skip the `CameraEvent` actions (`Look`, `LookDirect`,
`Turn`, `Idling`, `Move`, ...) that `ignoreCameraRotation` skips in first person. The pick-up demo's 3D picture is a
scripted camera move around Link and the item, and each cue reaches the render camera on the frame it starts, so the
base camera changes every 60 Hz frame. Both eyes take the same middle base pose; only the base camera moves.

**The fix.** `Demo002_0` is no longer part of the 2D-only presentation patch 0032 introduced for `Demo003_0`, so the
pick-up is 3D in either camera mode and `Demo003_0` keeps patch 0032 exactly as it was. Instead, patch bettervr 0037
latches the player's own pose when a `Demo002_0` event starts (`Camera::LatchEventCameraHold()`) and
`Camera::ResolveGameplayBasePose()` returns that pose while the event runs in third person. That is the function
through which the render camera, the visibility check and the reference matrix take the game camera every frame, so
the event camera's moves are ignored there and the headset pose is still composed on top. The hold is released when
the event ends. First person and every other event are unchanged.

**Why not upstream's fixed-camera path.** Upstream keeps the camera still during an event through
`ignoreCameraRotation` and `hook_ShouldSkipEventCamera`, and the "Cutscenes on a still 2D screen" preset turns that
on. The hook returns true only while `GameState::IsFirstPerson()` holds, because it is there to stop first-person
nausea; it skips the event camera actions, which first person already skips for this event, and it does not hold the
VR base pose in third person.

Logs: `Camera hold starts for 'Demo002_0' (third person, held at (x, y, z); head tracking still applies)` when the
event starts in third person, and `Camera hold ends for 'Demo002_0' (third person, held at (x, y, z))` when it ends.
`Demo003_0` keeps `2D-only presentation starts for 'Demo003_0' (the 2D layer alone, black around it)`.

**Test request 71 (v0.6.1 first-time pick-up camera hold and rune panel, 13.60 console).** Install the
`BotW-VR-v...zip` from this head's green "BetterVR build" run and start Breath of the Wild in VR.

1. **Normal play and another cutscene, unchanged.** Play about 60 s in the open world, then trigger a cutscene that
   is not an item get (a memory, or a tower). Does the world still have depth, and does the cutscene still play in 3D
   as before?
2. **Third-person camera (the report's case).** In the VR menu set Camera Mode to Third Person (the cutscene camera
   mode can stay at its default). Pick up an item type the save has never picked up, so the big panel appears (a new
   game's first Apple or Bird Egg, or any material you have not taken on this save; the small toast means that type
   was already taken). Watch the whole item-get presentation, then turn your head left and right and lean. Does the
   world stay where it was, with no scripted angle change behind the panel, and does it keep its depth? Does the item
   name/description panel show, stay readable, and stay in front of you when you turn? Repeat once with a second new
   item type if one is available and say which items they were.
3. **First-person camera (must stay as v0.6.0).** Set Camera Mode to First Person and pick up another item type the
   save has never taken. Watch the whole item-get presentation. Does it show the 3D presentation exactly as v0.6.0
   did (no camera hold, no black around the panel, the scene behind it steady), and does the panel stay put when you
   turn? Say what the 3D picture showed, because the same event must not have lost its depth.
4. **The rune sequence, unchanged in what it shows.** Collect a rune that downloads a panel (`Demo003_0`, for
   example the Magnesis rune). Does the 2D layer still show alone on black for the whole download, as in Test
   request 66? Is the panel a 16:9 window at its true shape, with no stretch (the text and the window's border the
   same proportions as in flat play), and does it stay steady when you turn your head?
5. **A chest, unchanged.** Open a chest in either camera mode. The chest and Link's hands must still show in 3D as
   before; no camera hold line may name `Demo000_0` or `Demo000_2`. Say whether the item panel after the chest opens
   looks as it did in v0.6.0.
6. **Send**, from `/data/ps5cemu/logs/boot.log`:
   - `LC_ALL=C grep -a "Camera hold\|2D-only presentation\|Event 'Demo002_0'\|Event 'Demo003_0'\|Event 'Demo000_" boot.log`
   - `LC_ALL=C grep -a "compose mode" boot.log`
   - `LC_ALL=C grep -a "\[draws\]" boot.log`
   - `LC_ALL=C grep -a "Found rendering resolution\|HUD mip chain" boot.log`
   - Which Camera Mode was set for steps 2, 3 and 4.

**Expected if it works:** in third person, one `Camera hold starts for 'Demo002_0'` line on the event's started line,
`compose mode` stays stereo for the whole event, the world behind the panel does not change angle, and one
`Camera hold ends for 'Demo002_0'` line at the event's end. In first person there is no camera hold line and the
event is the 3D presentation v0.6.0 showed. `Demo003_0` still logs `2D-only presentation starts` and its
`compose mode mono` line, as before, and the panel is the game's own 2D picture at its true 16:9 shape. The chest
step has no camera hold line and its `compose mode` stays stereo.

**What it decides:** a pair of camera hold lines with a steady, deep world and a readable panel in third person
closes the follow-up. A third-person event with no hold line means `IsThirdPerson()` was false when the event
started (check whether the scope or camera rune was up); a hold line in first person means the mode gate did not
take: send the lines from step 6 and the camera mode. A world that still changes angle behind the panel means the
hold does not reach the render camera. A missing or misplaced panel means the hold took the Layer2D reference with
it; the next step is to hold only the camera matrices. A `2D-only presentation starts for 'Demo003_0'` that changed
means the rune event lost its presentation, and a 2D-only line for a chest or the pick-up means the list is too wide.
A rune panel that is still stretched is the black-bars image taking the letterbox again: send the camera mode, the
`Found rendering resolution` and `HUD mip chain` lines, and whether the stretch is wide (the picture wider than tall
beyond 16:9) or narrow.

## 2026-11-20: v0.6.1 hotfix: third-person smooth turning reads the game's own yaw delta again; built, untested on console

The v0.6.0 report (13.60, DualSense, the Sense controllers off after 251 s; Camera Mode Third Person from 376 s):
**Snap Turn (third person)** on gave one 45-degree step per push (`SnapTurn: turning ±45 degrees in third person`,
388-640 s), but with it off the right stick did not turn the view at all, and while swimming the view turned by
itself.

**Root cause.** Patch bettervr 0012 moved the displaced `lfs f12, 0x194(r1)` in `captureCameraChaseStickYaw`
before the trampoline's stack pop but kept the game-frame offset. The trampoline's frame is 0x20 bytes, so before
the pop the same slot is `0x1B4(r1)` (the value `hook_AddGameCameraStickYaw` already reads as "the delta, 0x20
higher up"). `0x194(r1)` read the game's `0x174` slot instead. With Snap Turn on the override hook writes f12, so
the wrong load never reached the camera; with it off the chase camera applied that other slot every frame: zero on
foot (no turn) and a nonzero swim value in water (yaw with no stick). The first-person path
(`hook_AddGameCameraStickYaw`, `s_baseYaw` plus the captured delta, and `SetSmoothTurnInput` for a Sense) read the
right slot and was unchanged. It was not a stick deadzone or drift, and not a stale snap: the log has no
`SnapTurn: cancelled` line, and with snap off no request is made.

**The fix.** The chase trampoline loads `0x1B4(r1)`, exactly the instruction v0.5.1/upstream executed after the
pop; the snap hook still replaces it when Snap Turn is on. One `[turn] mode:` line when the effective mode changes
(first or third person, snap or smooth, with the angle or speed), and rate-limited (after a quarter second with
the right stick at rest, at most one a second, with the frames held back) `[turn] ... applied N deg of yaw with the
right stick at rest` lines in the first-person application and at both third-person stick-yaw sites. New patch
bettervr 0036; no Cemu change.

**Test request 70 (v0.6.1 smooth turn, 13.60 console).** Install the `BotW-VR-v...zip` from this head's green
"BetterVR build" run and start Breath of the Wild in VR. Say which controller each test used (DualSense or Sense).
Keep Comfort > Advanced > Turning at Snap 45 for steps 1-3 unless a step says otherwise.

1. **Third person, snap off, on land.** Set Camera Mode Third Person and Play Style > Third-Person Camera >
   Snap Turn (third person) off. Stand still on land, push the right stick fully right for about a second, release,
   then left for a second. The view must turn smoothly at the v0.5.1/upstream rate, stop when the stick returns, and
   not drift while it rests. Send the one `[turn] mode: third person smooth, the game's right stick` line.
2. **Third person, snap off, in water.** Without touching either stick, float (tread water) for 10 s. The view must
   not rotate. Then push the right stick right for a second and release; the turn must stop. There must be no
   `[turn] ... at rest` line in this step. Send every `[turn]` line of the test.
3. **Third person, snap on.** Turn Snap Turn (third person) on. One push right must step 45 degrees and log one
   `SnapTurn: turning 45 degrees in third person`; holding must not step again until the stick is released past the
   deadzone and pushed again; one push left steps -45. Send the `[turn] mode: third person snap, 45 degrees` line
   and every `SnapTurn:` line.
4. **First person.** Set Camera Mode First Person and Comfort > Advanced > Turning to Smooth. Repeat step 1's
   pushes: the DualSense's stick and, with the Sense controllers on, a Sense right stick must turn smoothly, and no
   `[turn] ... at rest` line may appear. Then set Turning to Snap 45 and push the DualSense's right stick once each
   way; each push must be one 45-degree step and log one `SnapTurn: turning 45 degrees`, and holding must not step
   again until the stick is released past the deadzone and pushed again (first person must follow the setting, not
   the game's smooth turn). With the Sense controllers on, the Sense right stick must do the same. Send the
   `[turn] mode: first person smooth, N deg/s` and `[turn] mode: first person snap, 45 degrees` lines and every
   `SnapTurn:` line.
5. **Both controller types.** Repeat step 1 with the Sense right stick (Sense controllers on) and say whether it
   matches the DualSense. Then repeat with the Sense controllers off and the DualSense only.
6. **Send** `LC_ALL=C grep -a "\[turn\]\|SnapTurn:" boot.log` and say, for each push, whether the view turned the
   way the stick was pushed and stopped on release.

**What it decides.** A smooth turn with both controllers and no at-rest lines closes the report. An at-rest line
names the source and the degrees it applied; the mode line tells which path was active. If a chase-camera line
persists after the stick is released, the game's own yaw still reaches the camera and the next step is the game's
chase state, not our stick map. A first-person push that logs one `SnapTurn: turning 45 degrees` and moves in one
step closes the DualSense case; a smooth move without that line means the game still received the stick.

## 2026-11-20: v0.6.0 ships asyncCompose on: the compose wait leaves Cemu's GPU thread, and the `[draws]` line splits the record; built; asyncCompose tested on console at 1x

The v0.6 console logs (1x and 1.5x, gameplay windows of 3,500+ draws) put the stereo pass's record at 12.3-13.5 ms at
1x and 12.9 ms at 1.5x, so it is the GPU thread's own work and not the eye's pixels; the compose wait was 4.6-5.1 ms at
1x and 11.6-11.8 ms at 1.5x, the GPU finishing the frame. A frame was the record plus that wait in series (about
18.5 ms, 54 fps), and the GPU had nothing to do while the next frame's first commands were recorded.

**asyncCompose, on by default.** `xrEndFrame` returns once the compose is submitted. The frame pump's next
`xrWaitFrame` (BetterVR's own thread, which already ran right after each frame end) waits for the compose's fence,
submits the frame to Hmd2 with the same render pose, and then naps and begins the next Hmd2 frame, in the same order
and at the same moment the synchronous path did; Cemu's GPU thread goes on to the next game frame. One compose is in
flight at most: an `xrEndFrame` that finds a compose the pump has not taken finishes it before it records its own (the
compose's command buffer, descriptor set and fence are reused), and a fence timeout is left to the GPU thread's next
`xrEndFrame`, which handles it as before. A `bettervr.json` with no `asyncCompose` key runs it on; `"asyncCompose": 0`
turns it off, and the frame end then does what it did, with its wait and submit in one function that `xrEndFrame` calls
at once. The startup log says `[vr-xr] compose wait: ... (bettervr.json's asyncCompose)`.

**Tested on firmware 13.60:** one session at 1x (a shrine, Bokoblins, the inventory, the scope and a save) averaged
59.9 fps of gameplay against 52.2 in the previous build's windows with the same draw counts; the 1% low rose from 44.4
to 58.0 fps, frames held for three refreshes fell from 31% to about 1% (2.01 refreshes an image), `xr end` fell from
4.5-5.1 ms to 0.07 ms, and repeated, skipped, queue-full and GPU timeout counts stayed at 0. 254 frames of 64,200 were
missed (0.4%), the submitted rate stayed within a frame of the game's (59.68 against 59.74 fps), there were no new
warnings, head turns felt the same and no visual issue showed. GPU busy was 56%: the 60 Hz slot stream is the ceiling
in those windows.

**The `[draws]` line.** It gains `| sequences N, index lists N (N KB), sets made N, pipelines made N` per frame: the
draws that set up their state in full (the first of a sequence), the index lists decoded (index cache misses) and
their size, and the descriptor sets and pipelines made. With `bettervr.json`'s `"profile": true` a second line,
`[vr-xr] [draws] record split per frame: sequence setup X ms, draws Y ms (uniforms, indices, buffers, pipeline and
sets, binds and the draw), between draws Z ms`, says where the stereo pass's record goes; it costs about 0.2 ms a
frame and only with the profile on. That session's split: setup 2.36 ms, draws 7.64 ms (of them binds and the draw
3.24 ms), between draws 2.08 ms, against a record of 12.14 ms, so the split is complete. Cemu's draw counters no
longer take a locked add on every draw. New patch: cemu 0058; no BetterVR patch.

**Test request 69 (v0.6.0 asyncCompose default and its off switch, 13.60 console).** Install the `BotW-VR-v...zip`
from this head's green "BetterVR build" run and start Breath of the Wild in VR with the headset and both Sense
controllers. Keep `"profile": true` in `/data/botwvr/launcher/bettervr.json` for all runs, and use one outdoor spot
for each: the same save, the same place and facing the same way, with grass and trees in view; each run, stand still
30 s, walk a small circle for 30 s, then quit to the library.

1. **Default, 1x.** Remove any `"asyncCompose"` key from `bettervr.json` (or set it to 1). The boot log must have
   `compose wait: on the frame pump's xrWaitFrame`. Say whether head turns feel like the last build (no extra lag,
   judder or smear) and whether the HUD, a menu and the inventory look as before.
2. **Off, 1x.** Set `"asyncCompose": 0` and restart the app (it reads the file at the session's start). The boot log
   must have `compose wait: in xrEndFrame on Cemu's GPU thread` with `("asyncCompose": 0)`. Same route; the fps should
   fall back to about the previous build's at this spot (about 52 fps in the matched windows) and `xr end` to
   4.5-5.1 ms.
3. **Default at 1.5x.** Put `"asyncCompose"` back to 1 (or remove it), set BetterVR's menu > System > VR Resolution >
   1.5x, restart the game, and play the same route; then set VR Resolution back to 1x.

Send each run's `boot.log` (Settings > Diagnostics > Copy logs to USB keeps the last five, or the file itself):
- `LC_ALL=C grep -a "compose wait:\|\[draws\]\|\[frame\]\|\[perf\]" boot.log`
- `LC_ALL=C grep -a "fps submitted\|compositor's GPU work\|VR start" boot.log`

**What it decides.** Run 1 shows the default path: its fps and the 600-frame line's `missed`, `repeated`, `skipped` and
`GPU timeouts` must match the tested 59.9 fps and zero. Run 2 must show the old rate, `xr end` back at 4.5-5.1 ms and
the off line in the log, which is the switch working. Run 3 says whether 1.5x, where the fence wait is 11-12 ms, keeps
`repeated`, `skipped` and `GPU timeouts` at zero and the submitted fps within a frame of `[perf]`.

## 2026-11-20: damage reads Link's life field from the first frame, so a fall rumbles; built, untested on console

The RC7 report: falling about 15 m did not vibrate. The log has `PlayerNormal state '着地ダメージ'`
(`0x101E0D18`) at 196.973, no `[vib] damage` line at all, and no life-probe line in the whole session. `+0x540`
was still the scan's field gated behind two damage states (test request 65, step 1): the probe only ran on
`0x101E0910`/`0x101E0930`/`0x101E0940` and the death state, so a session whose first damage is a fall (or fire,
cold or heat) never adopted it. The state hook does not list the landing-damage state, so the hit reached nothing.

**The fix.** `UpdateLifeScan` now reads the big-endian s32 at player actor `+0x540` itself, once the actor is
valid and no loading screen is up. A value in `0..480` quarter hearts (120 hearts, so every extra and yellow heart
fits) on three consecutive frames is adopted, and that value is the baseline: one
`[vib] life: player+0x540 adopted (12 of 480 quarter hearts), damage now follows its drops` line says so, with no
enemy hit needed first. A fall, fire, a cold tick or an enemy hit is then one Damage Taken event from the first
one. The rules are unchanged: every drop fires one `[vib] damage path=life <before>-><after> quarter hearts` line
and one falling rumble scaled by the quarter hearts lost; rises (food, containers) only move the baseline; a
loading screen or a new player actor resets the baseline and a lost actor clears it, so a load never fires.

**When the field is not the life field.** While the value never reads plausibly, the `PlayerNormal` state hook and
`PlayerLargeDamage` stay the source, exactly as in v0.5.0. A second of out-of-range values (60 frames) ends the
try and logs once, either
`[vib] life: player+0x540 never read a life value (<v>), the damage state hook stays the source` (never adopted) or
`[vib] life: player+0x540 left the life range (<v>), the damage state hook is the source` (adopted, then lost). The
150-frame scan ring, the 0.6 s probe baseline and the two-probe gate are gone; the probe's `did not fall` lines
with them.

New patch bettervr 0035 (`player.cpp` only); no Cemu change.

**Test request 68 (v0.6 fall damage from the first frame, 13.60 console).** Install the `BotW-VR-v...zip` from
this head's green "BetterVR build" run and start Breath of the Wild in VR with the headset and both Sense
controllers. Keep `HapticsLogPatterns` on (its default) and Damage Taken at 100 % with Hands and Headset ticked.

1. **Adoption, before any hit.** Load a save and stand still for about 5 s without taking any damage. Send the one
   `[vib] life: player+0x540 adopted (<n> of 480 quarter hearts)` line. If instead the line says `never read a life
   value (<v>)`, stop here and send it with the life and damage lines.
2. **Falls, the report's case.** Fall about 5 m without the paraglider (one quarter heart), then about 15 m (three or
   more). Each must pulse once, as Link lands, and log one `[vib] damage path=life ...` line. Say whether the
   one-quarter hit is clearly weaker than the bigger one.
3. **Fire and temperature.** Stand in burning grass for about 3 s, then wait for the cold ticks in Hebra (or the
   heat ticks in the Gerudo desert). Each tick that takes life must pulse once and log one `path=life` line.
4. **An enemy hit.** Take one hit from a weak enemy (a Bokoblin is fine). One pulse, one `path=life` line, and no
   second pulse from the state hook.
5. **Food.** Eat a meal that fills hearts. No pulse and no `[vib] damage` line while the life bar rises.
6. **A save load.** Save, then load the save (or fast travel). No pulse at the loading screen, during the load, or
   in the first second after it; no `[vib] damage` line there.
7. **Quit** to the library and use Settings > Diagnostics > Copy logs to USB. Send, from
   `/data/ps5cemu/logs/boot.log`:
   - `LC_ALL=C grep -a "\[vib\] life:" boot.log`
   - `LC_ALL=C grep -a "\[vib\] damage" boot.log`
   - the `PlayerNormal state` lines with timestamps from 10 s before to 10 s after each fall

**Expected if it works:** the adoption line at the start of the session; one `path=life` line and one pulse per
life drop, including the falls and the temperature ticks, at `hands 0.32` for one quarter heart and `hands 0.80`
for four or more (or reaching 0); no line for the meal; no line at the load.

**What it decides:** a fall line and a pulse close the report and make `+0x540` the damage source from the first
frame. A fall with no line means the field was not adopted (the adoption line says why) or its value did not drop
(the before/after values are in the `path=life` line when it fires); if it was adopted and stayed put, the next
check is the landing-damage state itself. An adoption line with a value that is not Link's hearts means the offset
moved in this build; send the value, and the fallback is the state hook. A pulse at the load means the baseline
reset missed it.

## 2026-11-20: an explosion plays from the blast actor's move, once per real detonation; built, untested on console

The RC7 report: explosion rumble felt delayed, two bomb arrows seemed to vibrate twice each, and a save load a few
seconds after the second arrow vibrated the headset on the loading screen.

**The lifecycle the old code was reading.** BotW's actor set has two kinds of resident actor (`ResidentActors.byml`):
`Explode` is `only_res: false`, i.e. one blast *instance* lives for the whole session and the game moves it to each
detonation; `BombArrow_A` and the four `RemoteBomb*` names are `only_res: true`, so only their resources are resident
and the game creates an instance per nocked arrow, per arrow in flight, per stuck arrow, per held bomb. The play4
start-of-session lines show the first load creating those names and the next actor list replacing them 1.4 s later.

- A nocked bomb arrow is a held `BombArrow_A` actor at the bow; on release the game drops that hold and creates the
  projectile actor, which flies with a moving matrix.
- On impact the projectile's AI ends and the actor stays in the list as a stuck arrow; its `BaseProc::state`
  (the byte at `BaseProc.state`) is the idle/not-calculating value **3 or more**, and it stays there: state 3 is not
  "being deleted". A real removal is the actor leaving the game's actor list, which the old code never watched.
- A detonation places the resident `Explode` actor: the game moves that one instance to the blast spot and runs its
  `BombExplode` action for 15 frames (a new instance, if the game makes one, is the same signal). The bomb or arrow
  itself is then freed or hidden, 1-N frames later.
- A save/area load frees a scene's actors as a batch, but the batch can arrive in ones and twos before the four-or-more
  frame, and it re-creates the resident set; the player actor (and `s_playerAddress`) goes away during it.

**Why the same actor fired repeatedly and twice per frame.** `UpdateExplosives` called `state >= 3` "gone" and removed
the record. `hook_UpdateActorList` walks the whole actor list on *every actor creation* and re-added the stuck arrow,
so the next frame saw state 3 again and played the row again: 14.6 m at 396.326 and 396.345, then 399.558, then twice
in one frame at 400.853 and again at 400.866. Two stuck arrows re-armed together give the two-in-a-row the report
heard. The mass-removal guard only caught the frame with five removals (407.797), not the 1.6 m one 0.6 s earlier
(407.209), which is the load rumble. A remote bomb picked up, a cancelled bomb and an un-nocked arrow all looked the
same.

**The fix.** The Explosions row now follows only the game's blast actor, `Explode`: `NoteExplosionActor` records its
pointer once, and each game frame `UpdateExplosions` reads its matrix. A move of more than 0.5 m (or the first placed
spot of a new record) within 30 m of Link is one detonation and plays one rumble; the same spot twice within 150 ms is
one blast, which covers a second blast actor placed for the same explosion. Removals, ids and actor states are no
longer read, so a nocked, spent or despawned arrow cannot fire. A loading screen, a missing player actor or a new
player actor clears the recorded spots and starts a one-second quiet window (BetterVR's own load signals, not a
removal count), so the load's own placement and the resident actor's re-creation cannot fire. The strength is
unchanged: full at 3 m, straight to nothing at 30 m. One line per event, under `HapticsLogPatterns`:
`[vib] explosion Explode 14.6 m +17 ms -> hands 0.57, ...`; the `+N ms` is the frame interval in which the move was
seen, so it is at most one game frame after the detonation.

New patch bettervr 0033 (`player.cpp` only); no Cemu change.

**Test request 67 (v0.6 explosion timing and dedupe, 13.60 console).** Install the `BotW-VR-v...zip` from this
head's green "BetterVR build" run and start Breath of the Wild in VR with both Sense controllers and the headset.
Keep `HapticsLogPatterns` on and the Vibration page's Explosions row at 100 % with Hands and Headset ticked.

1. **One remote bomb at three distances.** In a quiet spot with no enemies, detonate a round remote bomb at about
   3 m, then about 10 m, then about 25 m. Each must pulse once, as the visual blast starts (not after it), at
   `hands 1.00`, weaker, weaker still, and log one line each.
2. **Two bomb arrows, about 2 s apart,** into a wall about 10 m away. Exactly two pulses, one per impact, not at
   the draw, the release or the nock. This is the report's double; say whether any impact pulsed twice.
3. **A bomb arrow nocked and cancelled.** Draw a bomb arrow and put it away without firing (switch arrow type or
   lower the bow). No pulse and no `[vib] explosion` line.
4. **A remote bomb recalled.** Throw a bomb, then press the rune to recall/cancel it without detonating. No pulse
   and no line.
5. **A barrel about 10 m away.** Ignite an explosive barrel. One pulse, one line.
6. **A save load.** Save, then load the save (or fast travel), a few seconds after a blast. No pulse on the loading
   screen, during the load, or in the first second after it; no `[vib] explosion` line there.
7. **Quit** to the library and use Settings > Diagnostics > Copy logs to USB. Send, from
   `/data/ps5cemu/logs/boot.log`:
   - `LC_ALL=C grep -a "\[vib\] explosion" boot.log`
   - `LC_ALL=C grep -a "\[vib\] damage\|\[vib\] life:" boot.log`
   - the `Event '` and `compose mode mono` lines with timestamps from 10 s before to 10 s after the load

**Expected if it works:** one `[vib] explosion Explode <dist> m +<n> ms -> ...` line per real detonation, `n` at most
one frame (about 17 ms at 60 fps, more during a hitch); two arrow lines for two arrows; no line for steps 3-5 except
the barrel; no line at the load; strength full at 3 m and nothing past 30 m.

**What it decides:** a blast with no line means the game did not move the resident actor (or the record was not
found), and the next signal is the actor's scale/opacity or its `BombExplode` action state; send the lines. A line at
a throw or a draw means the actor is placed before the detonation and the signal needs the action state. Two lines
for one blast mean two records or a stepped placement; their positions and gaps say which. A line at the load means
the quiet window missed it; the timestamps against the `Event '` lines say where.

Test request 67 replaces steps 6 and 7 of test request 65: the cancelled bomb and the load are expected to be silent
now.

## 2026-11-20: the rune-download panel is the 2D layer alone for the whole event; built, untested on console

The RC's rune sequence kept a bad 3D picture behind BetterVR's 2D panel for 29 s of a 39 s `Demo003_0` event
(`boot.log` 480.9-520.2, `Item03`). The game keeps drawing while the panel is up, so BetterVR keeps submitting fresh
stereo pairs and the runtime's mono switch, which waits for a frame with no fresh pair, only fires at 510.9, 9 s
before the event ends (`logs5-out.txt` section 4). The panel is BetterVR's `Layer2D` quad; the pair behind it is the
bad picture.

`Demo003_0` is the Sheikah Slate download panel (upstream's table, `patch_Settings_Cutscenes.asm:61`), a 2D "get"
screen with nothing to see in 3D. `GameState::UseBlackBarsDuringEvents()` now returns true for that event, whatever
the camera mode and the `UseBlackBarsForCutscenes` setting: upstream's own black-bars presentation then leaves the
projection layer out of `xrEndFrame` from the event's first frame, and the 2D layer alone is composed with black
around it. The runtime's no-projection branch is unchanged. Other cutscenes and normal gameplay never enter this
path (the flag is set for `Demo003_0` only). No map-area event name appears in the play3/play5 logs, and the only
map-named events in upstream's table are `Demo004_0` "Map Tower Demo" (a third-person 3D camera scene, 28.9 s at
`boot.prev.log` 109.2-138.2) and `Demo039_0` "Divine Beast: Map Download" (not in these logs), so only `Demo003_0`
is covered. New patch bettervr 0032 (`game_state.cpp` only); no Cemu or runtime change.

Logs: `2D-only presentation starts for 'Demo003_0' (the 2D layer alone, black around it)` at the event's start, and
`2D-only presentation ends for 'Demo003_0'` at its end.

**Test request 66 (v0.6 rune-download 2D presentation, 13.60 console).** Install the `BotW-VR-v...zip` from this
head's green "BetterVR build" run and start Breath of the Wild in VR.

1. **Normal play and another cutscene, unchanged.** Play about 60 s in the open world, then trigger a cutscene that
   is not the rune download (a memory, or a tower). Does the world still have depth, and does the cutscene still
   play in 3D as before?
2. **A new rune (the report's case).** Pick up a rune you do not have (a new game up to the first rune pedestal, or
   any pedestal your save still allows) and watch the whole sequence without moving for the first 3 s, then turn
   your head left and right and lean. Is the panel on black from its first frame, with no bad 3D picture behind it
   at any point, and does it stay put in the room when you turn? Is the "Rune extracted" panel readable? If a
   second unclaimed rune is available, repeat once; say which entry point (`Item01`, `Item02`, `Item03`) each was.
3. **Send**, from `/data/ps5cemu/logs/boot.log`:
   - `LC_ALL=C grep -a "2D-only presentation\|Event 'Demo003_0'" boot.log`
   - `LC_ALL=C grep -a "compose mode" boot.log`
   - `LC_ALL=C grep -a "\[draws\]" boot.log`

**Expected if it works:** `2D-only presentation starts for 'Demo003_0'` on the same frame as the event's started
line, then `compose mode mono (frame N): no projection layer or eye picture this frame` from the first frame of the
event, with no `compose mode stereo` between it and `2D-only presentation ends for 'Demo003_0'`; the panel sits on
black for the whole event, and the `multiview` numbers keep moving under it because the game still draws. The next
`compose mode stereo` line is after the event's end.

**What it decides:** a mono line from the event's first frame and a steady panel on black close the report. A
`compose mode stereo` line after the 2D-only line means the projection layer still went out and the flag did not
reach the renderer; send the lines from step 3. A black or missing panel means the change took the 2D layer with
it, and the next step is gating the projection layer in the renderer instead.

## 2026-11-20: damage follows Link's life field and explosions play from the actors that go; built, untested on console

The RC's V3 scan found one field at player actor `+0x540` in quarter hearts and V4 saw the tracked bombs, barrels and
arrows go, but no source played the Explosions row. Both now do.

**Damage from life.** The scan is still the guard: 300 ms after a damage or death state, `+0x540` must have fallen
within its limits, on two probes, before any event uses it. A damage state that takes no life is not a wrong field,
so a miss is retried on the next state. Until then the `PlayerNormal` state hook is the source, exactly as in v0.5.0;
if the field does not fall on six probes, the state hook stays the source for the session. Once proven, every
drop is one Damage Taken event and the state hooks play none, so falls, burning, cold and heat ticks and a lethal hit
all count. The hands' amplitude is 0.4/0.6/0.8/1.0 of the state hook's 0.8 for 1/2/3/4+ quarter hearts lost, and
reaching 0 uses the full one; the headset pulse is about 11/13/15/17 Hz. A rise (food, a heart container) updates the
baseline and never fires. The baseline resets on a loading screen, on a new player actor and when the actor address is
lost, so a load cannot fire. One `[vib] damage path=life <before>-><after> quarter hearts -> ...` line per drop says
what the row asked for; one `[vib] life: player+0x540 is the life field ...` line says when it took over.

**Explosions.** The actor-list hook records names containing `Bomb`, `Barrel`, `Explo`, `Chuchu` or `Beam` as they are
created and follows the actor's id and state each game frame. When one goes within 30 m of Link, the Explosions row
plays one falling rumble on both hands, full at 3 m and straight to nothing at 30 m;
the headset follows through the mixer's level. Removals while a loading screen is up or the player actor is gone are
discarded, as are four or more in one frame (an area unload): that frame logs one `[vib] explosion: N gone in one
frame, ignored (area unload)`. One `[vib] explosion <name> <dist> m -> ...` line per actor says what the row asked for
or why it played nothing. A remote bomb that is picked up or cancelled also disappears and cannot be told from a
detonation, so that is a known false positive.

The V3/V4 spam (`life scan`, `life field` value lines, `explosive seen`, `explosive gone`) is gone; every new line is
under `HapticsLogPatterns` like the other `[vib]` lines. New patch bettervr 0031; no Cemu change. Details:
`UPDATE-1.0.md`, "Vibration: one page and one mixer".

**Test request 65 (v0.6 damage from life and explosions, 13.60 console).** Install the `BotW-VR-v...zip` from this
head's green "BetterVR build" run and start Breath of the Wild in VR with the headset and both Sense controllers.
Keep `HapticsLogPatterns` on (its default). In BetterVR's Vibration page keep Damage Taken at 100 % with Hands and
Headset ticked, Explosions at 100 % with Hands and Headset ticked, and Headset Limit 60 %.

1. **Validation, the first two hits.** Take two hits from a weak enemy (a Bokoblin is fine) at least 2 s apart, with
   no fall, fire or other damage in between. Expected: two `[vib] damage path=PlayerNormal ...` lines, then, 300 ms
   after the second, `[vib] life: player+0x540 is the life field, damage now follows its drops`. A single `did not
   fall (a->b), probe skipped` is retried on the next hit; if the line says `did not fall on 6 probes`, stop here and
   send it with the damage lines.
2. **Falls.** After the validation line, fall about 5 m without the paraglider (one quarter heart), then about 15 m
   (three or more). Each drop should pulse once and log one `[vib] damage path=life ...` line with the before and
   after values. Say whether the one-quarter hit is clearly weaker than the bigger one.
3. **Fire and temperature.** Stand in burning grass for about 3 s, then wait for the cold ticks in Hebra (or the heat
   ticks in the Gerudo desert). Every tick that takes life should pulse once and log one `path=life` line.
4. **Lethal and food.** Let a hit kill Link; the last pulse should be the large one. Reload, then eat a meal: there
   must be no pulse and no `[vib] damage` line while the life bar rises.
5. **Load.** Save and load once (or fast travel). There must be no pulse at the load or in the first second after it;
   send the `[vib] damage` lines from 10 s before to 10 s after.
6. **Explosions.** In a quiet spot with no enemies: detonate a round remote bomb at about 3 m, then about 10 m and
   about 25 m (shield up or behind cover); repeat the 3 m one with the cube bomb; shoot a bomb arrow at a wall about
   10 m away; ignite an explosive barrel about 10 m away. Say whether the hands and the headset pulsed and how strong
   each was, from weak to too strong. Write down the order.
7. **A cancelled remote bomb.** At about 5 m, press the rune to recall/cancel a remote bomb without detonating it.
   An explosion pulse here is the known false positive; send its line and say whether it was felt.
8. **Quit** to the library and use Settings > Diagnostics > Copy logs to USB. Send, from
   `/data/ps5cemu/logs/boot.log`:
   - `LC_ALL=C grep -a "\[vib\] damage\|\[vib\] life:" boot.log`
   - `LC_ALL=C grep -a "\[vib\] explosion" boot.log`
   - `LC_ALL=C grep -a "\[vib\] headset pulse" boot.log`

**Expected if it works:** the validation line after the second enemy hit; one `path=life` line and one pulse per later
drop, including falls and temperature ticks; no line for a meal; a one-quarter hit at `hands 0.32` with `pulse 11 Hz`
and a four-quarter or lethal one at `hands 0.80` with `pulse 17 Hz`; explosions at `hands 1.00` near 3 m, weaker at
10 m, weaker still at 25 m and nothing past 30 m; no line at a load, and possibly one `ignored (area unload)` line
when a load removes the scene's actors.

**What it decides:** the validation line and the `path=life` lines confirm `+0x540` and make the life field the
damage source in v0.6. `did not fall` means the field moved or the probe's baseline is wrong; the before/after values
say which. A fall or temperature tick with no line means the drop was missed or the value is not life; the surrounding
lines show it. An explosion pulse on a cancelled bomb is the known false positive, and the order in step 7 says
whether a distance or time filter can separate them.

## 2026-11-20: the optional VR exports cover seven libSceHmd2 tables, and a startup probe logs every resolution route; built, untested on console

The headset motor (`sceHmd2SetVibration`) and the gaze probe (`sceHmd2GazeGetResult`,
`sceHmd2InternalGazeStart`, `sceHmd2InternalGazeStop`) bound only through the 13.60 table, although
PS5VR carries rows for six more builds of libSceHmd2. Separately, nothing in the app had tested
whether `sceKernelDlsym` can see these functions under their NID, or under the loader's
`NID#lib#module` spelling; the research said names fail on 13.60, but not whether the other
spellings do.

**Step A, data only.** The two optional tables now carry every build PS5VR has a table for: 13.60,
12.40, 11.60, 10.00, 8.40, 7.61 and 5.50. The rows are PS5VR's (`app/src/vr_exports_*.h`,
GPL-3.0-or-later, Copyright (C) 2026 Husam Osman), and each build is keyed by the same anchor
string and offset our own `hmd2_exports.h` already uses, so a table is used only when its anchor
sits exactly where it says. 9.00 has no PS5VR row for either function and stays off; a build
without a matching anchor stays off as before, with its one `off on this firmware` line. The bind
lines now name the matched build (`the 12.40 table`, `the 7.61 table`, …). No link-time import was
added, and the Sense setters still bind by name or at libScePad's 13.60 table only.

**Step 0, log only.** `setup()` now runs one probe as soon as libSceHmd2 is loaded, before the
required resolve and before anything optional is called. For the motor, the three gaze exports and
the four libScePad Sense setters it prints one line each:

`hmd2: resolve sceHmd2SetVibration dlsym-nid fail dlsym-lib fail table 0x... (+0x2ae00)`

`dlsym-nid` is `sceKernelDlsym(handle, "Al4qjNREVQQ", ...)`, `dlsym-lib` is the same call with
`"Al4qjNREVQQ#libSceHmd2#libSceHmd2"`, and `table` is the address the bind itself would take (or
`-` when no table's anchor matches). The probe fills no slot, changes no latch and calls no address,
so the session behaves exactly as it would without it. The singular trigger setter has no row in
libScePad's table, so its `table` is expected to print `-`.

**What this does not change.** On 13.60 the same addresses bind as before (motor +0x2ae00, gaze
+0x35510/+0x31b60/+0x31b80, Sense mode +0x100b0, intensity +0x5610, both-hands triggers +0xa730).
The Sense setters as link-time NID imports and the elevation-helper resolver remain v0.7 work and
are not in this build. The six added tables are unrun: only 13.60 (this app) and 13.60/12.40
(PS5VR) have console results.

**Test request 64 (v0.6 resolution probe and the seven motor/gaze tables, 13.60 console).** Install
the `BotW-VR-v...zip` from this head's green "BetterVR build" run and start Breath of the Wild in VR
with both Sense controllers and the headset on.

1. **The motor.** Use the Vibration page's headset test, or take a hit. The headset must vibrate as
   in the RC.
2. **The Sense pair.** Use the Vibration page's Sense vibration row and its trigger test row. Both
   must behave as in the RC.
3. **The gaze stream.** Look around for about 10 s at the title screen or in the world. The probe
   starts the stream once, so its `[gaze]` samples should appear twice a second for the first
   minute.
4. **Quit** to the library and use Settings > Diagnostics > Copy logs to USB.
5. **Send** `/data/ps5cemu/logs/boot.log`, or these greps:
   - `LC_ALL=C grep -a "hmd2: resolve " boot.log`
   - `LC_ALL=C grep -a "\[gaze\]" boot.log`
   - `LC_ALL=C grep -a "headset vibration" boot.log`
   - `LC_ALL=C grep -a "Sense vibration exports bound" boot.log`
   - `LC_ALL=C grep -a "Sense trigger effects bound" boot.log`

**Expected on 13.60:** eight `hmd2: resolve ...` lines, in this shape and with these table offsets:

- `sceHmd2SetVibration ... table 0x... (+0x2ae00)`
- `sceHmd2InternalGazeStart ... table 0x... (+0x31b60)`
- `sceHmd2InternalGazeStop ... table 0x... (+0x31b80)`
- `sceHmd2GazeGetResult ... table 0x... (+0x35510)`
- `scePadVrControllerSetVibrationMode ... table 0x... (+0x100b0)`
- `scePadVrControllerSetVibration ... table 0x... (+0x5610)`
- `scePadVrControllerSetTriggerEffects ... table 0x... (+0xa730)`
- `scePadVrControllerSetTriggerEffect ... table -`

`dlsym-nid fail` and `dlsym-lib fail` are the expected result for all eight; the research predicts
it for the names on 13.60 and the two loader spellings are untested. If **either** `dlsym` side
prints an address equal to `table`, say which function: that is the useful surprise, because
userland resolution by NID then works and v0.7's resolver shrinks to a few lines. Also send the
`headset vibration bound ... 13.60 table` and `[gaze] bound at the 13.60 table ...` lines, and say
whether the motor, the Sense vibration and the triggers still feel as before (they must not change).

**What it decides:** all eight lines as above with both `dlsym` sides failing confirms Step 0's
premise and leaves the v0.7 helper resolver as planned. A `dlsym` address equal to `table` changes
that plan to a userland lookup. A table address or offset that differs from the list means the
anchor/table pairing for that export needs a look. The six added tables can only be verified by a
report from a 12.40, 11.60, 10.x, 8.40, 7.x or 5.50 console; the same eight lines from one of those
would be the evidence.

## 2026-11-20: a recenter puts the 2D panel on the seat's forward and takes the display time's pose; built, untested on console

The RC's six holds (boot.log 555, 565, 1292, 1298, 1311, 1318; `[menu] Options up after 1172-1434 ms`) each
recentered; the report's loading-screen panel then ended up a few degrees off to one side. Three things were
wrong, and they are separate.

**Where the panel is.** BetterVR's `Layer2D::FinishRendering` (`renderer.cpp:919`) builds one quad:

- `p = ½(p_L + p_R)`, `q = slerp(q_L, q_R)` from `GetPoses(frameIdx)`, the views the *HUD frame* was latched
  with (a frame or two behind the display, and in the seated world of that moment);
- while a full screen is up (`isScreenShown`) the direction is `m_lockedGazeForward`, latched on the first
  such frame from `GetHorizontalGazeDirection(q)` and then **fixed in world space**: the position is
  `p + 1.85 m (hudDistance) × f`, so the panel is re-laid each frame but its heading never follows the head;
- the layer's `.space` is `m_stageSpace`, i.e. the runtime's display frame `D = seat ∘ tracker`. The runtime
  composes `quadPose = SpacePose(STAGE) ∘ q_app`, and `SpacePose` of STAGE is the reference space's own
  offset (a y floor shift): **the seat does not reach the quad**, by design, because the app authored the
  pose in `D`. `p` is the eye midpoint, so the panel is measured from the head's centre, not from an eye.

So the panel is a world-locked object in `D`: a recenter turns the head and the game camera inside that
world, and the panel does not move. Write `h` for the head's heading at the moment the screen appeared and
`h_head` for its heading now: the panel stands `h − h_head` from the head before a recenter and `h` right
after one, never 0. The recenter moves the head's forward, not the panel, so the panel keeps the heading
the player had when the screen appeared (a few degrees when they were looking near straight ahead, tens if
not): that is the "a little to the left". An in-game HUD does not hold it: that branch of
`FinishRendering` re-lays the panel from the head's position every frame, so only a locked full screen
keeps the error. The display time's pose matters too, but less: the seat was latched from
`hmd2::predicted_pose(Now())`, the CPU clock, about 28 ms behind the display (0.5-1.1° at 20-40°/s), and
only at the 1 s mark, while the release came 170-430 ms later (3-17° more if the head kept turning).

**baseYaw is not it.** `Camera::BuildEyePose` (`camera.cpp:376`) puts the game camera's `baseYaw` on the
located head pose to build *the game camera*; the panel never passes through it, and the yaw is constant
through a recenter (first person keeps the last adopted game yaw, `ShouldAdoptGameCameraYaw`), so it cannot
shift the panel. The panel only has to be in front of the *head*, and the seated head's forward is -z.

**The yaw extraction was wrong off the level.** `GetHorizontalGazeDirection` took the quaternion's `w` and
`y` alone; that is the yaw only while the head is level, because a pitched and rolled head puts part of the
turn into `y` (30° down and 10° of roll is 2.7° of false yaw). The runtime's own seat yaw, taken from the
rotated forward vector, was already exact at any pitch and roll. The two therefore disagreed by that leak
whenever a screen opened with the head tilted.

**The fix.** (1) A hold only *asks* for a recenter: `xrWaitFrame` makes the seat from
`hmd2::predicted_pose(predictedDisplayTime)` before the app locates the eyes of that frame, so the pose is
the one the frame is displayed with (`ApplyRecenter`). (2) `BetterVR_PS5_RecenterGeneration` counts seats;
patch bettervr 0030 makes `Layer2D` notice, drop the old gaze lock, place the panel from *this* frame's
views (both in `FinishRendering` and in the re-shown-frame path, which re-used the last quads verbatim) and
lock the gaze anew. (3) `GetHorizontalGazeDirection` flattens `q · (0,0,-1)` instead of taking `w` and `y`.
The panel now lands on the seat's own yaw by construction: the seat and the panel are both built from the
display time's head, one pose, so the residual is 0, not "small".

One line a recenter, from `xrEndFrame`, when it can say where the panel went:
`[recenter] right Sense: head yaw 8.6 -> 0.0 deg, the 2D panel is 0.0 deg right of it; the seat moved -0.05 -0.09 m (the head was at 0.04 0.96 0.10)`.
`head yaw` is the tracker head's heading before the seat (positive: to the right), the second number is the
seated head's heading of the frame (the seat's forward is -z, so it reads 0), and the panel's number is the
panel centre's heading from the head's centre in the same frame (0: straight ahead; in the old build it was
`h`, the heading the screen appeared at, because the panel stayed where the seat did not reach).
`port/bettervr/xr/runtime.cpp` and bettervr patch 0030; no Cemu change, no shader change.

**Test request 63 (v0.6 recenter and the 2D panel, 13.60 console).** Install the `BotW-VR-v...zip` from this
head's green "BetterVR build" run and start Breath of the Wild in VR. Use the right Sense's Options or the
DualSense's, whichever you reported.

R1. **A loading screen (the report's case).** Trigger a load with a full-screen 2D picture (fast travel, or
enter a shrine and let the load screen show). While the loading picture is up, turn your head about 15° to
the right, hold Options for about 1.5 s, and release only after the view has moved. Is the panel straight
ahead the instant the view recenters, and does it stay there while the loader is still up? Repeat looking
about 15° to the left.

R2. **Head tilted.** On a second loading screen, look about 30° down and tilt your head about 15° to one
side, hold Options for 1.5 s, then straighten up. Is the panel straight ahead of the *straightened* head
(it is world-locked, so it should be where the recenter put it, not where you look next)? Repeat looking
about 30° up.

R3. **Menus.** Open the inventory and the map, do R1's turn-and-hold in each, and close them. Is the menu
panel straight ahead after each recenter, and does it stay put when you turn your head afterwards (it
should: it is a screen in the room)?

R4. **Play.** Play 60 s in the open world, recenter once mid-turn while walking, and say whether the HUD and
the view still behave as in the RC.

R5. **Send** `LC_ALL=C grep -a "\[recenter\]" boot.log`, then `LC_ALL=C grep -a "\[menu\] Options" boot.log`,
and the `Seated height calibrated` line if it appears.

**Expected if it works:** one `[recenter]` line a hold, in the shape above, at the moment the view moved.
`head yaw B -> A` with `A` within 0.5° of 0 (that is the seat landing on the display time's pose), and `the
2D panel is P deg right of it` with `|P|` at most 1° in R1-R3, at both signs of turn. `P` is what the fix is
for: it is 0 when the panel is on the head's forward, and in this build's shape it is the panel's heading in
the frame the seat just made.

**What it decides:** `P` at 0 with the panel straight in R1-R3 closes the report. `P` at 0 but the panel
still off to one side means the picture on the headset is not that quad (a mono-projection frame drawn
through the flag-16 path, not the 2D layer) and the next step is the compose's mono path, not the panel.
A `P` that is not 0 means the panel is still locked to the world (a build without patch 0030, or a frame
placed elsewhere than `Layer2D`), and that is the next step. A panel that is right at the recenter and then
drifts is `hudDistance`/`DoesUIFollowGaze`, not this.

## 2026-11-20: a mono or cutscene frame is the 2D quad alone, never an eye picture across both eyes; built, untested on console

The RC's rune pickup (`Demo003_0`, `boot.log` 273.7-310.8) and the 60 fps stretch from 274 to 607 s are
mono/cutscene frames: `[draws] ... multiview 0.0, record 0.00 ms` (`boot.log` 303.458, 588.759) says the
game draws one picture, not a stereo pair. The `[vr-xr]` frame line's `kept` count stops at 233 by 308.395
and never moves again, and the one `render pose kept for a re-shown pair` line is at 300.119: BetterVR's
submit thread re-shows the last pair, and from that frame the located ring no longer holds the pose the pair
was rendered with. The runtime kept that pose and composed the pair into both eyes anyway: a picture more
than a second old, stretched across both eyes, with Hmd2 reprojecting it from where it was rendered. That is
the frozen background; the rune panel (BetterVR's 2D quad) is the only layer that drew correctly, and the
report says a new map area shows the same thing.

Why `monoQuad` was off (a412839): 1.0.0 dropped the 0.9.23 mod's presentation switches (`monoQuad`,
`menuLock`, the proof counters), and the runtime carried the three that still mattered as fixed values. The
mono-on-quad path was left `false`: 1.0.0 presents a mono frame as its own 2D layer, a quad with the picture
in it, not as the 0.9.23 shared-pose mono projection the flag-16 path was written for, so the path had no
frame of that shape to run on and a mono projection was copied across each eye instead.

The compositor now has two modes, and a switch between them logs one line. **Stereo** is both eyes'
pictures with the HUD quad over them, unchanged: a fresh pair is found in the located ring as before, so
normal play, menus and the HUD frame are the same. **Mono** is the 2D picture alone in front of the player,
black around it: a mono projection (identical pose and FOV in both views) rides the HUD quad through flag
16; a re-shown pair the ring no longer holds has its eye pictures left out, and the frame's own quad is
drawn instead; a frame with no projection keeps its quad alone. In mono the quad is placed with the live
eyes and Hmd2 is given the live head for the compose, so the picture sits still in the room instead of being
reprojected from an old pose. A re-shown pair with no usable quad keeps the old kept-pose path.
`port/bettervr/xr/runtime.cpp` only; no Cemu or BetterVR change, no shader change.

Logs: `[vr-xr] compose mode stereo (frame N): both eyes' pictures with the HUD quad over them`, and
`[vr-xr] compose mode mono (frame N): <reason>; the 2D picture alone in front of the player, black outside
it`, where the reason is `one mono picture in both views, on the HUD quad`, `a re-shown pair the located ring
no longer holds`, or `no projection layer or eye picture this frame`. `render pose kept` and `kept N` are
still there.

**Test request 62 (v0.6 mono and cutscene frames, 13.60 console).** Install the `BotW-VR-v...zip` from this
head's green "BetterVR build" run and start Breath of the Wild in VR.

M1. **Normal play, unchanged.** Play about a minute in the open world with the HUD up. Does the picture
have depth and follow your head as before, and is the HUD where it was?

M2. **A new rune (the report's case).** Pick up a rune you do not have (a new game up to the first rune
pedestal, or any rune pedestal your save still allows), and watch the whole cutscene without moving for the
first 3 seconds, then turn your head left and right and lean. Is the cutscene picture a screen in front of
you with black around it that stays put in the room when you turn, or is anything stretched or doubled
across the eyes? Is the "Rune extracted" panel readable?

M3. **A new map area.** Cross a region boundary on foot, or fast-travel to a region you have not entered,
so the area-name screen shows. Same questions as M2.

M4. **Menus and the map.** Open the inventory for 10 s, turn your head, then open the map. Anything
different from the RC?

M5. **Send** `LC_ALL=C grep -a "compose mode" boot.log`, then `LC_ALL=C grep -a "render pose kept\|\[draws\]"
boot.log`, the `LC_ALL=C grep -a "frames, .* fps submitted" boot.log` lines from M1-M3, plus the [bettervr]
`Event '...' started/ended` lines for M2 and M3. Say which mode line came at which step.

**Expected if it works:** `compose mode stereo` at VR start and through M1 and M4. At M2 and M3 the line
becomes `compose mode mono ...` with `a re-shown pair the located ring no longer holds` (or `one mono picture
in both views, on the HUD quad`), stays for the length of the screen and becomes `compose mode stereo` when
play resumes: one line each way, not a flicker. The headset shows the 2D picture on a screen in the room in
M2 and M3, and the `multiview 0.0` windows line up with the mono lines.

**What it decides:** a mono line and a steady screen in M2/M3 fixes the RC's frozen background. A stereo
line through the whole cutscene means the frames arrive as a fresh projection rather than a re-shown pair,
and the next step is BetterVR's side of the mono capture (a numbered patch), not the compose. A screen that
flickers between the quad and the eye pictures is a mode switching per frame: the line count says which
frames, and the mode needs its hold lengthened.

## 2026-11-20: the headset's motor takes a speed in Hz and Damage Taken kicks it; built, untested on console

The RC logs gave the headset 12, 11, 10, ... 7 from a hit and nothing was felt, while the page's 25-for-400 ms test
is. The value is not an amplitude: it is the motor's speed in Hz (10-25; 1-9 run at 10, 0 stops), as PSVR2Toolkit
documents for the same motor. A 12-to-7 Hz decay is about 23 % to 8 % of the force of 25 Hz, so the old linear
`a x 25` made every hit weak and its falling envelope made it weaker still.

Every level the runtime sends now goes through one mapping, `25 * sqrt(a)`, off below a = 0.08: the mixer's sink,
the Damage Taken pulse, and anything else that reaches the motor. The Vibration page's Headset test stays at 25 Hz,
the speed already felt. Damage Taken adds `BetterVR_PS5_HeadsetPulse`: a 60 ms kick at 25 Hz, then the hit's mapped
level held for 300 ms, scaled by the Damage Taken row and the Headset Limit (100 % strength at the default 60 % cap:
a = 0.48 -> 17 Hz after the kick). The hands keep their 0.4 s falling pattern; the pulse only changes the headset. The
damage line now says `headset pulse 17 Hz for 300 ms`, and the runtime adds `[vib] headset pulse 17 Hz for 300 ms
(kick 25 Hz for 60 ms, strength 0.48)`.

Two diagnostics for the next console run, read-only and capped: the report's life-field scan (a 40-frame copy of the
player actor, one probe 300 ms after each damage or death state, intersecting the 4-byte s32 offsets that fell, then
watching the one field it finds) and the explosive-actor log (`Bomb`, `Barrel`, `Explo`, `Chuchu`, `Beam`; one
`explosive seen` line per name, `explosive gone` with age and last distance within 40 m). They print offsets, values,
distances and times only, never game code or instruction words. The V208 store decode is not in this build. New patch
bettervr 0029; no Cemu change.

**Test request 61 (v0.6 vibration: the Hz model, the held damage pulse, the life scan, explosions; 13.60 console).**
Install the build and start Breath of the Wild in VR with `HapticsLogPatterns` on. Keep Damage Taken at 100 %, Hands
and Headset ticked, Headset Limit 60 %.

V1. **The motor's speed.**
1. Open BetterVR's menu (hold left Square on Sense) > Vibration > Test and press **Headset** six times, about 2 s
   apart. All six are now 25 Hz for 400 ms; say whether each was felt and whether they felt equal.
2. Set Headset Limit to 100 %, take one weak enemy hit in a quiet spot, then set it to 25 % and take another. Say
   which was stronger and whether the 25 Hz test is clearly stronger than both. Restore 60 %.
3. Send `LC_ALL=C grep -a "\[vib\] test headset\|\[vib\] headset pulse\|hmd2: headset" boot.log`.

V2. **Damage.**
1. Take three hits from a weak enemy, one non-lethal fall of about 15 m without the paraglider, stand in burning
   grass for about 3 s, and let Link die once.
2. For each event say whether the hands and the headset were felt and how strong (weak / fine / too strong). Say
   whether the headset is held for the hit rather than fading at once.
3. Send `LC_ALL=C grep -a "\[vib\] damage\|\[vib\] headset pulse\|hmd2: headset" boot.log`.

V3. **Life field.** No extra steps: V2's hits feed it.
1. Send every `[vib] life scan` and `[vib] life field` line. If the scan stops, send its `no field left ... scans`
   line; that is expected when Link's life is not stored inline, and the store-site fallback is not in this build.
2. After the hits, eat one meal and say whether a `[vib] life field: player+0x... = N quarter hearts` line rises.

V4. **Explosions (diagnostic).**
1. With the Remote Bomb rune, detonate a round bomb about 3 m from Link (shield up or behind cover), then about 10 m
   and about 25 m; repeat with the cube bomb.
2. If available, shoot one bomb arrow at a wall about 10 m away, and ignite an explosive barrel from about 10 m.
3. Write down the order you did them in.
4. Send `LC_ALL=C grep -a "\[vib\] explosive" boot.log`.

**Expected if it works:** V1's six test presses all feel the same; the 100 % hit (about 22 Hz) is clearly stronger
than the 25 % one (about 11 Hz); V2's headset is felt on every logged hit, held for about 0.3 s; V3 prints candidate
counts that fall to one field after two hits, or the `no field left` line; V4's gone lines name the actors with an age
and a distance in the 3-25 m range. The full list of `[vib]` lines is in `UPDATE-1.0.md`, "Vibration: one page and one
mixer".

## 2026-11-20: the gaze probe is bound on 13.60 and logs only, and the throw assist does nothing without a real sample; built, untested on console

The RC session's 24 `[Throw] gaze assist bent 15.0 degrees toward the head` lines came from patch bettervr
0010: when no eye-gaze sample existed, the assist fell back to the head's forward direction. A leftover
`EyeGazeThrowAssist: true` in `bettervr.json` therefore bent every throw by the full 15 degrees even though
the row was hidden and the player could not turn it off. The assist now returns the velocity unchanged
unless `Tracking::EyeGaze()` gives a valid sample, and the target line starts at the hand that throws (the
left hand's position when only the left hand holds the item). The row stays hidden and the setting stays
default off; a leftover on setting now has no effect. Patch bettervr 0028.

The gaze probe now uses the table that already works for the headset motor. `sceHmd2GazeGetResult` and
`sceHmd2InternalGazeStart` / `Stop` are bound at PS5VR's 13.60 offsets (`+0x35510`, `+0x31b60`,
`+0x31b80`) behind `kAnchor_libSceHmd2_1360`, exact and never scanned; the four names with no public
offset are gone. After `sceHmd2Open` the stream starts once, `begin_frame()` polls one `0x38` result, and
a sample is valid only when the call returned 0, `+4` is 0, `+0x10` is 1 and the `+0x14` direction is
finite and unit length. Each log line carries the raw `0x38` bytes plus the parsed direction, validity and
status, about twice a second for the first 60 s of VR, then every 10 s, with one measured new-`+8`-sample
rate line after that minute. Other firmware logs one `hmd2: [gaze] unavailable on this firmware (no
verified libSceHmd2 table)` line and does nothing else. The runtime no longer flips the direction
(`{-x, y, -z}` is gone; the firmware's frame is `-z` forward) and does not offer
`XR_EXT_eye_gaze_interaction`: the probe is logging only, so no sample reaches gameplay, BetterVR logs
`Supports Eye Tracking: No`, and the Gaze Throw Assist row stays hidden. No Cemu change. Details:
`UPDATE-1.0.md`, "Eye tracking and the throw assist".

**Test request 60 (the gaze probe, the report's request 59, 13.60 console).**
1. On the PS5, open Settings > Accessories > PS VR2. Turn eye tracking on, then run **Adjust Eye Tracking**
   once while wearing the headset.
2. Install the build, put the headset on **before** starting the game, then start Breath of the Wild in VR.
   In BetterVR's Controls > Motion Controls, **Gaze Throw Assist** must not be shown.
3. When the headset picture appears, keep the head still and do all of this in the first 60 s: look
   **straight ahead 5 s**, **far right 5 s**, **far left 5 s**, **up 5 s**, **down 5 s**, then **close your
   eyes 3 s** and open them.
4. Play about a minute.
5. Quit to the home screen. Send `/data/ps5cemu/logs/boot.log` (and `boot.1.log` if the game was started
   twice), or paste:
   - every `hmd2: [gaze] ...` line: `bound at the 13.60 table ...`, `InternalGazeStart -> 0x...`, the
     `0x38 rc 0x...:` hex lines with their `dir ... valid ... status ...` fields, the `sample rate ...`
     line, and `InternalGazeStop -> 0x...`;
   - `gaze: XR_EXT_eye_gaze_interaction not offered: this build logs the gaze probe only` (the line may end with
     `(the application did not chain the system property)`);
   - BetterVR's ` - Supports Eye Tracking: No` line;
   - any `[Throw] gaze assist ...` line, which must be absent.
6. Say whether the game froze or crashed at VR start, and whether the PS5 eye-tracking setting was on and
   the adjustment completed.

**Expected if it works:** straight ahead gives `dir 0.0000 0.0000 -1.0000 valid 1`; right gives `x > 0`;
left gives `x < 0`; up gives `y > 0`; down gives `y < 0`; closed eyes gives `valid 0` or `status != 0`. The
`sample rate` line is the headset's measured rate. If right gives `x < 0`, only the x sign needs flipping,
and the hex lines show it.

## 2026-11-20: v0.6 triggers (the bow on feedback mode 1, one Sense path, a step test), built, untested on console

The 13.60 logs of RC `5fbe5c6` refused every bow R2 payload: `slope 0-9 1-8` (mode 5) came back
`0x80920001` (`SCE_PAD_ERROR_INVALID_ARG`) from the DualSense setter and from libScePad's Sense setter, every
time, while the shield's L2 `feedback 2 5` returned 0 on both. The bow is now feedback mode 1 at position 2,
its strength 1-8 rising with the draw, on the DualSense and the Sense controllers. The Sense path is now the
both-hands `0x78` call only: the one-hand `0x38` export was never bound (`0x80000001`), and its failure set the
Auto layout to -1, which stopped every later Sense effect. The format row, its `senseTriggerLayout` key and its
strings are gone, and a refused effect no longer affects the other device or a later call. The Vibration page's
Test row has a fourth button, **Triggers n/11**: each press sends the next of eleven fixed payloads (the report's
table: feedback, weapon, vibration, multi-position, both slope payloads and the off pair) to the DualSense and to
the Sense controllers, whatever the Sense Triggers row says, and logs one line a device. A step holds 15 s or
until the next press; the last step is all off. New patch bettervr 0027; no Cemu change. Details:
`UPDATE-1.0.md`, "Adaptive triggers".

**Test request 59 (v0.6 triggers, 13.60 console).**
1. On the PS5, open Settings > Accessories > Controllers (General) > **Trigger Effect Intensity** and write the
   value down. If it is Off, set Strong for this test.
2. Install the build, start BotW in VR with both Sense controllers charged and the DualSense connected. In
   BetterVR's menu > System, turn **Sense Triggers** on. The **Sense trigger format** row must be gone.
3. Open menu > Vibration > Test and press **Triggers** once per step, 1 to 11 (the button reads `Triggers N/11`).
   At each step slowly squeeze each Sense trigger fully, left then right, then the DualSense's L2 and R2. For every
   step write: left felt? right felt? and what (a stop at about 20 %, stiff all the way, a click, a buzz, nothing).
4. Step 3 holds for 15 s: squeeze once, press the **Sense Controllers** rumble test, wait for it to stop, then
   squeeze again. Say whether the resistance is still there.
5. Close the menu. Equip a bow and draw slowly three times while holding R2: the right trigger must push back
   harder as the draw grows, and go slack after the shot or when a draw is cancelled.
6. With a one-handed weapon and a shield, raise the shield to your chest, then pull L2: a stop at about 20 %.
   Lower the shield and pull again: it must be free.
7. Send the felt notes from steps 3-6, the step 1 setting, and every line of `/data/ps5cemu/logs/boot.log` that
   contains `[trig]` or `trigger effects`. In particular: `hmd2: Sense trigger effects bound (both hands ...)`,
   the eleven `[trig test] step N/11 ... rc=0x...` pairs (one DualSense line and one Sense line a step), the
   `[trig] Sense both hands (0x78) L2 ..., R2 ... -> 0x...` lines from the bow and the shield, and `Sense off`.

**What the answers decide:** step 3 felt on the right proves the bow fix; steps 7-9 at `0x80920001` while 1-5
are 0 means modes 4-6 are refused for this title (keep 1-3, do not change `--module-sdk`); step 2 or 3 refused
means a value-range rule (lower the bow's cap); step 4 lost after the rumble means the effect must be re-sent
after a Sense vibration (a v0.6 follow-up, not in this build); no `[trig test]` line at all means the button or
the log path needs a fix, so send the `[vib]` lines of the same run too.

## 2026-11-19: v0.6 shadows (High 200% by default, and the cascades snap to whole texels), built, untested on console

A fresh launcher starts the Graphics pack's Shadows preset on **High (200%)** (`$shadowRes = 2`): the near cascade's
768x720 map becomes 1536x1440 and its 2x2 PCF taps cover 4 times the texels, so shadow edges lose their stair steps.
A saved choice wins, and the preset names are unchanged, so Low, Medium, Ultra and Extreme apply as before. The game
fits the cascades to the record camera every frame; with head movement the fit slid by a fraction of a texel a frame,
so the 2x2 taps landed on different texels and the dapple re-formed. Two native hooks now round the fit's x/y
translation to whole shadow-map texels before the game stores it: the `DepthShadowProj` lookup matrices (four cascade
slots) and the `ShadowPrePass` render matrices (the three the game uploads in its one call). The fourth lookup's
render fit is not written through that call, and the pack's fifth 360x360 map never gets a cascade index. The texel
size comes from the active Shadows preset and is frozen at the first frame, so a preset saved for the next start
cannot change this run's grid. One boot line: `Shadow cascades: 1-texel snap on, Shadows "High (200%)" (2.00x)`.
New patches bettervr 0021 (High default) and 0022 (the snap); no Cemu change. The shadow investigation's item 3, the
in-world NPC dialogue panel's own shadows, is deliberately **not** in this build: it is deferred to v0.7.

**Test request 58 (shadows, 13.60 console).** With a fresh launcher (or Shadows saved as High), play 60 s on Hyrule
Field and near the tower's lattice, then set Shadows to Medium and repeat after a restart. At High the near shadow
edges must be visibly sharper than at Medium; with slow head movement the lattice dapples must step by whole texels
instead of re-forming every frame, and no shadow may jump as a block. Walk between a village, grass and the plateau's
ruins and say whether any cascade seam or new shadow acne appears at 200%. Send the
`Shadow cascades: 1-texel snap on, Shadows "..." (N.NNx)` line from each start (it must name the preset and its
scale), the `Graphics pack presets:` line (Shadows must read `High (200%)` on a fresh launcher), and the `[draws]` and
`[perf]` lines from two minutes at each setting; say whether the frame time moved outside its usual spread.

## 2026-11-19: v0.6 compose follow-up (1x eyes copied, a HUD-only dispatch, the last barrier without a host read), built, untested on console

At 1x the compose no longer runs its 8.13 Mpx compute pass over every eye pixel: each eye's swapchain image is the
eye's own 1984x2048 with its whole rect, so the runtime copies both halves into Hmd2's side-by-side eye buffer
(`vkCmdCopyImageToBuffer`, the buffer's rows already 3968 pixels wide) and dispatches only the HUD quad's box (the
eye rect clipped by the quad's homography, a pixel or two wider; a normal frame's two boxes are about 0.8 Mpx). The
mono picture on the quad, a higher VR Resolution and an image that is not R8G8B8A8 keep the previous full dispatch.
The 3D present now writes the eye pictures opaque (patch bettervr 0023), so the copied alpha is 255, exactly what the
shader wrote before, and the copy is byte for byte the old output; the flat screen's quad keeps its source alpha,
which it blends by. The compose buffer's last barrier no longer asks for a host read: Hmd2 samples the buffer on the
GPU and nothing on the host reads it. The `[draws]` line's compose time is the same fence wait as before. One
`[vr-xr] the 1x compose copies each eye into hmd2's buffer and dispatches only the HUD's box (N and N pixels this
frame)` line says when the path first runs. New patch bettervr 0023; no Cemu change. Details: `UPDATE-1.0.md`,
"Performance: packets, profile and VR Resolution".

**Test request 57 (the 1x compose, 13.60 console).** At 1x, play 60 s in a village and 60 s on Hyrule Field, then
open and close the pause menu, the map and the scope; look at the HUD's edges (minimap, hearts, the clock) and the
weapon wheel's dimmed arrows against the previous build, then set 1.5x and repeat. The picture must be the same at
every step: no offset, no stretch, no missing HUD, the same HUD blending. Send every `[draws]` line, the
`the 1x compose copies ...` line, and `Graphics pack presets:` (1x must read `1984x2048`). Say whether the 1x
compose time in `[draws]` fell, and whether the HUD moved or faded differently at either step.

## 2026-11-19: v0.6 recenter (Options held 1 s, and the menu on Square or a touchpad tap), built, untested on console

Options on the right Sense and on the DualSense is now two things: held for about a second it recenters the
view as a PS VR2 game does, and released sooner it is the game's "+" (pause). The recenter lives in the
runtime (`port/bettervr/xr/runtime.cpp`): one seat pose, a yaw and an x/z offset, latched from the head's
own pose at the second and applied to the head and both hands in VIEW, LOCAL and STAGE, so BetterVR and the
game need no change. Hmd2 still reprojects from the tracker's own head pose, not the seat. The height is
kept: BetterVR's stage offset is a pure y shift for the seated eye height, so its seated calibration stays
valid. One `[recenter] <input>: yaw N deg, offset X Z m (the head was at ...)` line a hold. BetterVR's menu
no longer opens on Options: it opens on the left Sense's Square (unchanged) or on a DualSense touchpad click
+ Options tap, and that chord held for a second is the app's own in-game menu, as before (`port/ps5/pad.cpp`,
the app calls `BetterVR_PS5_ToggleModMenu`). New patch bettervr 0020; no Cemu change.

**Test request 56 (recenter and the new menu controls, 13.60 console).** In VR, stand facing away from the
room's forward and hold Options on the right Sense for about a second: the view must turn to face forward and
its horizontal position must reset under you, with no pause and no menu; a short tap must still open the
inventory. Repeat with the DualSense's Options, and again with both Sense controllers off. Say whether the
seated eye height stayed the same across each recenter. Then say how the menus open: the left Sense's Square
held opens BetterVR's menu; a DualSense touchpad click + Options tap opens it; that chord held for a second
opens the app's in-game menu; a short Options press never opens either. Send every `[recenter]` line, every
`[menu]` line and the `Seated height calibrated` line.

## 2026-11-19: a damage hit takes its hand from any other rumble and logs every event; built, untested on console

The ef87231 logs do hold damage events: the 13:00 process logged `[vib] damage state '小ダメージ'`
(0x101E0940) at 13:06:16 and `'中ダメージ'` (0x101E0930) twice at 12:59, and the 13:14 `boot.log` shows the
headset motor taking levels 12, 11, 9, 8, 7, 6 from that process's load-time large-damage call (a plain
`grep` calls the log binary and prints nothing, so `grep -a` is needed). What could swallow a hit was the
mixer (`rendering/rumble.h`): `updateHaptics` drops a request while its hand still plays a prioritized
rumble, and swings, guard and the weapon/gear releases all ask for that; the releases are `Falling` like the
hit, so the same-type check dropped it too. With both hands busy, nothing reached the headset. A Damage
request now replaces its hand. The `0x02CAB32C` call that runs once while a save loads is no longer played
as a hit. One `[vib] damage path=... state='...' (...) -> ...` line per event, under `HapticsLogPatterns`
like the game's pattern lines, names the path, the state and the hands and headset levels, or why nothing
played. New patch bettervr 0019; no Cemu change. Details: `UPDATE-1.0.md`, "Vibration: one page and one mixer".

**Test request 55 (Damage Taken, 13.60 console).** In VR with `HapticsLogPatterns` on, press the Vibration
page's **Headset** test once, alone, before any hit. Then take a Bokoblin hit, a hit from a fall of a few
hearts, and fire damage; then a large one (a Guardian beam, a long fall or a bomb at your feet). Each hit must
give one rumble on both hands and one on the headset, per the Damage Taken row (100 %, Hands and Headset
ticked): one `[vib] damage` line per hit must show `hands 0.80` and a non-zero `headset N/25`, and the large
hit must say `path=PlayerLargeDamage` while the ordinary ones say `path=PlayerNormal` with a state hash. At
0 % (or Off) the same hits must stay still and the line must say `none, Damage Taken is off or at 0% in the
Vibration page`. Report whether a single hit rumbles twice, and send every `[vib] damage` line, the one
`[vib] damage path=PlayerLargeDamage ... before the first in-game frame (load)` line at start (the motor must
be still there), every `[vib] PlayerNormal state` line, and `[vib] test headset: ...`.

## 2026-11-19: v0.6 performance follow-up (900-draw batch, 1x 1984x2048 and a box compose, no 4K TV picture in stereo, `[draws]` line), built, untested on console

Cemu now submits a command buffer every 900 draws instead of 300 (the setting still overrides it). 1x asks the
Graphics pack for the headset's own 1984x2048 eye size, so the compose is 1:1, and a higher VR Resolution is
downsampled with a 4-tap box instead of one bilinear tap. BetterVR stereo no longer draws Cemu's 4K TV picture,
since Hmd2's social screen is the TV; the present pass no longer writes its full-screen depth. Every 5 s the runtime
logs `[vr-xr] [draws] per frame: multiview N, replay N, total N | record X ms | replay Y ms | compose Z ms | xr end
W ms`; Cemu's draw path counts and times the multiview/replay split. New patches: bettervr 0015-0016, cemu 0056-0057.
The compose fence wait still runs on the GPU thread; moving it off needs a worker, eye-buffer rotation and a deferred
Hmd2 submit.

**Test request 54 (v0.6 performance, 13.60 console).** In VR at 1x, play 60 s in a village and 60 s on Hyrule Field,
then set 1.5x and repeat; send the logs zip. Say whether the picture shimmers less at 1x, whether 1.5x is sharper,
and what the TV shows while VR runs. From `/data/ps5cemu/logs/boot.log`, send every `[draws]` line (it says how much
is still replayed), the `[frame]` lines, the `[perf]` fps lines, and `Graphics pack presets:` (1x must read
`1984x2048`).

## 2026-11-19: the DualSense trigger setter binds on 13.60 (link-time import under its NID), built, untested on console

The DualSense's adaptive triggers were bound with `sceKernelDlsym`, which matches libScePad's name
table; on 13.60 that lookup fails and the setter never reached the firmware. `scePadSetTriggerEffect`
is now imported at link time: the packaging tool turns the SDK stub's symbol into its NID
`2JgFB2n9oUM`, and the loader binds that from libScePad, the same NID AstroBotVR2 resolves at runtime.
The app already imports libScePad (pads, Sense reads), and the NID is exported from 5.50 through
13.60, so a missing symbol cannot stop the title loading; there is no name lookup and no table. The
bind logs one line, `hmd2: DualSense trigger effects bound by NID 2JgFB2n9oUM (link-time import, ...)`,
and the System page's **DualSense** row now reads **Available**. A refused effect is no longer marked
sent: it is retried 5 s later, and the trigger payload's command and parameter sizes and offsets are
static asserts in `port/bettervr/xr/triggers.h`. New patch: bettervr 0014 (the row). Test request 53(f)
below now expects the triggers to work and the row to read Available.

## 2026-11-19: v0.6 combined (Options hold, stall tolerance, performance, Vibration, triggers, eye tracking, third-person snap turn), built, untested on console

Branch `bettervr-1.0/v06-all`, from v0.5.0, combining the v0.6 tracks. Options on the right Sense and on the
DualSense recenters the view on a 1 s hold and is the game's "+" (pause) on a shorter press (reworked by the entry
above); the start popup
is gone. A camera stall shorter than 250 ms keeps first person, so strafing near grass no longer flashes Link's body,
and the compositor blends the HUD premultiplied, so the weapon wheel's arrows, the minimap and the clock keep their
alpha and the HUD stops shimmering (two mip levels, `textureGrad`, LOD capped at 1.0). The frame end does fewer
copies, accurate barriers are off by default like a PC VR run, `bettervr.json` `"profile": true` prints a `[frame]`
line every 10 s, and BetterVR's System page has a VR Resolution selector (0.5x to 1.5x). BetterVR's menu has a Vibration page (nine categories, a
master switch, a headset cap and a test button for each device) over one 60 Hz mixer that also reaches the PS VR2
headset's motor and the DualSense. The bow's draw firms R2 and the shield's guard clicks L2 on the DualSense (the
Sense controllers behind the System card's **Sense Triggers** row, saved as `senseTriggers`), and Damage Taken also
follows the large-damage state machine, once per hit. The PS VR2 gaze exports are probed, `XR_EXT_eye_gaze_interaction`
is exposed only where they bind, and an optional `Gaze Throw Assist` bends a throw up to 15 degrees toward where the
player looks. Bettervr 0011 keeps the last good first-person body root for a frame whose pose pass is skipped (the
root is posed from the live player matrix, as in 0.5.0) and gives the HUD background its own premultiplied ImGui
pipeline, holds the first 2D frame after a screen change, and
keeps a short press from opening the world guide in third person; the bow aim arc needs a real draw there. Bettervr
0012 adds an optional **Snap Turn (third person)** that reuses the first-person snap angle and threshold, and with it
off third person is unchanged. Bettervr 0013 logs the magnesis wrist twist (fixed flag, twist, requested angular
velocity, the body's own turn, the reach ratio) and, when the held body's angular velocity stays at zero, turns the
body's transform about its centre instead, capped at 6 rad/s; fixed objects are untouched. New patches: bettervr
0007-0013, cemu 0055. The PS5's notification window no longer comes up at game start: the Wii U start's touchpad +
Options hint is gone, and what the app posts while the launcher gives the screen up waits for the game's own VideoOut
surface. This build also removes the click heard exactly every half second in idle play: Cemu's 12 ms blocks and
AudioOut's 256-frame grains left grains a few frames short at the queue's 24 ms target, and `OutputThread` padded them
with silence. A grain is now played only when a whole one is queued, four blocks (48 ms) wait with room for eight, and
`PS5 AudioOut: dropped blocks N, padded grains M since the last line (queue K ms)` is written at most once in 10 s and
only when something happened. Settings are done for one game: the folder-per-system row and its startup offer are gone
(an install already in separate folders is still read), the Box art texts read for one game, and the mods help no longer
names `docs/GUIDE.md`. Details: `UPDATE-1.0.md`, "v0.6", "v0.6 body, HUD and needle", "Snap turn in third person" and
"Magnesis wrist twist".

**Test request 53 (v0.6 combined).** One console run of this build (13.60), in VR, with `bettervr.json`
`"profile": true` and the Sense controllers charged; send the logs zip and the lines named under each letter.

- (a) Options, Sense and DualSense. On the right Sense, tap Options once (the game's pause/inventory opens) and tap
  again straight away, well inside the next 400 ms (it closes); hold it about a second (the view recenters, with no
  pause and no menu; Test request 56 above has the detail). Repeat with the DualSense's Options, with the Sense
  controllers off and third person set in Settings > VR, and move through BetterVR's menu (left Square held, or a
  touchpad + Options tap) with the DualSense's left and right stick. When the Sense is connected but has no pose, the
  pad's sticks must not also move the cursor. With both inputs ready, hold one Options and tap the other before the
  hold reaches 1 s: the hold must recenter and the tap must reach the game. A tap after the menu is open is dropped on
  purpose (`short press ignored, game input is blocked`). Say whether any help card or popup still appears over the
  game at start. Send every
  `[menu]` line (`Options down`, `Options up after N ms`, `Options held for 1 s: the view recenters, no pause`,
  `release after the second: no pause`, `short press -> pause`, `short press ignored, game input is blocked`,
  `touchpad + Options tap -> open`/`closed`) and every `[recenter]` line.
- (b) Body facing and the strafe flash. Walk forward, back up and strafe left and right in first person, and look
  down: Link's body must face the way you look, not the opposite way (a 0.6.0 build turned it 180 degrees). Then
  strafe near grass and say whether the body still flashes for a frame. Send the `[pose] a short camera stall kept
  first person (N frames)` lines (at most one a second) and every `[pose] first-person body root ...` line (they name
  the skip: actor missing, model mismatch, hand pose inactive, bone missing); say whether the frame rate held.
- (c) Weapon wheel and HUD. Open the weapon wheel: the left and right arrows must be visible on the black buttons, and
  the minimap and clock must not look faded or black. Turn your head slowly while looking at the HUD and at menu text,
  menu open and closed: say whether the edges still crawl or shimmer, whether menu text is as sharp as v0.5.0 or still
  soft, and whether the TV mirror differs. Send the `HUD mip chain: ... 2 levels ...` line, the `[frame]` and `[perf]`
  lines from two minutes in the overworld at 1x, every `[capture] held the first 2D capture after a screen change ...`
  line, and the `[camera] first frame after a screen: source=...` lines from opening and closing the wheel; say whether
  the bright first wheel frame and the 2-frame world shift on closing are still there.
- (d) Vibration page. Open BetterVR's menu (left Square held, or a touchpad + Options tap) and open the new
  **Vibration** page between Controls and Display; say whether it
  draws (master switch, headset cap, three test buttons, nine category rows with On / Strength / Hands / Headset /
  DualSense). **Press Headset first, alone, before taking any damage**, so its test line comes before a hit: a Damage
  Taken or Explosions event can also call the motor (both default to the headset). It is never called at VR start. Then
  press Sense Controllers and DualSense and say which buzzed. Send `[vib] test headset: level 25 -> 0x...`, `[vib]
  test hands`, `[vib] test pad`, and the
  `hmd2: headset vibration bound ...` line (or `hmd2: sceHmd2SetVibration off on this firmware (no verified
  libSceHmd2 table), the headset stays still`).
- (e) Vibration categories. At 0 % (or Off) a sword swing, a weapon/gear equip, a bow draw, a shield raise and the
  slate's hum are each silent on their own; Game Rumble at 0 % silences the game's own rumble while BetterVR's rows
  still work; 30 % is clearly weaker than 100 %. Get hit (Bokoblin, fall, fire) at Damage Taken 100 %: both hands give
  one rumble; at 0 %, none. Get hit by a large blow (a fall, a Guardian beam, an explosion) and say whether the hands
  rumble once. With a category set to DualSense, one event is felt once, not twice, and the pad still works normally
  (no rumble) when VR is not running. Send every `[vib] PlayerNormal state '...' (0x...)` line (each state once, at most
  64), every `[vib] damage state '...'` line (up to 6; the large-damage path reads `PlayerLargeDamage`), every
  `[vib] game rumble ...` line (at most 40, 150 ms apart; each has `caller=`, `state=`, the VPAD `bytes=` and the sead
  `pattern="..." repeat=` when found), and the one `[pad] rumble: BetterVR's vibration mixer drives the DualSense`
  line at VR start. Say whether the run crashed or stuttered around them, and whether a single hit rumbled twice.
- (f) Triggers. With the DualSense, equip the bow, hold R2 and draw: R2 must firm up as the draw grows and release when
  the arrow flies. Guard with the shield on the left hand: L2 must click around a third of its pull while guarding,
  none after lowering it. Open BetterVR's menu and the game's pause screen: both must go soft at once; quit to the
  library: nothing may stay firm. In the System page, turn Adaptive Triggers off and on and change Strength: live and
  kept after a restart; the **DualSense** row must read **Available**, not Unavailable. Turn the new **Sense Triggers**
  row on: it must save to `/data/botwvr/launcher/bettervr.json` as `"senseTriggers": true`, and off must clear any
  effect the Sense controllers still hold. Repeat the bow and shield on the Sense controllers, then switch the **Sense
  trigger format** test row between Auto, 1 and 2 while the bow is held. Send every `[trig]` line: the
  `hmd2: DualSense trigger effects bound by NID 2JgFB2n9oUM (link-time import, ...)` line, the
  `DualSense (handle N) L2 ... R2 ... -> 0x...` calls, `Sense trigger effects on/off`, `Sense trigger format: ...`, the
  `Sense both hands (0x78 ...)` and/or `Sense one hand (0x38 ...)` results, `Sense payload ... accepted`, and the
  `DualSense off` / `Sense off` lines. Say which of formats 1 and 2 was felt, if either, and send the lines of a
  restart to show the row and format came back. If the setter refuses, send the first five `DualSense (handle N)`
  lines and say roughly how far apart they were (a refusal is retried no sooner than 5 s).
- (g) VR Resolution. BetterVR's menu > System > VR Resolution: choose 0.5x, then 1x, then 1.25x. Each choice must log
  `VR resolution preset: ...` and show the restart notice. After 0.5x, restart and send the `view configuration`,
  `Found rendering resolution`, `Graphics pack presets` and `VR Resolution Multiplier` lines: the eye size must be
  smaller; after 1x it must be back to 1984x2048.
- (h) Eye tracking and throw assist. In the PS5 settings turn on Settings > Accessories > PS VR2 > Eye Tracking and run
  Adjust Eye Tracking, then play in VR. On 13.60 the runtime must not offer the extension now: send `gaze:
  XR_EXT_eye_gaze_interaction not offered ...`, the one `[gaze] libSceHmd2 does not export ...` line, and say whether
  BetterVR logs `Supports Eye Tracking: No` and Controls > Motion Controls has no **Gaze Throw Assist** row. If the
  exports do bind on this firmware, send every `[gaze]` line instead: `five exports bound by name`,
  `ActivateGazeTracking(1)`, `device detected` or the device status line, `calibrated` or the calibration status line,
  and during the first 10 s `result 0x38`, `foveated 0x18`, `raw direction ... status full N foveated N`. With the row
  shown, turn Gaze Throw Assist on, then throw something while looking left of the head direction and again while
  looking right; say which way each throw went and send the `[Throw] gaze assist bent N degrees toward the gaze` lines
  (or `toward the head`). With the setting off, the same throw must not bend.
- (i) Touchpad click. With the DualSense in game, a click of the touchpad opens the Slate (`-`) and Create opens it as
  well; moving a finger on the touchpad still moves the GamePad's cursor; touchpad + Options let go sooner opens only
  BetterVR's menu, and the same chord held a second only the port's in-game menu, not the Slate. Start once with an
  older `/data/botwvr/launcher/ps5cemu.json` (no
  `touchpadClickMigrated`): `touchpadClick` must read `[2, 2, 2, 2]` afterwards. Set player 1's Touchpad click to
  Automatic in Settings, restart, and say it is still Automatic. Send the `[controls]` lines that name each player's
  mappings.
- (j) Settings and About. On the Wii U side open Settings and go through the ten pages, then About: there must be no
  PayPal or QR support card and no other donation link, About must have the one licence line naming Cemu, PS5-ECAM and
  Crementif's BetterVR (under the GNU GPL and MIT respectively) and the Wii U's own paths, and the pages must show no
  other system's games, sides or settings. Send a photo of About and say whether any page still reads as a multi-game
  launcher.
- (k) Blue needle in third person. Set third person in Settings > VR; on foot, tap Options once on the right Sense and
  once on the DualSense (each is only the game's pause) and look at the screen centre while Link stands still. Say
  whether the thin blue needle still appears, and whether it appears while drawing the bow in third person (it should
  only then). Send every `[guide] short press ignored on the PS5 ...` line, `[guide] world guide ribbon drawn in third
  person ...` if one appears, and `[bow] bow aim arc drawn in third person (draw=..., engaged=...)`.
- (l) Third-person snap turn. Still in third person, set Comfort > Advanced > Turning to Snap 45 degrees, then open
  Play Style > Third-Person Camera and turn **Snap Turn (third person)** on. With the DualSense's right stick, push
  right once: the view must step 45 degrees right, not drift, and holding the stick must not turn further until it is
  released past the deadzone and pushed again; push left once: 45 degrees left. Repeat with a Sense stick and say
  whether pad and Sense behave the same. Turn the setting off: the same push must turn smoothly as before. Say whether
  the row is absent in first person and whether it is still on after quitting and restarting. Send every
  `SnapTurn: turning N degrees in third person` line (one per step; N is ±30, ±45 or ±60, and its sign is the step's
  direction) and say whether each step went
  the way the stick was pushed; if a step turns twice, is missed or goes the wrong way, say which stick and which
  direction.
- (m) Magnesis wrist twist. Take the Magnesis rune, hold a free metal crate with the right grip held, and turn the
  right hand's wrist left and right as if unscrewing it, first slowly, then quickly; the crate must spin with the wrist
  instead of sliding away. Repeat with a sliding block or a door (a fixed object): it must not spin. Send every
  `[magnesis] spin` line, at most two a second (`fixed=`, `twist=`, `want=`, `body=`, `reach=`), and the one
  `[magnesis] spin path: ...` line of each hold: `angular velocity (...)` means the game kept the spin, `transform
  (...)` means it had dropped it and this build turned the body itself. Say whether the crate spun both ways, whether
  its centre stayed put or slid away, and send the `reach=` values from standing still and from the middle of a twist.
- (n) Notification window at start. From the library, start BotW fresh in VR: the PS5's notification window must not
  appear at all (no panel with the console's older messages over the black screen), and the "BotW VR: Touchpad click +
  Options: the in-game menu ..." hint must never show (the in-game menu is the touchpad + Options hold, and
  BetterVR's is the left Square's). If the console has
  notifications for the account on, a toast must come only once the game's picture is up. Quit to the library and start
  the game again: the same. Send, for each start, the `[ui] gfx: stopped` or `[ui] the new launcher is gone ...` line
  and the `[vulkan] VideoOut surface WxH at N Hz` line that follows it, plus any `[cheevos]` line of that start; say
  whether the window appeared at all and what it showed.
- (o) Firmware gates. On this 13.60 run send every `hmd2:` line that starts with `Sense vibration`, `Sense trigger
  effects` or `sceHmd2SetVibration` and ends `off on this firmware`; each must be one line and VR, tracking, the game
  and the DualSense must keep working. If you can run a 12.40 console, the same build must not abort at the first
  Sense vibration call: the Sense rows may be off there, but the session must start and play.
- (p) The audio click. Stand still in the overworld with the game idle and listen for half a minute: the click that came
  exactly every half second (a short tick in the music and ambience) must be gone. Then walk, fight and open the
  inventory, and say whether any new click, pop or dropout appears. Send every `PS5 AudioOut:` line: `output thread
  priority`, the GamePad speaker's open line, and any `dropped blocks N, padded grains M ... (queue K ms)`. In steady
  play the dropped/padded line must not appear at all; if it does, send each line and what the game was doing.
- (q) Settings follow-ups. Settings > Game files must have no **A folder for each system** row, and a fresh start must
  not offer to move the folders; an install already in `/data/ps5cemu/cemu`, `azahar`, `melonds` and `eden` must still
  start and play (the boot log's `[reorganize]` lines, if any). Settings > Online: the Box art rows must read for one
  game (no "every game on this side"), and the mods help cards must not name `docs/GUIDE.md`. Send a photo of the Game
  files page and of the Box art sub-page.


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

## v0.7 test build

One build for the console test, from the v0.7 draws and gaze branches on top of v0.6.1; built, untested on console.

- **1.25x draw cuts.** On by default; no menu row. The index lists kept across frames are `bettervr.json`'s
  `indexCache` (leave the key out, or set it to 1); `"indexCache": 0` gives Cemu's own cache. The VR Resolution
  preset is BetterVR's menu > System > VR Resolution > 1.25x (restart the game). RADV threaded recording stays a
  `ps5cemu.json` experiment. Test request 72.
- **Eye Aim Assist.** BetterVR's menu > Controls > Motion Controls, first person only, drawn while the runtime offers
  a gaze pose. Five steps, Off by default to Full (`EyeAimAssist`). Test request 73.
- **Eye Magnesis.** BetterVR's menu > Controls > Motion Controls, first person only, under Eye Aim Assist. Off by
  default (`EyeMagnesis`). Test request 74.
- **Eye Rune Pick.** BetterVR's menu > Controls > Motion Controls, first person only, under Eye Magnesis. Off by
  default (`EyeRunePick`). Test request 74.
- **Respawn camera hold.** The shrine and open-world respawn demos hold the third-person camera and drop the 16:9
  fade panel. Test request 76.
