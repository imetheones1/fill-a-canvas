cmake_minimum_required(VERSION 3.24)
project(MySDL3WasmGame LANGUAGES C CXX)

# Enforce a Release build configuration if none is declared
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose build type" FORCE)
endif()

# Apply aggressive WebAssembly optimization flags
if(EMSCRIPTEN)
    set(WASM_OPT_FLAGS "-O3 -flto -sMALLOC=emmalloc")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${WASM_OPT_FLAGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${WASM_OPT_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${WASM_OPT_FLAGS}")
endif()

include(FetchContent)

# --- SDL3 Configurations ---
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL
    GIT_TAG        main # Or use a specific tag like release-3.2.0
)

# --- SDL3_ttf Configurations ---
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
    GIT_TAG        main
)

FetchContent_MakeAvailable(SDL3 SDL3_ttf)

# --- Executable & Target Linking ---
add_executable(my_game main.c)
target_link_libraries(my_game PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)

if(EMSCRIPTEN)
    set_target_properties(my_game PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
        # Relink when the shell page changes
        LINK_DEPENDS "${CMAKE_SOURCE_DIR}/shell.html"
    )
    target_link_options(my_game PRIVATE
        "--shell-file" "${CMAKE_SOURCE_DIR}/shell.html"
        "-sUSE_WEBGL2=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "--preload-file" "${CMAKE_SOURCE_DIR}/Roboto-Regular.ttf@/Roboto-Regular.ttf"
    )
else()
    # Desktop build loads the font from next to the executable
    add_custom_command(TARGET my_game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/Roboto-Regular.ttf" "$<TARGET_FILE_DIR:my_game>"
    )
endif()