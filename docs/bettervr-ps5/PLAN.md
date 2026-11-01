# Plan: BotW-BetterVR on PS5 with PSVR2

Each milestone ends in a pre-release build that Patryk tests on his console, so a wrong turn
costs one milestone, not the whole project. Work on the next milestone may start before the
previous one is confirmed, but only up to the point where it would build on a guess about console
behaviour. Below that point, stop and write a test request.

Target: Patryk's PS5 on firmware 13.60 (the only firmware PS5XR is built for), PSVR2 with Sense
controllers, PS5-ECAM's homebrew setup (see `docs/HEN-SETUP.md`), and his own dump of Breath of the
Wild with update v208.

## M1: baseline build from this repo

- **Build:** run `make deps`, `make build` and `make release`, all unmodified, in a cloud session.
  Record how long each step takes.
- **Title ID:** if it is a contained change, give this fork its own title ID and app name (for
  example "PS5-ECAM VR"). That way it can sit next to an installed PS5-ECAM instead of replacing it.
  Check that the ID isn't one the PS5 homebrew scene already uses. Known ones: PS5-ECAM is PPSA99360,
  HLA-PSVR2 is PPSA99198, prospero-win is PPSA99995. If the change spreads too far, keep PPSA99360
  and say in the release notes that this build replaces PS5-ECAM.
- **Release:** publish `m1-baseline`.
- **Test request:**
  1. Install the build and play BotW flat.
  2. Measure: frame pacing off, FPS++ limit at 120 or higher, then Kakariko or Hateno village and a
     fight on Hyrule Field. Send the FPS and GPU-busy lines from `/data/ps5cemu/logs/boot.log`
     (see `docs/DRIVER-PERFORMANCE.md`).
  3. As a rough go/no-go, VR fps will be about half the uncapped flat fps in those spots.

## M2: flat picture in the headset through hmd2

Uses `third_party/hmd2/`, the PSVR2 library from Patryk's photo-splat-vr. It is tested on his
console with the Sense controllers and a DualSense. Read its README first. PS5XR
(`third_party/ps5xr/`) isn't needed for M2.

- **Build hmd2 in:** compile `third_party/hmd2/hmd2.cpp` into the app.
- **Set up the eye buffers:** on Cemu's own `VkDevice`, create three host-visible, host-coherent
  buffers of `2 * eye_w * eye_h * 4` bytes. Map them and pass them to `hmd2::init` as
  `Config::external`. `third_party/hmd2/reference/eye_buffers.*` shows how.
- **Show the picture:** each frame, `wait_slot` → `begin_frame` → `acquire`, copy and scale Cemu's
  final frame into both halves of the acquired buffer on Cemu's queue, wait for the fence, then
  `submit`. A centred, screen-sized picture is enough. Head tracking isn't needed yet.
- **Make it optional:** turn VR on with one setting or config switch, so flat play keeps working.
  Turn frame generation (MihawkGen) and ReShade off while VR is on.
- **Release:** publish `m2-hmd2-flat`.
- **Test request:** does the picture appear in the headset, and is it stable? Send the `hmd2` log
  lines and the Cemu log lines. Also check the TV picture.

## M3: BetterVR stereo with head tracking

Starting point: `patches/bettervr/0001-*.patch`. It is upstream BetterVR 0.9.23 plus the Linux fork's
native Vulkan/OpenXR backend.

- **Fetch BetterVR:** pin Crementif/BotW-BetterVR at tag 0.9.23 in `tools/deps.json`, apply
  `patches/bettervr/` the way the other dependencies' patches are applied, and build its sources
  into the app.
- **Connect to Cemu directly:** replace the `GetProcAddress`/`dlsym` lookups of
  `gameMeta_getTitleId`, `memory_getBase` and `osLib_registerHLEFunction` with direct calls. Register
  the 74 HLE hooks at startup when BotW boots.
- **Ship the graphic pack:** ship `resources/BreathOfTheWild_BetterVR` as a graphic pack where
  PS5-ECAM keeps graphic packs, and enable it together with FPS++ (BetterVR crashes without FPS++).
- **Replace the Vulkan layer:** the layer intercepts 8 device-level and 6 instance-level calls. On
  the PS5 there is no loader. Cemu gets every Vulkan entry point through `PS5_VulkanGetGlobalProc`
  in `port/cemu/VulkanPS5.cpp`, which makes it the natural place to hand out wrapped function
  pointers. Prefer that to patching each call in Cemu.
- **Choose how BetterVR reaches the headset.** BetterVR (and its Linux backend) is written against
  OpenXR, and hmd2 is not an OpenXR runtime. There are two options:
  - **A (preferred): a small OpenXR runtime over hmd2,** in `port/bettervr/xr/`. It implements only
    what BetterVR calls: instance, system, session with the Vulkan binding, swapchains,
    wait/begin/end frame with projection and quad layers, spaces and `xrLocateViews`, and actions
    with the Oculus Touch profile mapped to the Sense controllers and DualSense. This is what PS5XR
    is. It keeps BetterVR's code close to upstream, so it stays rebasable. If Patryk adds the PS5XR
    source to `third_party/ps5xr/`, compare it with yours or use it directly.
  - **B: call hmd2 directly** from BetterVR's renderer and controls. That's less code at first but
    touches many BetterVR files.

  Write down which you chose, and why, in `STATUS.md`.
- **Adapt the Linux backend:**
  - Make `XR_KHR_composition_layer_depth` optional (hmd2 and PS5XR have no depth layer).
  - Use the timespec time conversion.
  - Make the three-queue setup fit what RADV on the PS5 exposes; fall back to fewer queues.
  - Drop the desktop mirror, launcher script and Nix build.
  - Consider dropping the backend's own re-presentation thread, since PSVR2's reprojection (Hmd2)
    already fills frames up to 120 Hz.
- **Release:** publish `m3-stereo`.
- **Test request:** check the stereo picture, head tracking, scale and comfort. Send the measured
  frame rate and the log.

## M4: controllers, menus, performance

- **Controllers:** BetterVR binds `/interaction_profiles/oculus/touch_controller`. Map the Sense
  controllers to it (buttons and sticks through `scePadVrControllerReadState`, as in
  `third_party/hmd2/reference/pad_input.h`; poses through `hmd2::controller_pose`). Map the
  DualSense as a fallback.
- **Frame-rate locks:** add 40 and 60 fps locks with reprojection to 120 Hz, as RE7-PSVR2 does. Use
  `XR_FB_display_refresh_rate` for 90 or 120 Hz.
- **Performance options:** expose BetterVR's options for performance (shadows off, resolution per
  eye).
- **Release:** publish `m4-…` builds as needed.

## When to stop a session

Stop when any of these is true:
- Everything that doesn't need a console result is done, built, pushed and released, and `STATUS.md`
  lists the open test requests.
- You're blocked: a blocked download, or a decision Patryk has to make. Write the blocker in
  `STATUS.md`.

Don't polish past an open test request.
