cmake_minimum_required(VERSION 3.20)
project(wwhd_switch2_probe VERSION 0.1.0 LANGUAGES C CXX)
if(NOT NINTENDO_SWITCH2)
  message(FATAL_ERROR "Use -DCMAKE_TOOLCHAIN_FILE=../../cmake/switch2/Switch2.cmake (absolute path recommended)")
endif()
get_filename_component(WWHD_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
# The pinned SDK 20261114 archives use the newer newlib _reent layout.
# A successful link with an older libc is not evidence of ABI compatibility:
# its _open_r can write deviceData at 344 while fsdev_open reads it at 568.
if(OUNCE_LIBRARY_DIR STREQUAL "${NX_ROOT}/lib" AND EXISTS "${OUNCE_SDK_ROOT}/ounce-devkit-version.txt")
  file(READ "${OUNCE_SDK_ROOT}/ounce-devkit-version.txt" sdk_version)
  if(sdk_version MATCHES "20261114")
    include(CheckCSourceCompiles)
    unset(WWHD_PINNED_SDK_REENT_ABI CACHE)
    check_c_source_compiles("#include <stddef.h>\n#include <sys/reent.h>\n_Static_assert(offsetof(struct _reent, deviceData) == 568, \"newlib ABI mismatch\");\nint main(void) { return 0; }"
      WWHD_PINNED_SDK_REENT_ABI)
    if(NOT WWHD_PINNED_SDK_REENT_ABI)
      message(FATAL_ERROR "Pinned libounce/newlib ABI mismatch. Use python tools/switch2/build.py --rebuild-libounce; do not link the SDK's prebuilt libounce to this libc.")
    endif()
  endif()
endif()
add_executable(wwhd_switch2_probe
  first_frame.cpp probe_runtime.c
  "${WWHD_ROOT}/runtime/src/platform/switch2_vulkan.cpp"
  "${WWHD_ROOT}/runtime/src/gfx/vulkan/loader.cpp")
target_compile_features(wwhd_switch2_probe PRIVATE cxx_std_20)
target_compile_definitions(wwhd_switch2_probe PRIVATE VK_NO_PROTOTYPES VK_USE_PLATFORM_VI_NN WWHD_PROBE_VULKAN_HOST=1)
target_compile_options(wwhd_switch2_probe PRIVATE -Wall -Wextra -Wno-missing-field-initializers -fsigned-char)
target_include_directories(wwhd_switch2_probe PRIVATE "${WWHD_ROOT}/runtime/src")
# An absolute path prevents accidentally linking Switch 1's Vulkan library.
target_link_libraries(wwhd_switch2_probe PRIVATE "${OUNCE_PORTLIBS}/lib/libvulkan.a")
nx_generate_nacp(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/probe.nacp"
  NAME "WWHD Switch 2 - Vulkan test" AUTHOR "SwitchWakerHD contributors" VERSION "${PROJECT_VERSION}")
nx_create_nro(wwhd_switch2_probe NACP "${CMAKE_CURRENT_BINARY_DIR}/probe.nacp")

# Isolate loader/libc startup from Vulkan, C++ constructors and default applet
# services. The second binary bypasses libounce's mimalloc as an A/B test.
# Do not leave the default -lnx in the C targets: its libounce.a can resolve
# malloc before newlib's libc.a does, defeating the core-only experiment.
set(CMAKE_C_STANDARD_LIBRARIES "-lm")
foreach(variant boot boot_core boot_posix)
  set(target wwhd_switch2_${variant})
  add_executable(${target} boot.c probe_runtime.c)
  target_compile_options(${target} PRIVATE -Wall -Wextra)
  if(variant STREQUAL "boot_core")
    target_compile_definitions(${target} PRIVATE WWHD_PROBE_CORE=1)
    target_link_libraries(${target} PRIVATE "-Wl,--start-group"
      "${OUNCE_LIBRARY_DIR}/libounce_core.a" c gcc "-Wl,--end-group")
  else()
    target_link_libraries(${target} PRIVATE "${OUNCE_LIBRARY_DIR}/libounce.a")
    if(variant STREQUAL "boot_posix")
      target_compile_definitions(${target} PRIVATE WWHD_PROBE_POSIX=1)
    endif()
  endif()
  nx_generate_nacp(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${variant}.nacp"
    NAME "WWHD Switch 2 - ${variant}" AUTHOR "SwitchWakerHD contributors" VERSION "${PROJECT_VERSION}")
  nx_create_nro(${target} NACP "${CMAKE_CURRENT_BINARY_DIR}/${variant}.nacp")
endforeach()

set(WWHD_GAME_MANIFEST "" CACHE FILEPATH "Private metadata from prepare_game.py")
option(WWHD_BUILD_GAMECODE "Compile the personal recompiled game to an AArch64 static library (not a playable NRO)" OFF)
if(WWHD_GAME_MANIFEST)
  include("${CMAKE_CURRENT_LIST_DIR}/GameBringup.cmake")
elseif(WWHD_BUILD_GAMECODE)
  message(FATAL_ERROR "WWHD_BUILD_GAMECODE requires WWHD_GAME_MANIFEST from prepare_game.py")
endif()
