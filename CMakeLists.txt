cmake_minimum_required(VERSION 3.13)

# Set board to Pico 2 (RP2350)
set(PICO_BOARD pico2 CACHE STRING "Pico board type")

# Board configuration variant (M1, M2)
set(BOARD_VARIANT "M1" CACHE STRING "Board variant: M1 or M2")

include(pico_sdk_import.cmake)

# Import pico-extras for audio_i2s library (for future sound support)
set(PICO_EXTRAS_PATH ${CMAKE_CURRENT_LIST_DIR}/pico-extras CACHE PATH "Path to pico-extras")
if(EXISTS ${PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)
    include(${PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)
endif()

project(murmduke3d C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Initialize pico-extras if available
if(COMMAND pico_extras_init)
    pico_extras_init()
endif()

# Add fatfs
add_subdirectory(src/fatfs)
# Add drivers
add_subdirectory(drivers/sdcard)
add_subdirectory(drivers/ps2kbd)

# Create a library for other drivers
add_library(drivers
    drivers/HDMI.c
    drivers/psram_init.c
    drivers/psram_allocator.c
)
target_include_directories(drivers PUBLIC drivers)
target_link_libraries(drivers pico_stdlib hardware_dma hardware_pio hardware_spi)

# Duke3D Engine sources
set(ENGINE_SOURCES
    components/Engine/cache.c
    components/Engine/display.c
    components/Engine/draw.c
    components/Engine/engine.c
    components/Engine/filesystem.c
    components/Engine/fixedPoint_math.c
    components/Engine/network.c
    components/Engine/tiles.c
    components/Engine/mmulti.c
)

# Duke3D Game sources  
set(GAME_SOURCES
    components/Game/actors.c
    components/Game/animlib.c
    components/Game/config.c
    components/Game/console.c
    components/Game/control.c
    components/Game/cvar_defs.c
    components/Game/cvars.c
    components/Game/game.c
    components/Game/gamedef.c
    components/Game/global.c
    components/Game/keyboard.c
    components/Game/menues.c
    components/Game/player.c
    components/Game/premap.c
    components/Game/rts.c
    components/Game/scriplib.c
    components/Game/sector.c
    components/Game/sounds.c
)

# Audiolib - use stub implementation for initial port (no sound)
# The full audiolib will be integrated later
set(AUDIOLIB_SOURCES
    src/audio_stub.c
)

add_executable(murmduke3d
    src/main.c
    src/duke3d_rp2350.c
    src/compat.c
    src/psram_data.c
    src/SDL/SDL_rp2350.c
    src/SDL/SDL_video_rp2350.c
    src/SDL/SDL_event_rp2350.c
    src/SDL/SDL_audio_stub.c
    src/fatfs_stdio.c
    ${ENGINE_SOURCES}
    ${GAME_SOURCES}
    ${AUDIOLIB_SOURCES}
)

target_include_directories(murmduke3d PRIVATE
    src
    src/SDL
    src/fatfs
    src/freertos
    components/Engine
    components/Game
    components/audiolib
    drivers
    drivers/sdcard
)

target_compile_definitions(murmduke3d PRIVATE
    BOARD_${BOARD_VARIANT}
    PLATFORM_SUPPORTS_SDL
    DUKE3D_RP2350
    PLATFORM_ESP32  # Use ESP32 platform code paths
    # Disable sound for initial port
    NOSOUND
    # Screen resolution
    DUKE3D_RESX=320
    DUKE3D_RESY=200
    # Platform defines
    PICO_ON_DEVICE=1
    PICO_BOARD
    # Memory management - large arrays in PSRAM
    RP2350_PSRAM
    EXT_RAM_ATTR=
)

# Relax some warnings for the Duke3D engine code (32-bit pointer issues)
target_compile_options(murmduke3d PRIVATE
    -Wno-int-conversion
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
    -Wno-overflow
    -Wno-incompatible-pointer-types
)

# Allow multiple definitions (ESP32 port has variables defined in headers)
# This is a workaround - ideally headers should use extern
target_link_options(murmduke3d PRIVATE
    -Wl,--allow-multiple-definition
)

# Use custom linker script with PSRAM sections
pico_set_linker_script(murmduke3d ${CMAKE_CURRENT_SOURCE_DIR}/memmap_psram.ld)

target_link_libraries(murmduke3d
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_dma
    hardware_pio
    hardware_pwm
    hardware_irq
    hardware_interp
    sdcard
    ps2kbd
    fatfs
    drivers
)

# Wrap stdio functions to use FatFS
target_link_options(murmduke3d PRIVATE
    -Wl,--wrap=fopen
    -Wl,--wrap=fclose
    -Wl,--wrap=fread
    -Wl,--wrap=fgetc
    -Wl,--wrap=fwrite
    -Wl,--wrap=fseek
    -Wl,--wrap=ftell
    -Wl,--wrap=remove
    -Wl,--wrap=rename
    -Wl,--wrap=open
    -Wl,--wrap=close
    -Wl,--wrap=read
    -Wl,--wrap=write
    -Wl,--wrap=lseek
)

# Enable USB stdio
pico_enable_stdio_usb(murmduke3d 1)
pico_enable_stdio_uart(murmduke3d 0)

pico_add_extra_outputs(murmduke3d)
