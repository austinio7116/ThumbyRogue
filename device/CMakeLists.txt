#
# ThumbyRogue — device firmware (RP2350, Thumby Color). Phase 0.
#
# Standalone build — produces firmware_thumbyrogue.uf2 to drop onto the
# RPI-RP2350 mass-storage drive after entering BOOTSEL (turn off, hold
# DOWN, turn on).
#
# Build:
#   cmake -S device -B build_device \
#       -DPICO_SDK_PATH=$HOME/mp-thumby/lib/pico-sdk
#   cmake --build build_device -j8
#   cp build_device/thumbyrogue.uf2 ../firmware_thumbyrogue.uf2
#
# Wrapped as a ThumbyOne slot (THUMBYONE_SLOT_MODE) in a later phase.
#
cmake_minimum_required(VERSION 3.16)

set(PICO_BOARD tinycircuits_thumby_color_rp2350)
set(PICO_PLATFORM rp2350)
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(thumbyrogue_device C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

pico_sdk_init()

set(ROGUE_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(CRAFT_ROOT ${ROGUE_ROOT}/engine)
set(SRC        ${CRAFT_ROOT}/src)
set(ROGUE_SRC  ${ROGUE_ROOT}/src)
set(DEVICE     ${CMAKE_CURRENT_LIST_DIR})

# Bake the procedural texture atlas to const flash data — saves ~32 KB
# SRAM. Native host tool via sub-CMake so the cross toolchain skips it.
include(ExternalProject)
ExternalProject_Add(thumbyrogue_baker
    SOURCE_DIR ${CRAFT_ROOT}/tools
    BINARY_DIR ${CMAKE_BINARY_DIR}/baker
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=cc
        -DCMAKE_SYSTEM_NAME=
        -DCMAKE_SYSTEM_PROCESSOR=
        -DCMAKE_C_FLAGS=
        -DCMAKE_EXE_LINKER_FLAGS=
    INSTALL_COMMAND ""
    BUILD_ALWAYS 1
    BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/baker/bake_textures
)
set(CRAFT_BAKED_C ${CMAKE_BINARY_DIR}/generated/craft_textures_baked.c)
add_custom_command(
    OUTPUT ${CRAFT_BAKED_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    COMMAND ${CMAKE_BINARY_DIR}/baker/bake_textures ${CRAFT_BAKED_C}
    DEPENDS thumbyrogue_baker ${SRC}/craft_blocks.c ${SRC}/craft_blocks.h
    COMMENT "Baking craft_textures atlas to flash"
)
add_custom_target(generate_craft_textures DEPENDS ${CRAFT_BAKED_C})

add_executable(thumbyrogue
    # Platform layer.
    ${DEVICE}/rogue_device_main.c
    ${DEVICE}/craft_lcd_gc9107.c
    ${DEVICE}/craft_buttons.c
    ${DEVICE}/craft_chunk_store_stub.c
    ${DEVICE}/craft_audio_pwm.c

    # ThumbyRogue gameplay.
    ${ROGUE_SRC}/rogue_game.c
    ${ROGUE_SRC}/rogue_camera.c
    ${ROGUE_SRC}/rogue_player.c
    ${ROGUE_SRC}/rogue_render.c
    ${ROGUE_SRC}/rogue_gen.c
    ${ROGUE_SRC}/rogue_enemy.c
    ${ROGUE_SRC}/rogue_hud.c
    ${ROGUE_SRC}/rogue_items.c
    ${ROGUE_SRC}/rogue_stats.c
    ${ROGUE_SRC}/rogue_inventory.c
    ${ROGUE_SRC}/rogue_loot.c
    ${ROGUE_SRC}/rogue_proj.c
    ${ROGUE_SRC}/rogue_band.c
    ${ROGUE_SRC}/rogue_sfx.c
    ${ROGUE_SRC}/rogue_platform.c

    # Lean vendored engine set — render path only. NO redstone, furnace,
    # mobs, chests, audio etc. yet (added per phase that uses them) so
    # device SRAM only pays for what's live.
    ${SRC}/craft_world.c
    ${SRC}/craft_blocks.c
    ${SRC}/craft_render.c
    ${SRC}/craft_torches.c
    ${SRC}/craft_tool_models.c
    ${SRC}/craft_font.c
    ${SRC}/craft_audio.c
    ${SRC}/craft_gen.c

    ${CRAFT_BAKED_C}
)

target_include_directories(thumbyrogue PRIVATE ${SRC} ${ROGUE_SRC} ${DEVICE})

target_compile_options(thumbyrogue PRIVATE
    -O3 -g -Wall -Wextra
    -Wno-unused-parameter -Wno-unused-function -Wno-unused-but-set-variable
    -ffast-math
    -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
)

target_link_libraries(thumbyrogue
    pico_stdlib pico_multicore pico_rand
    hardware_spi hardware_dma hardware_pwm hardware_irq
    hardware_flash hardware_sync hardware_clocks hardware_gpio
)

target_compile_definitions(thumbyrogue PRIVATE
    PICO_HEAP_SIZE=0x4000
    PICO_STACK_SIZE=0x1000
    PICO_CORE1_STACK_SIZE=0x1000
    CRAFT_TEXTURES_BAKED=1
    ROGUE_FULLFRAME_RENDER=1   # no craft hotbar plate -> render edge-to-edge
    # Engine-default 64x64x64 cubic world = one bounded dungeon floor.
    # This is ThumbyCraft's native device config, so it's known to fit.
)
add_dependencies(thumbyrogue generate_craft_textures)

pico_enable_stdio_usb(thumbyrogue 0)
pico_enable_stdio_uart(thumbyrogue 0)

pico_add_extra_outputs(thumbyrogue)
