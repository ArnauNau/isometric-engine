cmake_minimum_required(VERSION 3.30)
project(NAU_Engine C)

set(CMAKE_C_STANDARD 23)

# Set sanitizer flags
set(SANITIZER_FLAGS "-fsanitize=address,undefined")
add_compile_options(${SANITIZER_FLAGS} -fsanitize-address-use-after-scope)
# add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -O2 -flto)
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -g -O0)
add_link_options(${SANITIZER_FLAGS} -fsanitize-address-use-after-scope)

# Add SDL3, SDL_ttf, SDL_image as subdirectories
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL_ttf EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL_image EXCLUDE_FROM_ALL)

# Define your executable
add_executable(NAU_Engine main.c
        profiler.c
        profiler.h
        camera/camera.c
        camera/camera.h
        entity.c
        entity.h
        sparse_set.c
        sparse_set.h
        ecs.c
        ecs.h
        renderer/renderer.c
        renderer/renderer.h
        renderer/ui.c
        renderer/ui.h)

# Link against vendored libraries
target_link_libraries(NAU_Engine PRIVATE
    SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)

if(APPLE)
  set_target_properties(NAU_Engine PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
  )
endif()
