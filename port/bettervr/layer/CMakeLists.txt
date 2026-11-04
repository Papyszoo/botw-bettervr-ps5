# SPDX-License-Identifier: GPL-3.0-or-later
# BotW-BetterVR on PS5, M3: BetterVR (.deps/BotW-BetterVR with patches/bettervr), its own ImGui 1.91.9
# and ImPlot 0.16, and the app's OpenXR runtime over hmd2 (port/bettervr/xr), built with the PS5
# toolchain (tools/ps5.cmake) into one relocatable object, build/bettervr/bettervr.o
# (tools/build-bettervr.sh).
#
# Cemu links ImGui 1.89.5 of its own. In bettervr.o every symbol it defines is local except the
# ones exports.txt names (the Vulkan layer's negotiation entry point and the PS5 glue), so the two
# ImGuis, and anything else both have, never meet in the app's link. What bettervr.o needs from
# the app (Cemu's three exports, hmd2, ps5vk's vkGetInstanceProcAddr, the C and C++ libraries) stays
# undefined and is resolved there.

cmake_minimum_required(VERSION 3.25)
project(BetterVR_PS5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BVR "${PS5CEMU_ROOT}/.deps/BotW-BetterVR")
set(IMGUI "${PS5CEMU_ROOT}/.deps/imgui-1.91.9")
set(IMPLOT "${PS5CEMU_ROOT}/.deps/implot-0.16")
set(OPENXR "${PS5CEMU_ROOT}/.deps/OpenXR-SDK")
set(VULKAN_HEADERS "${PS5CEMU_ROOT}/.deps/Cemu/dependencies/Vulkan-Headers/include")
set(PORT "${PS5CEMU_ROOT}/port/bettervr")

set(BVR_SOURCES
	utils/logger.cpp utils/debug_draw.cpp
	hooking/framebuffer.cpp hooking/layer.cpp hooking/layer_active.cpp hooking/camera.cpp
	hooking/game_state.cpp hooking/settings.cpp hooking/profiler_hooks.cpp hooking/weapon.cpp
	hooking/imgui_menus.cpp hooking/controls.cpp hooking/entity_debugger.cpp hooking/rumble.cpp
	hooking/skeleton.cpp hooking/entity_controller.cpp hooking/bow.cpp
	rendering/renderer.cpp rendering/openxr.cpp rendering/swapchain.cpp rendering/texture.cpp
	rendering/vulkan.cpp rendering/vulkan_imgui.cpp
	rendering/linux_desktop_mirror.cpp rendering/renderer_vulkan.cpp
	hooking/framebuffer_linux_diag.cpp hooking/layer_linux.cpp)
list(TRANSFORM BVR_SOURCES PREPEND "${BVR}/src/")

add_library(bettervr_objects OBJECT
	${BVR_SOURCES}
	"${BVR}/dependencies/imgui_impl_vulkan.cpp"
	"${IMGUI}/imgui.cpp" "${IMGUI}/imgui_draw.cpp" "${IMGUI}/imgui_tables.cpp" "${IMGUI}/imgui_widgets.cpp"
	"${IMGUI}/imgui_demo.cpp"
	"${IMPLOT}/implot.cpp" "${IMPLOT}/implot_items.cpp" "${IMPLOT}/implot_demo.cpp"
	"${PORT}/xr/runtime.cpp")
target_include_directories(bettervr_objects PRIVATE
	"${BVR}/src" "${BVR}/include" "${BVR}/dependencies" "${IMGUI}" "${IMPLOT}"
	"${OPENXR}/include" "${VULKAN_HEADERS}" "${PS5CEMU_SYSROOT}/include" "${PS5CEMU_ROOT}/third_party/hmd2")
# ImGui opens links with fork and exec by default, which a PS5 title cannot
target_compile_definitions(bettervr_objects PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS BETTERVR_PS5=1 NDEBUG)
target_compile_options(bettervr_objects PRIVATE -O2 -Wno-everything)
# pch.h is force-included into every one of BetterVR's files, as its own build does; ImGui, ImPlot
# and the runtime are built without it
set_source_files_properties(${BVR_SOURCES} PROPERTIES COMPILE_OPTIONS "-include;${BVR}/include/pch.h")

# one object, with everything but exports.txt's names made local
set(BETTERVR_OBJECT "${CMAKE_BINARY_DIR}/bettervr.o")
add_custom_command(OUTPUT "${BETTERVR_OBJECT}"
	COMMAND ld.lld-18 -r -o "${CMAKE_BINARY_DIR}/bettervr-all.o" $<TARGET_OBJECTS:bettervr_objects>
	COMMAND llvm-objcopy-18 --keep-global-symbols=${PORT}/layer/exports.txt "${CMAKE_BINARY_DIR}/bettervr-all.o" "${BETTERVR_OBJECT}"
	DEPENDS bettervr_objects $<TARGET_OBJECTS:bettervr_objects> "${PORT}/layer/exports.txt"
	COMMAND_EXPAND_LISTS
	COMMENT "BetterVR's one object, its own symbols local")
add_custom_target(bettervr ALL DEPENDS "${BETTERVR_OBJECT}")
