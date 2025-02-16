# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# Adiciona o cJSON ao projeto
add_library(cjson STATIC cJSON.c)

project(unidade6_2 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(unidade6_2 unidade6_2.c ssd1306.c cJSON.c)

pico_set_program_name(unidade6_2 "unidade6_2")
pico_set_program_version(unidade6_2 "0.1")

# Generate PIO header
#pico_generate_pio_header(unidade6_2 ${CMAKE_CURRENT_LIST_DIR}/blink.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(unidade6_2 1)
pico_enable_stdio_usb(unidade6_2 0)

# Add the standard library to the build
target_link_libraries(unidade6_2
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(unidade6_2 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(unidade6_2 
        hardware_i2c
        hardware_pio
        hardware_adc
        hardware_pwm
        hardware_timer
        pico_stdlib
        pico_lwip_http
        pico_cyw43_arch_lwip_threadsafe_background
        cjson
        )

pico_add_extra_outputs(unidade6_2)

