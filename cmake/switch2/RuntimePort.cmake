# Separate from Switch 1/deko3d. Compile-only by default; --nro explicitly
# selects the experimental host that exits the entire title process.
set(WWHD_SWITCH ON)
set(WWHD_SWITCH2 ON)
set(WWHD_RENDERER VULKAN)
set(WWHD_HAS_METAL OFF)
set(WWHD_HAS_VULKAN ON)
set(WWHD_HAS_DEKO3D OFF)
set(WWHD_SDL_HOST ON)
set(WWHD_SWITCH2_VERSION "0.0.1")
set(WWHD_VERSION "${WWHD_SWITCH2_VERSION}" CACHE STRING "Switch 2 homebrew version" FORCE)
option(WWHD_SWITCH2_BUILD_NRO "Build experimental whole-process-exit Switch 2 NRO" OFF)
set(WWHD_EDITOR_MCP OFF CACHE BOOL "No editor network server on console" FORCE)
add_compile_options(-fsigned-char)
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/build/switch2-deps/prefix")
set(Vulkan_INCLUDE_DIR "${OUNCE_PORTLIBS}/include" CACHE PATH "OunceVK headers")
set(Vulkan_LIBRARY "${OUNCE_PORTLIBS}/lib/libvulkan.a" CACHE FILEPATH "OunceVK static loader")
message(STATUS "Switch 2: GCC runtime + Clang game archive, Vulkan/SDL; experimental NRO=${WWHD_SWITCH2_BUILD_NRO}")

function(switch2_import_gamecode)
  set(WWHD_SWITCH2_GAMECODE "${CMAKE_SOURCE_DIR}/build/switch2-first-frame/libwwhd_switch2_gamecode.a"
      CACHE FILEPATH "Private Clang game-code archive from tools/switch2/build.py")
  if(NOT EXISTS "${WWHD_SWITCH2_GAMECODE}")
    message(FATAL_ERROR "Build the private game archive with tools/switch2/build.py --build-gamecode first")
  endif()
  add_library(gamecode STATIC IMPORTED GLOBAL)
  set_target_properties(gamecode PROPERTIES IMPORTED_LOCATION "${WWHD_SWITCH2_GAMECODE}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/runtime/include;${GEN_DIR}")
endfunction()

macro(switch_port_libraries)
  target_compile_options(cemu_latte PRIVATE -fno-strict-aliasing)
  target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
  include(FetchContent)
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()
  set(LZ4_BUILD_CLI OFF CACHE BOOL "" FORCE)
  set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "" FORCE)
  set(LZ4_BUNDLED_MODE ON)
  FetchContent_Declare(lz4
    URL https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz
    URL_HASH SHA256=537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b
    SOURCE_SUBDIR build/cmake)
  FetchContent_MakeAvailable(lz4)
endmacro()

macro(switch_port_sources)
  list(FILTER RUNTIME_SOURCES EXCLUDE REGEX "runtime/src/crash_addr\\.cpp$")
  list(APPEND GFX_SOURCES runtime/src/platform/switch2_vulkan.cpp
    runtime/src/platform/switch2_memory.cpp runtime/src/platform/upstream_stubs_switch.cpp
    runtime/src/platform/settings_ini.cpp runtime/src/platform/posix_stubs_switch.cpp
    runtime/src/platform/switch2_host.cpp runtime/src/platform/switch2_profile.cpp tools/switch2/probe_runtime.c)
endmacro()

macro(switch_port_executable)
  set_source_files_properties(tools/switch2/probe_runtime.c PROPERTIES COMPILE_OPTIONS "")
  target_compile_options(wwhd PRIVATE -fno-strict-aliasing)
  target_compile_definitions(wwhd PRIVATE VK_USE_PLATFORM_VI_NN=1 WWHD_GAME_HOST=1)
  target_include_directories(wwhd PRIVATE "${CMAKE_SOURCE_DIR}/tools/switch2")
  # C portlibs are used only from the selected devkitPro tree (the same zlib
  # already validated by the RPX probe). Never search a host Windows library.
  find_library(WWHD_SWITCH2_ZSTD zstd HINTS "${DEVKITPRO}/portlibs/switch/lib" NO_DEFAULT_PATH REQUIRED)
  target_link_libraries(wwhd PRIVATE "${WWHD_SWITCH2_ZSTD}")
  if(WWHD_SWITCH2_BUILD_NRO)
    # EGL is the SDK's linker script for the combined libouncevk-gl provider,
    # which exports both Vulkan and EGL. Do not also pull Vulkan-only
    # libouncevk.a: their two providers define the same Vulkan entry points.
    foreach(lib EGL GLESv2 glapi drm_nouveau)
      target_link_libraries(wwhd PRIVATE "${OUNCE_PORTLIBS}/lib/lib${lib}.a")
    endforeach()
    target_link_options(wwhd PRIVATE -Wl,--wrap=main -Wl,--wrap=exit -Wl,--wrap=_Exit -Wl,--wrap=_exit
      -Wl,--wrap=abort -Wl,--wrap=pthread_create -Wl,--wrap=pthread_detach -Wl,--wrap=getcwd)
    set_target_properties(wwhd PROPERTIES OUTPUT_NAME wwhd_switch2)
    set(WWHD_SWITCH2_ICON "${CMAKE_SOURCE_DIR}/build/switch2-branding/icon-256.jpg"
        CACHE FILEPATH "Optional private-build 256x256 JPEG icon (copyrighted logo not shipped in source)")
    set(switch2_icon_args)
    if(EXISTS "${WWHD_SWITCH2_ICON}")
      list(APPEND switch2_icon_args ICON "${WWHD_SWITCH2_ICON}")
    endif()
    nx_generate_nacp(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wwhd_switch2.nacp"
      NAME "WWHD Switch 2" AUTHOR "SwitchWakerHD / Zoria" VERSION "${WWHD_SWITCH2_VERSION}")
    nx_create_nro(wwhd NACP "${CMAKE_CURRENT_BINARY_DIR}/wwhd_switch2.nacp" ${switch2_icon_args})
  endif()
endmacro()
