# SPDX-License-Identifier: GPL-3.0-or-later
# BotW-BetterVR on PS5: what BetterVR's own src/CMakeLists.txt takes from the port while it is built into
# PS5-ECAM's Cemu (CEMU_PS5; patches/bettervr, tools/build-cemu.sh's CEMU_EXTERNAL_MODULES). Included
# there, after add_library(BetterVR ...), in place of platform_linux.cpp.
#
#   PlatformPS5.cpp  the platform functions, the log in the boot log, the generic atomic functions and
#                    the stand-ins the runtime still asks of the 0.9.23 mod
#   xr/runtime.cpp   the OpenXR runtime over hmd2, the one that answers the module's xr functions (the
#                    vendored loader is not built: dependencies/CMakeLists.txt)

set(PS5_BETTERVR_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PS5_HMD2 "${CEMU_PS5_PORT_DIR}/../third_party/hmd2")

target_sources(BetterVR PRIVATE
	"${PS5_BETTERVR_DIR}/PlatformPS5.cpp"
	"${PS5_BETTERVR_DIR}/xr/runtime.cpp"
)
# neither is BetterVR's code: no pch.h in front of them
set_source_files_properties(
	"${PS5_BETTERVR_DIR}/PlatformPS5.cpp"
	"${PS5_BETTERVR_DIR}/xr/runtime.cpp"
	PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)
target_include_directories(BetterVR PRIVATE "${PS5_HMD2}")
