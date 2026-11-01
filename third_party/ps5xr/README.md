# PS5XR (placeholder, optional)

Optional since 2026-11-01. The PSVR2 path now comes from `third_party/hmd2/` (Patryk's
photo-splat-vr), which is tested on his console. PS5XR is only worth adding as a reference for the
OpenXR layer in M3, or to use directly as that layer.

PS5XR is the OpenXR runtime for PlayStation VR2 from theghostonline's HLA-PSVR2 and RE7-PSVR2
(GPL-3.0). It drives the headset, tracking, Sense controllers and frame pacing through the PS5's VR
system libraries. Source: https://theghostonline.dev/theghostonline/HLA-PSVR2 (folder `ps5xr/`).

Cloud sessions can't download it (theghostonline.dev is blocked for git and curl), so it is copied
into this folder by hand.

To add it:

1. On the HLA-PSVR2 page, open `ps5xr/` and use "Download ZIP".
2. Unpack it here, so that `third_party/ps5xr/runtime/src/xr_vulkan.c` exists.
3. Add a line below with the HLA-PSVR2 commit it came from, then commit.

The files expected, as of 2026-11-01:
`runtime/src/{ps5xr_host.h, psvr2.c, psvr2.h, vr_exports_1360.h, xr_debug.c, xr_exports.c,
xr_input.c, xr_instance.c, xr_internal.h, xr_log.c, xr_log.h, xr_session.c, xr_vulkan.c}`,
`third_party/openxr/include/openxr/`, `tools/`.

Source commit: (not added yet)
