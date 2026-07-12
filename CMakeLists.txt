cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
    else()
        message(FATAL_ERROR "Set PICO_SDK_PATH to a Pico SDK 1.5.1 checkout")
    endif()
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(t_vgm_fw C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

set(TUMOVGM_VERSION "t-vgm-dev-001-001")

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE TUMOVGM_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND git status --porcelain --untracked-files=normal
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE TUMOVGM_GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

if(TUMOVGM_GIT_STATUS STREQUAL "")
    set(TUMOVGM_GIT_DIRTY 0)
else()
    set(TUMOVGM_GIT_DIRTY 1)
endif()

add_executable(tumovgm_recovery_probe
    src/recovery_probe.c
)

target_compile_options(tumovgm_recovery_probe PRIVATE
    -Wall
    -Wextra
    -Werror
)
target_compile_definitions(tumovgm_recovery_probe PRIVATE
    TUMOVGM_VERSION="${TUMOVGM_VERSION}"
    TUMOVGM_GIT_COMMIT="${TUMOVGM_GIT_COMMIT}"
    TUMOVGM_GIT_DIRTY=${TUMOVGM_GIT_DIRTY}
)
target_link_libraries(tumovgm_recovery_probe PRIVATE
    pico_stdlib
)

pico_set_program_name(tumovgm_recovery_probe "TumoVGM Recovery Probe")
pico_set_program_description(
    tumovgm_recovery_probe
    "Input-only RP2040 recovery and USB heartbeat probe"
)
pico_set_program_version(tumovgm_recovery_probe "001.001")
pico_enable_stdio_uart(tumovgm_recovery_probe 0)
pico_enable_stdio_usb(tumovgm_recovery_probe 1)
pico_add_extra_outputs(tumovgm_recovery_probe)

message(STATUS "TumoVGM version: ${TUMOVGM_VERSION}")
message(STATUS "TumoVGM commit: ${TUMOVGM_GIT_COMMIT}")
message(STATUS "TumoVGM dirty: ${TUMOVGM_GIT_DIRTY}")
