cmake_minimum_required(VERSION 3.13)

set(TUMOVGM_REQUIRED_PICO_SDK_VERSION "1.5.1")
set(TUMOVGM_REQUIRED_PICO_SDK_COMMIT "6a7db34ff63345a7badec79ebea3aaef1712f374")
set(TUMOVGM_REQUIRED_GCC_VERSION "12.3.1")
set(TUMOVGM_PINNED_PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk")

if(NOT DEFINED PICO_SDK_PATH OR PICO_SDK_PATH STREQUAL "")
    set(PICO_SDK_PATH "${TUMOVGM_PINNED_PICO_SDK_PATH}")
endif()

if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR
        "Pico SDK is missing at ${PICO_SDK_PATH}. Run: "
        "git submodule update --init third_party/pico-sdk && "
        "git -C third_party/pico-sdk submodule update --init lib/tinyusb")
endif()
if(NOT EXISTS "${PICO_SDK_PATH}/lib/tinyusb/src/tusb.c")
    message(FATAL_ERROR
        "TinyUSB is missing. Run: "
        "git -C third_party/pico-sdk submodule update --init lib/tinyusb")
endif()

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${PICO_SDK_PATH}"
    OUTPUT_VARIABLE TUMOVGM_PICO_SDK_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE TUMOVGM_PICO_SDK_GIT_RESULT
)
if(NOT TUMOVGM_PICO_SDK_GIT_RESULT EQUAL 0)
    message(FATAL_ERROR "Pico SDK path is not a readable Git checkout: ${PICO_SDK_PATH}")
endif()
if(NOT TUMOVGM_PICO_SDK_COMMIT STREQUAL TUMOVGM_REQUIRED_PICO_SDK_COMMIT)
    message(FATAL_ERROR
        "Unsupported Pico SDK commit ${TUMOVGM_PICO_SDK_COMMIT}; "
        "expected ${TUMOVGM_REQUIRED_PICO_SDK_COMMIT}")
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

if(NOT PICO_SDK_VERSION_STRING STREQUAL TUMOVGM_REQUIRED_PICO_SDK_VERSION)
    message(FATAL_ERROR
        "Unsupported Pico SDK version ${PICO_SDK_VERSION_STRING}; "
        "expected ${TUMOVGM_REQUIRED_PICO_SDK_VERSION}")
endif()

project(t_vgm_fw C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR
        "Unsupported C compiler ${CMAKE_C_COMPILER_ID}; GNU Arm Embedded is required")
endif()
if(NOT CMAKE_C_COMPILER_VERSION VERSION_EQUAL TUMOVGM_REQUIRED_GCC_VERSION)
    message(FATAL_ERROR
        "Unsupported GCC version ${CMAKE_C_COMPILER_VERSION}; "
        "expected ${TUMOVGM_REQUIRED_GCC_VERSION}")
endif()

pico_sdk_init()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(TUMOVGM_VERSION "t-vgm-dev-001-003")
set(TUMOVGM_PROTOCOL_MAJOR 1)
set(TUMOVGM_PROTOCOL_MINOR 0)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_protocol_constants.py"
        --check
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)

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
    src/protocol.c
    src/recovery_probe.c
)

target_include_directories(tumovgm_recovery_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
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
pico_set_program_version(tumovgm_recovery_probe "001.003")
pico_enable_stdio_uart(tumovgm_recovery_probe 0)
pico_enable_stdio_usb(tumovgm_recovery_probe 1)
pico_add_extra_outputs(tumovgm_recovery_probe)

add_custom_command(
    TARGET tumovgm_recovery_probe
    POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" --strip-debug "$<TARGET_FILE:tumovgm_recovery_probe>"
    COMMENT "Normalizing ELF metadata for reproducible artifacts"
    VERBATIM
)

set(TUMOVGM_OUTPUT_BASE "${CMAKE_CURRENT_BINARY_DIR}/tumovgm_recovery_probe")
add_custom_command(
    TARGET tumovgm_recovery_probe
    POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_firmware_manifest.py"
        --elf "$<TARGET_FILE:tumovgm_recovery_probe>"
        --bin "${TUMOVGM_OUTPUT_BASE}.bin"
        --uf2 "${TUMOVGM_OUTPUT_BASE}.uf2"
        --output "${TUMOVGM_OUTPUT_BASE}.manifest.json"
        --version "${TUMOVGM_VERSION}"
        --protocol-major "${TUMOVGM_PROTOCOL_MAJOR}"
        --protocol-minor "${TUMOVGM_PROTOCOL_MINOR}"
        --git-commit "${TUMOVGM_GIT_COMMIT}"
        --dirty "${TUMOVGM_GIT_DIRTY}"
        --pico-sdk-version "${PICO_SDK_VERSION_STRING}"
        --pico-sdk-commit "${TUMOVGM_PICO_SDK_COMMIT}"
        --compiler-id "${CMAKE_C_COMPILER_ID}"
        --compiler-version "${CMAKE_C_COMPILER_VERSION}"
    BYPRODUCTS "${TUMOVGM_OUTPUT_BASE}.manifest.json"
    VERBATIM
)

message(STATUS "TumoVGM version: ${TUMOVGM_VERSION}")
message(STATUS "TumoVGM protocol: ${TUMOVGM_PROTOCOL_MAJOR}.${TUMOVGM_PROTOCOL_MINOR}")
message(STATUS "TumoVGM commit: ${TUMOVGM_GIT_COMMIT}")
message(STATUS "TumoVGM dirty: ${TUMOVGM_GIT_DIRTY}")
message(STATUS "Pico SDK: ${PICO_SDK_VERSION_STRING} (${TUMOVGM_PICO_SDK_COMMIT})")
message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
