include_guard(GLOBAL)
foreach(lang C CXX ASM)
  set(CMAKE_${lang}_STANDARD_INCLUDE_DIRECTORIES
      "${NX_ROOT}/include;${OUNCE_PORTLIBS}/include" CACHE STRING "")
endforeach()
include("${DEVKITPRO}/cmake/Platform/NintendoSwitch.cmake")
set(NINTENDO_SWITCH2 TRUE)
file(STRINGS "${NX_ROOT}/switch2_cpu.mk" cpu REGEX "^SWITCH2_CPU[ \t]*:=")
string(REGEX REPLACE "^SWITCH2_CPU[ \t]*:=[ \t]*" "" cpu "${cpu}")
if(NOT cpu)
  message(FATAL_ERROR "Missing SWITCH2_CPU in ounce-devkit")
endif()
set(NX_ARCH_SETTINGS "${cpu} -mtp=soft -ftls-model=local-exec")
__dkp_init_platform_settings(NX)
foreach(lang C CXX ASM)
  string(APPEND CMAKE_${lang}_FLAGS_INIT " -D__SWITCH2__")
endforeach()
# Stock specs hard-code DEVKITPRO/libounce. Relocate only that reference for
# a repository-local SDK; keep all other ounce linker/startup settings intact.
file(READ "${NX_ROOT}/switch.specs" specs)
if(NOT specs MATCHES "-T %:getenv\\(DEVKITPRO /libounce/switch.ld\\)")
  message(FATAL_ERROR "Unrecognized ounce switch.specs: review linker-script relocation")
endif()
string(REPLACE "-T %:getenv(DEVKITPRO /libounce/switch.ld)" "" specs "${specs}")
file(WRITE "${CMAKE_BINARY_DIR}/switch2.specs" "${specs}")
string(REPLACE "-specs=${NX_ROOT}/switch.specs" "-specs=\"${CMAKE_BINARY_DIR}/switch2.specs\""
       CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT}")
string(PREPEND CMAKE_EXE_LINKER_FLAGS_INIT "-L\"${OUNCE_LIBRARY_DIR}\" -L\"${OUNCE_PORTLIBS}/lib\" ")
# Paths embedded inside specs are not translated by MSYS for the native linker.
# Pass the script as a regular, shell-quoted compiler argument instead.
set(script "${NX_ROOT}/switch.ld")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "MSYS")
  execute_process(COMMAND cygpath -m "${script}" OUTPUT_VARIABLE script
                  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
endif()
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " \"-Wl,-T,${script}\"")
