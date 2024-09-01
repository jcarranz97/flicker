cmake_minimum_required(VERSION 3.12)
set(TARGET_NAME main)

# Pull in SDK (must be before project)
include(cmake/pico_sdk_import.cmake)

# Include FreeRTOS
if (NOT FREERTOS_KERNEL_PATH AND NOT DEFINED ENV{FREERTOS_KERNEL_PATH})
    message("Skipping FreeRTOS examples as FREERTOS_KERNEL_PATH not defined")
    return()
endif()
include(cmake/FreeRTOS_Kernel_import.cmake)

# TODO: Put your project name here
project(pico_freertos_project_template C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.0.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(PICO_EXAMPLES_PATH ${PROJECT_SOURCE_DIR})

# If you want debug output from USB (pass -DPICO_STDIO_USB=1) this ensures you don't lose any debug output while USB is set up
if (NOT DEFINED PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS)
    set(PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS 3000)
endif()

# Initialize the SDK
pico_sdk_init()

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        )
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-maybe-uninitialized)
endif()

add_executable(${TARGET_NAME}
    src/main.c
    src/usb_descriptors.c
    )
target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include/
    ${CMAKE_CURRENT_LIST_DIR}/include/freertos/
    ${CMAKE_CURRENT_LIST_DIR}/include/usb/
    )
target_link_libraries(${TARGET_NAME} PRIVATE
    pico_async_context_freertos
    FreeRTOS-Kernel-Heap4
    pico_stdlib
    pico_unique_id
    tinyusb_device
    tinyusb_board
    )
# Enable Serial over UART
pico_enable_stdio_usb(${TARGET_NAME} 0)
pico_enable_stdio_uart(${TARGET_NAME} 1)

if(PICO_CYW43_SUPPORTED)
    # For led support on pico_w
    target_link_libraries(${TARGET_NAME} PRIVATE
        pico_cyw43_arch_none
    )
endif()
target_compile_definitions(${TARGET_NAME} PRIVATE
    configNUMBER_OF_CORES=2
    )
pico_add_extra_outputs(${TARGET_NAME})
