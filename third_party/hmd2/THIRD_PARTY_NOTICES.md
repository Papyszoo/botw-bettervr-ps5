## Ported into the PS5 payloads and titles (source in this repository)

The payload libraries, payloads and titles are GPL-3.0-or-later like this
repository. The notices below are the per-directory `platform/ps5/hmd2/NOTICE`
content, merged here.

### PS5VR

- Upstream: <https://github.com/theghostonline/PS5VR> (the clone used here is
  the `mtp-24` fork, <https://github.com/mtp-24/PS5VR>)
- License: GNU General Public License v3.0 or later
- Notice: Copyright (C) 2026 Husam Osman. PS5VR - Copyright (C) 2026 Husam
  Osman - is distributed under the GNU General Public License v3.0 or later
  (PS5VR's own `THIRD_PARTY_NOTICES.md`).
- Ported into `platform/ps5/hmd2/hmd2.cpp`:
  - `app/src/app_vr.c`: loading libSceHmd2 / libSceVrTracker2 / libSceVrHand
    through `sceSysmoduleLoadModule` + `sceKernelLoadStartModule`, resolving
    their functions by `sceKernelDlsym` or by this firmware's export offsets
    after the module-name and anchor-string check; the Hmd2 initialize / Open /
    FOV / tracker QueryMemory+Initialize+RegisterDevice / reprojection memory
    and Initialize sequence and its parameter blocks; the VideoOut bus-32 port
    (`sceVideoOutOpen(0xff, 32, 0, NULL)`), the buffer attribute, the type 0x10
    layer sets, `sceVideoOutRegisterBuffers2` with category 0x10,
    `sceVideoOutConfigureOutput(0x2000f)`,
    `sceHmd2ReprojectionSetRenderConfig(1)`, the per-frame BeginFrame /
    GetPredictedDisplayTime / SetParam / `sceVideoOutSubmitFlip` sequence and
    the layer layout it fills, the 60 Hz frame cadence, the headset-worn poll
    through `sceHmd2GetDeviceInformation`, and the shutdown order
    (DisableVrMode, buffer unregister with the busy retry, Close, tracker
    UnregisterDevice/Finalize, reprojection Terminate, Hmd2 Close/Terminate).
  - `app/engine/media/src/eng_agc_writer.c`: the GFX10 2D texture-descriptor
    builder (`build_tsharp_2d_internal` and
    `eng_agc_build_tsharp_render_target`'s field placement and tiling bit).
    `hmd2.cpp` builds the descriptor for a B8G8R8A8 surface with the BGRA
    swizzle instead of an RGBA8 AGC target.
  - `app/src/vr_exports_1360.h`, `app/src/vr_exports_761.h`: the export offsets
    and anchor strings, now in `tools/export-tables/tables.json` and emitted as
    `platform/ps5/hmd2/hmd2_exports.h`. The same files for 12.40, 11.60, 10.00,
    8.40 and 5.50 (`vr_exports_1240.h`, `_1160.h`, `_1000.h`, `_840.h`,
    `_550.h`).

### Ghost-VR

- Upstream: <https://theghostonline.dev/theghostonline/Ghost-VR>
- License: GNU General Public License v3.0 or later
- Notice: Copyright (C) 2026 theghostonline. Ghost-VR is distributed under the
  GNU General Public License v3.0 or later.
- Used in `platform/ps5/hmd2/hmd2_exports.h` (via
  `tools/export-tables/tables.json`): `vr_exports.h`'s libSceHmd2 and
  libSceVrTracker2 offsets and anchor strings for firmware 9.00, which PS5VR
  has no table for. Its tables for the other firmware added here (12.40, 11.60,
  10.x, 8.40, 5.50) were compared with PS5VR's and agree.
- Modified for this project: the eye buffers are direct memory filled from the
  CPU, not AGC render targets, so the library needs no GPU API.

### HLA-PSVR2

- Upstream: <https://theghostonline.dev/theghostonline/HLA-PSVR2>
- License: GNU General Public License v3.0 or later
- Notice: Copyright (C) 2026 theghostonline. HLA-PSVR2 (the PS5XR runtime and
  the Half-Life: Alyx PS VR2 port) is distributed under the GNU General Public
  License v3.0 or later.
- Ported into `platform/ps5/hmd2/hmd2.cpp` and used by
  `platform/ps5/vr_hello/title.cpp`:
  - `ps5xr/runtime/src/psvr2.c` `tsharp_rgba8`: the linear RGBA8 texture
    descriptor (format `8_8_8_8_UNORM`, X,Y,Z,W swizzle, no tiling, pitch ==
    width) that makes a `vkMapMemory` pointer a sampleable eye texture.
  - `psvr2.c` `wait_display_live`: the three-vblank, 5 s gate after
    EnableVrMode, before the first flip.
  - `psvr2.c` `psvr2_begin_frame` and `xr_session.c` `x_WaitFrame`: the
    predicted-display-time pacing (nap to the next slot from the last
    BeginFrame, then to `display - period - 4160 us - 1500 us` when the app has
    slack), the four-deep begun-frame queue, and the re-submit of a frame two
    or more behind with the last image (`show_last`).
  - `psvr2.c` `submit_flip`: per-eye tangents as the headset's own field of
    view (no overscan) and the `SetParam` + `SubmitFlip` ordering.
  - `xr_vulkan.c` `vk_output_start`/`vk_compose`: three host-visible,
    host-coherent `VkBuffer`s of `2*eye_w*eye_h*4` bytes, mapped and handed to
    Hmd2 by address; one command buffer and fence per buffer, a bounded 500 ms
    fence wait before `SetParam`/`SubmitFlip`, and no CPU cache flush after the
    GPU write.
- Modified for this project: the buffers are filled by this project's own
  Vulkan pass instead of an OpenXR swapchain, and framed by `hmd2`'s API.

