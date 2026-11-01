# hmd2: PS VR2 output, poses and Sense controllers (from photo-splat-vr)

Copied from Patryk's own **Papyszoo/photo-splat-vr** at commit `eddd5b7` (v0.1.1, 2026-10-27),
`platform/ps5/hmd2/`. Licence: GPL-3.0-or-later, the same as this repo. The code it ports from
theghostonline's PS5VR and HLA-PSVR2 (PS5XR) is credited in `THIRD_PARTY_NOTICES.md` here. Keep
that file when the copy changes.

photo-splat-vr is a PSVR2 title for jailbroken PS5s (firmware 13.60 tested). Patryk reports it
tested and working on his console with the PS VR2 Sense controllers and with a DualSense.

## What's here

- **`hmd2.h`, `hmd2.cpp`, `hmd2_exports.h`:** the library.
  - Loads libSceHmd2, libSceVrTracker2 and libSceVrHand at runtime. It finds their functions with
    `sceKernelDlsym`, or with export tables for firmware 13.60 and 7.61.
  - Opens the VideoOut bus-32 port and registers the eye buffers.
  - Does the per-frame BeginFrame / predicted display time / SetParam / SubmitFlip sequence, with
    HLA-PSVR2's pacing and re-submit of abandoned frames.
  - **External mode** (`Config::external`): you hand it 3 linear RGBA8 side-by-side images, each a
    `vkMapMemory` pointer of a RADV host-visible buffer. This is the path a Vulkan app (Cemu) uses.
  - Gives you the head and per-eye predicted poses (`predicted_poses()`), the FOV tangents
    (`field_of_view()`) and the refresh rate.
  - Gives you the Sense controller poses through VrTracker2 (`register_controller`,
    `controller_pose`). A comment in photo-splat-vr's `vr_mode.cpp` still marks the controller pose
    path "UNVERIFIED on hardware", so ask Patryk before relying on it.
- **`Makefile`:** photo-splat-vr's build of `libhmd2.a`, for reference. PS5-ECAM's build has to
  compile `hmd2.cpp` its own way.
- **`reference/`:** how photo-splat-vr's viewer uses the library. Read these; don't build them.
  - `pad_input.h`: one button/stick/trigger table for the DualSense and the two Sense controllers.
    The button word is at +0x00 for both. A Sense controller's 0x68-byte
    `scePadVrControllerReadState` state has the stick at +0x04/+0x05 and the triggers at
    +0x08/+0x09.
  - `eye_buffers.*`: allocating, mapping and filling the three host-visible eye buffers.
- **The rest of the viewer** (`platform/ps5/viewer/vr_mode.cpp`: the frame loop, controller
  reading, recentring) is in photo-splat-vr. Clone it at `eddd5b7` when you need it. It's a public
  GitHub repo, so cloud sessions can clone it.

## RADV versions

photo-splat-vr links PS5_Vulkan `f410e67`'s Mesa. PS5-ECAM links PS5_Mesa `7b59ef27`, built with
PS5_Vulkan `fde9e37`. Both are Mihawk's RADV port. The external-buffer path depends on RADV mapping
direct memory at the same address for the CPU and the GPU, which PS5XR's code also relies on. If
the M2 picture is garbage or black, check this difference first.
