cmake_minimum_required(VERSION 3.24)
project(fill-a-canvas LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose build type" FORCE)
endif()

if(EMSCRIPTEN)
    set(WASM_OPT_FLAGS "-O3 -flto")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${WASM_OPT_FLAGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${WASM_OPT_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${WASM_OPT_FLAGS} -sMALLOC=emmalloc")
endif()

include(FetchContent)

# --- SDL3 ---
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)

# 3.4 is the first release with SDL_SavePNG, fill-document windows and pinch events
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL
    GIT_TAG        release-3.4.18
    GIT_SHALLOW    TRUE
)

# --- SDL3_ttf ---
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# Build FreeType from SDL_ttf's bundled sources instead of looking for a system copy
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
# Not needed for plain Latin text; skipping them keeps the build small
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf
    GIT_TAG        release-3.2.2
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES external/freetype
)

FetchContent_MakeAvailable(SDL3 SDL3_ttf)

# --- Executable ---
add_executable(fill_a_canvas main.c)
target_link_libraries(fill_a_canvas PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)

if(EMSCRIPTEN)
    set_target_properties(fill_a_canvas PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
        # Relink when the shell page changes
        LINK_DEPENDS "${CMAKE_SOURCE_DIR}/shell.html"
    )
    target_link_options(fill_a_canvas PRIVATE
        "--shell-file" "${CMAKE_SOURCE_DIR}/shell.html"
        "-sUSE_WEBGL2=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "--preload-file" "${CMAKE_SOURCE_DIR}/Roboto-Regular.ttf@/Roboto-Regular.ttf"
    )
else()
    set_target_properties(fill_a_canvas PROPERTIES OUTPUT_NAME "fill-a-canvas")
    # Desktop build loads the font from next to the executable
    add_custom_command(TARGET fill_a_canvas POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/Roboto-Regular.ttf" "$<TARGET_FILE_DIR:fill_a_canvas>"
    )
endif()
