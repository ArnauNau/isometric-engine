cmake_minimum_required(VERSION 3.30)
project(miso C)

set(CMAKE_C_STANDARD 23)
enable_testing()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(COMMON_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
set(DEBUG_FLAGS -g -O0 -fsanitize=address,undefined -fsanitize-address-use-after-scope)
set(RELEASE_FLAGS -O2 -flto)

option(MISO_PGO_GENERATE "Build instrumented binary to generate PGO profile" OFF)
set(MISO_PGO_PROFILE_DIR "" CACHE PATH "Directory where profraw files will be written")
set(MISO_PGO_USE_PROFDATA "" CACHE FILEPATH "Path to merged .profdata for PGO use phase")
option(MISO_BUILD_TESTBED "Build testbed engine-client target" ON)
option(MISO_BUILD_TESTS "Build miso unit test executables" ON)

add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL_ttf EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL_image EXCLUDE_FROM_ALL)

function(miso_apply_compile_options target_name)
    target_compile_options(${target_name} PRIVATE ${COMMON_WARNINGS})

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${target_name} PRIVATE ${DEBUG_FLAGS})
        target_compile_definitions(${target_name} PRIVATE MISO_DEBUG)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(${target_name} PRIVATE ${RELEASE_FLAGS})
    endif()
endfunction()

set(MISO_CORE_SOURCES
    profiler.c
    ecs/entity.c
    ecs/sparse_set.c
    ecs/ecs.c
    renderer/renderer.c
    renderer/ui.c
    tilemap/tilemap.c
    debug_ui.c
    engine/src/internal/miso__renderer_backend.c
    engine/src/miso_engine.c
    engine/src/miso_events.c
    engine/src/miso_camera.c
    engine/src/miso_iso.c
    engine/src/miso_world.c
    engine/src/miso_buildings.c
    engine/src/miso_render.c
    engine/src/miso_render_diagnostics.c
    engine/src/miso_text.c
    engine/src/miso_debug_ui.c
    engine/src/miso_save.c
    engine/src/miso_replay.c
    engine/src/internal/miso__paths.c
)

set(MISO_TEST_SOURCES
    tests/test_tile_picking.c
)

set(MISO_ENGINE_TEST_SOURCES
    tests/test_miso_iso.c
)

add_library(miso_core STATIC ${MISO_CORE_SOURCES})

miso_apply_compile_options(miso_core)

target_include_directories(miso_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/src
)

target_link_libraries(miso_core
    PUBLIC
        SDL3::SDL3
        SDL3_ttf::SDL3_ttf
        SDL3_image::SDL3_image
)

if(MISO_BUILD_TESTS)
    add_executable(miso_unit_tests
        ${MISO_TEST_SOURCES}
    )
    add_executable(miso_engine_tests
        ${MISO_ENGINE_TEST_SOURCES}
    )
    miso_apply_compile_options(miso_unit_tests)
    miso_apply_compile_options(miso_engine_tests)

    target_include_directories(miso_unit_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/src
    )
    target_include_directories(miso_engine_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
    )

    target_link_libraries(miso_unit_tests PRIVATE miso_core)
    target_link_libraries(miso_engine_tests PRIVATE miso_core)
endif()

if(MISO_BUILD_TESTBED)
    add_executable(miso_testbed
        testbed/main_testbed.c
        testbed/testbed_game.c
    )
    miso_apply_compile_options(miso_testbed)

    target_include_directories(miso_testbed PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(miso_testbed PRIVATE miso_core)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(TARGET miso_unit_tests)
        target_link_options(miso_unit_tests PRIVATE -fsanitize=address,undefined -fsanitize-address-use-after-scope)
    endif()
    if(TARGET miso_engine_tests)
        target_link_options(miso_engine_tests PRIVATE -fsanitize=address,undefined -fsanitize-address-use-after-scope)
    endif()
    if(TARGET miso_testbed)
        target_link_options(miso_testbed PRIVATE -fsanitize=address,undefined -fsanitize-address-use-after-scope)
    endif()
    message(STATUS "Debug build: sanitizers ON, optimization OFF, MISO_DEBUG defined")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(TARGET miso_unit_tests)
        target_link_options(miso_unit_tests PRIVATE -flto)
    endif()
    if(TARGET miso_engine_tests)
        target_link_options(miso_engine_tests PRIVATE -flto)
    endif()
    if(TARGET miso_testbed)
        target_link_options(miso_testbed PRIVATE -flto)
    endif()
    message(STATUS "Release build: sanitizers OFF, optimization ON, LTO ON")
endif()

if(MISO_PGO_GENERATE)
    if(MISO_PGO_PROFILE_DIR STREQUAL "")
        message(FATAL_ERROR "MISO_PGO_GENERATE=ON requires MISO_PGO_PROFILE_DIR to be set")
    endif()

    message(STATUS "PGO Generation enabled. Instrumented binary will write profiles to: ${MISO_PGO_PROFILE_DIR}")
    message(FATAL_ERROR "MISO_PGO_GENERATE is currently only supported by executable clients outside miso_core")
endif()

if(NOT MISO_PGO_USE_PROFDATA STREQUAL "")
    if(NOT EXISTS "${MISO_PGO_USE_PROFDATA}")
        message(FATAL_ERROR "MISO_PGO_USE_PROFDATA points to missing file: ${MISO_PGO_USE_PROFDATA}")
    endif()

    message(STATUS "PGO Use enabled. Using profile data from: ${MISO_PGO_USE_PROFDATA}")
    message(FATAL_ERROR "MISO_PGO_USE_PROFDATA is currently only supported by executable clients outside miso_core")
endif()

find_program(CLANG_FORMAT_EXE NAMES clang-format)
option(MISO_FORMAT_ON_BUILD "Run formatting target before building miso" OFF)

file(GLOB_RECURSE MISO_FORMAT_C_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
list(FILTER MISO_FORMAT_C_FILES EXCLUDE REGEX "/vendored/")
list(FILTER MISO_FORMAT_C_FILES EXCLUDE REGEX "/cmake-build-[^/]+/")

if(CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${MISO_FORMAT_C_FILES}
        COMMENT "Running clang-format"
        VERBATIM
    )
else()
    add_custom_target(format
        COMMENT "Formatting C sources"
    )
    message(WARNING "clang-format not found. 'format' target will skip C/C header formatting.")
endif()

if(MISO_FORMAT_ON_BUILD)
    if(NOT CLANG_FORMAT_EXE)
        message(FATAL_ERROR "MISO_FORMAT_ON_BUILD=ON requires clang-format in PATH.")
    endif()
    if(TARGET miso_testbed)
        add_dependencies(miso_testbed format)
    endif()
    if(TARGET miso_unit_tests)
        add_dependencies(miso_unit_tests format)
    endif()
    if(TARGET miso_engine_tests)
        add_dependencies(miso_engine_tests format)
    endif()
endif()

find_program(RG_EXE rg)
add_custom_target(miso_boundary_lint
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DRG_EXE=${RG_EXE}
        -DLINT_CORE=ON
        -DLINT_GAME_CAFE=OFF
        -DLINT_TESTBED=OFF
        -DLINT_ENGINE_PORTABILITY=ON
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/miso_boundary_lint.cmake
)

add_dependencies(miso_core miso_boundary_lint)
if(MISO_BUILD_TESTS)
    add_test(NAME miso_iso_tile_to_world
        COMMAND miso_engine_tests --case tile-to-world
    )
    add_test(NAME miso_iso_world_to_tile_center
        COMMAND miso_engine_tests --case world-to-tile-center
    )
    add_test(NAME miso_iso_world_to_tile_floor_boundary
        COMMAND miso_engine_tests --case world-to-tile-floor-boundary
    )
    add_test(NAME miso_testbed_hidpi_tile_pick
        COMMAND miso_unit_tests --case testbed-hidpi
    )
    add_test(NAME miso_world_pixel_tile_pick_parity
        COMMAND miso_unit_tests --case world-pixel-parity
    )
    add_test(NAME miso_cafe_hidpi_tile_pick
        COMMAND miso_unit_tests --case cafe-hidpi
    )
    add_test(NAME miso_resize_dispatch_dedupe
        COMMAND miso_unit_tests --case resize-dispatch-dedupe
    )
    add_test(NAME miso_normalized_viewport
        COMMAND miso_unit_tests --case normalized-viewport
    )
endif()

if(TARGET miso_testbed)
    add_custom_target(miso_testbed_boundary_lint
        COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DRG_EXE=${RG_EXE}
            -DLINT_CORE=OFF
            -DLINT_GAME_CAFE=OFF
            -DLINT_TESTBED=ON
            -DLINT_ENGINE_PORTABILITY=OFF
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/miso_boundary_lint.cmake
    )
    add_dependencies(miso_testbed miso_testbed_boundary_lint)
endif()

if(APPLE)
    if(TARGET miso_testbed)
        set_target_properties(miso_testbed PROPERTIES
            OUTPUT_NAME "NAU_Engine"
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
        )
    endif()
endif()
