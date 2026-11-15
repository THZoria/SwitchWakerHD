if(NOT WWHD_COMPILER_PYTHON)
  message(FATAL_ERROR "The installer requires the actual console CPython frontend")
endif()
set(WWHD_INSTALLER_RESOURCES_DIR "" CACHE PATH "Audited content-addressed installer resources")
if(NOT EXISTS "${WWHD_INSTALLER_RESOURCES_DIR}/resources.json")
  message(FATAL_ERROR "Run tools/switch2/package_installer_resources.py first")
endif()
file(SHA256 "${WWHD_INSTALLER_RESOURCES_DIR}/resources.json" resources_sha256)
add_executable(wwhd_switch2_installer installer_main.cpp installer_platform.cpp)
set_target_properties(wwhd_switch2_installer PROPERTIES SUFFIX ".elf" CXX_STANDARD 20)
target_compile_definitions(wwhd_switch2_installer PRIVATE WWHD_RESOURCES_SHA256="${resources_sha256}")
target_include_directories(wwhd_switch2_installer PRIVATE "${WWHD_ROOT}/build/switch2-deps/prefix/include"
  "${WWHD_ROOT}/runtime/src")
target_link_libraries(wwhd_switch2_installer PRIVATE wwhd_install_pipeline
  "${WWHD_ROOT}/build/switch2-deps/prefix/lib/libSDL3.a" EGL stdc++ m)
# Static LLVM archives contain several GiB of development DWARF. Keep the
# symbol table for auditing, but do not feed that debug-only payload to
# elf2nro (whose input reader cannot handle this >2 GiB development ELF).
target_link_options(wwhd_switch2_installer PRIVATE -Wl,--no-undefined -Wl,--wrap=getcwd
  -Wl,--strip-debug)
nx_generate_nacp(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/installer.nacp"
  NAME "Wind Waker HD - Installation" AUTHOR "Zoria / SwitchWakerHD contributors" VERSION "0.0.1")
nx_create_nro(wwhd_switch2_installer
  NACP "${CMAKE_CURRENT_BINARY_DIR}/installer.nacp"
  ICON "${WWHD_INSTALLER_RESOURCES_DIR}/branding/icon.jpg"
  ROMFS "${WWHD_INSTALLER_RESOURCES_DIR}")
