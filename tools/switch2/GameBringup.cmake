# Keep CPU/loader bring-up separate from the existing Switch 1 and desktop game
# targets. This does not yet link the full GX2/HLE/SDL game host.
file(READ "${WWHD_GAME_MANIFEST}" game_manifest)
string(JSON GAME_RPX_SHA256 GET "${game_manifest}" rpx_sha256)
string(JSON GAME_ENTRY GET "${game_manifest}" entry)
string(JSON GAME_BUILD GET "${game_manifest}" build)
string(LENGTH "${GAME_RPX_SHA256}" digest_length)
if(NOT digest_length EQUAL 64 OR NOT GAME_RPX_SHA256 MATCHES "^[0-9a-f]+$" OR
   NOT GAME_ENTRY MATCHES "^[0-9]+$" OR NOT GAME_BUILD MATCHES "^(USA|EU)$")
  message(FATAL_ERROR "Invalid private game manifest; run prepare_game.py again")
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/private")
configure_file("${CMAKE_CURRENT_LIST_DIR}/game_identity.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/private/game_identity.h" @ONLY)
find_path(WWHD_ZLIB_INCLUDE zlib.h HINTS "${DEVKITPRO}/portlibs/switch/include" NO_DEFAULT_PATH REQUIRED)
find_library(WWHD_ZLIB z HINTS "${DEVKITPRO}/portlibs/switch/lib" NO_DEFAULT_PATH REQUIRED)

add_executable(wwhd_switch2_runtime_probe runtime_probe.cpp runtime_memory_host.cpp probe_runtime.c
  "${WWHD_ROOT}/runtime/src/rpx_loader.cpp"
  "${WWHD_ROOT}/runtime/src/crash_memory.cpp"
  "${WWHD_ROOT}/runtime/src/platform/switch2_memory.cpp")
target_compile_features(wwhd_switch2_runtime_probe PRIVATE cxx_std_20)
target_compile_definitions(wwhd_switch2_runtime_probe PRIVATE WWHD_PROBE_RUNTIME_HOST=1)
target_compile_options(wwhd_switch2_runtime_probe PRIVATE -Wall -Wextra -fsigned-char -ffp-contract=off)
target_include_directories(wwhd_switch2_runtime_probe PRIVATE
  "${WWHD_ROOT}/runtime/src" "${WWHD_ROOT}/runtime/include"
  "${CMAKE_CURRENT_BINARY_DIR}/private" "${WWHD_ZLIB_INCLUDE}")
target_link_libraries(wwhd_switch2_runtime_probe PRIVATE "${WWHD_ZLIB}")
nx_generate_nacp(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/runtime.nacp"
  NAME "WWHD Switch 2 - RPX loader test" AUTHOR "SwitchWakerHD contributors" VERSION "${PROJECT_VERSION}")
nx_create_nro(wwhd_switch2_runtime_probe NACP "${CMAKE_CURRENT_BINARY_DIR}/runtime.nacp")

if(WWHD_BUILD_GAMECODE)
  set(WWHD_CLANG_C_COMPILER "" CACHE FILEPATH "Clang with AArch64/musttail support (C game code only)")
  if(NOT EXISTS "${WWHD_CLANG_C_COMPILER}")
    message(FATAL_ERROR "Set WWHD_CLANG_C_COMPILER to a Clang supporting AArch64 musttail")
  endif()
  string(JSON gen GET "${game_manifest}" generated_dir)
  # TO_CMAKE_PATH treats a native drive colon as a PATH-list separator under
  # MSYS (D:/... becomes D;/...). Convert this single path explicitly instead.
  string(REPLACE "\\" "/" gen "${gen}")
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "MSYS")
    execute_process(COMMAND cygpath -u "${gen}" OUTPUT_VARIABLE gen
                    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  endif()
  if(NOT EXISTS "${gen}/table.c" OR NOT EXISTS "${gen}/funcs.h")
    message(FATAL_ERROR "Recompiler output incomplete: ${gen}")
  endif()
  file(GLOB code CONFIGURE_DEPENDS "${gen}/code_*.c")
  if(NOT code)
    message(FATAL_ERROR "No generated game functions in ${gen}")
  endif()
  list(APPEND code "${gen}/table.c" "${gen}/imports.c" "${CMAKE_CURRENT_LIST_DIR}/tailcall_check.c"
    "${WWHD_ROOT}/runtime/src/platform/switch2_dispatch.c")
  file(GLOB headers CONFIGURE_DEPENDS "${gen}/*.h" "${WWHD_ROOT}/runtime/include/*.h")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gamecode-objects")
  set(objects)
  foreach(source IN LISTS code)
    get_filename_component(name "${source}" NAME_WE)
    set(object "${CMAKE_CURRENT_BINARY_DIR}/gamecode-objects/${name}.o")
    # Generated C contains no TLS. Do not pass GCC's -mtp=soft (Clang does not
    # support it); the C++ runtime and libounce continue to use GCC/newlib.
    add_custom_command(OUTPUT "${object}"
      COMMAND "${WWHD_CLANG_C_COMPILER}" --target=aarch64-none-elf -mcpu=cortex-a78c+crypto
        -fPIC -ffunction-sections -fdata-sections -fsigned-char -O3 -std=gnu11
        -ffp-contract=off -fno-strict-aliasing -fwrapv -fomit-frame-pointer
        # GuestExit and guarded HLE exceptions must traverse ordinary guest
        # calls as well as the dispatch trampoline (bare-metal Clang C otherwise
        # emits nounwind functions without unwind tables).
        -fexceptions -funwind-tables
        -D__SWITCH__ -D__SWITCH2__ -Werror=unknown-attributes
        -isystem "${DEVKITPRO}/devkitA64/aarch64-none-elf/include"
        "-I${WWHD_ROOT}/runtime/include" "-I${gen}" -c "${source}" -o "${object}"
      DEPENDS "${source}" ${headers}
      COMMENT "Clang AArch64 guest code: ${name}" VERBATIM)
    list(APPEND objects "${object}")
  endforeach()
  add_library(wwhd_switch2_gamecode STATIC ${objects})
  set_target_properties(wwhd_switch2_gamecode PROPERTIES LINKER_LANGUAGE C)
endif()
