#
# ThumbyRogue — host (Linux/SDL2) build.
#
# Links against the VENDORED ThumbyCraft engine under ../engine/src so
# ThumbyRogue is fully self-contained — no cross-repo coupling and no
# risk of changes here leaking back into ThumbyCraft.
#
# Build:
#   cmake -B build_host -S host
#   cmake --build build_host -j8
#   ./build_host/thumbyrogue_host [seed]
#
cmake_minimum_required(VERSION 3.16)
project(thumbyrogue_host C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

set(ROGUE_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(CRAFT_ROOT ${ROGUE_ROOT}/engine)
set(SRC        ${CRAFT_ROOT}/src)
set(ROGUE_SRC  ${ROGUE_ROOT}/src)

# Bake the procedural texture atlas to const data (mirrors ThumbyCraft /
# ThumbyGolf): the baker compiles craft_blocks.c without
# CRAFT_TEXTURES_BAKED, runs it, and dumps the in-RAM atlas to a
# generated .c that we compile with CRAFT_TEXTURES_BAKED=1.
include(ExternalProject)
ExternalProject_Add(thumbyrogue_baker
    SOURCE_DIR ${CRAFT_ROOT}/tools
    BINARY_DIR ${CMAKE_BINARY_DIR}/baker
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
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
    COMMENT "Baking craft_textures atlas"
)
add_custom_target(generate_craft_textures DEPENDS ${CRAFT_BAKED_C})

# Host has no flash — chunk store is a no-op stub.
set(CHUNK_STORE_STUB ${CRAFT_ROOT}/host/craft_chunk_store_stub.c)

add_executable(thumbyrogue_host
    ${CMAKE_CURRENT_LIST_DIR}/host_main.c
    ${CHUNK_STORE_STUB}
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
    ${ROGUE_SRC}/rogue_shop.c
    ${ROGUE_SRC}/rogue_particle.c
    ${ROGUE_SRC}/rogue_dmgnum.c
    ${ROGUE_SRC}/rogue_loot.c
    ${ROGUE_SRC}/rogue_proj.c
    ${ROGUE_SRC}/rogue_band.c
    ${ROGUE_SRC}/rogue_sfx.c
    ${ROGUE_SRC}/rogue_platform.c
    # Lean module set: only what's needed to generate + render a world.
    # ThumbyRogue has NO redstone and NO furnace/smelting, so those
    # SRAM-heavy subsystems are dropped (the three craft_redstone_* hooks
    # craft_world calls are no-op'd in host_main). Gameplay modules
    # (mobs/chests/drops/player/hud/save/audio) get added in the phases
    # that actually use them, so device SRAM only pays for what's live.
    ${SRC}/craft_world.c
    ${SRC}/craft_blocks.c
    ${SRC}/craft_gen.c
    ${SRC}/craft_render.c
    ${SRC}/craft_torches.c
    ${SRC}/craft_tool_models.c
    ${SRC}/craft_font.c
    ${SRC}/craft_audio.c
    ${CRAFT_BAKED_C}
)
add_dependencies(thumbyrogue_host generate_craft_textures)

target_compile_definitions(thumbyrogue_host PRIVATE
    CRAFT_TEXTURES_BAKED=1
    CRAFT_HOST=1
    ROGUE_FULLFRAME_RENDER=1   # no craft hotbar plate -> render edge-to-edge
    # ThumbyRogue keeps the engine default 64x64x64 cubic world — one
    # bounded dungeon floor per level.
)
target_include_directories(thumbyrogue_host PRIVATE
    ${SRC}
    ${ROGUE_SRC}
    ${ROGUE_ROOT}/device   # craft_buttons.h type (host doesn't link buttons.c)
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(thumbyrogue_host PRIVATE ${SDL2_LIBRARIES} m)
