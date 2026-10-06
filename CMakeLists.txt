cmake_minimum_required(VERSION 3.24)
project(fill-a-canvas LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose build type" FORCE)
endif()

# Set before SDL is fetched so SDL, SDL_ttf and FreeType get link-time optimization too
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR LANGUAGES C)
if(IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    if(EMSCRIPTEN)
        # CMake picks ThinLTO for Clang; full LTO gives emscripten smaller, faster output
        set(CMAKE_C_COMPILE_OPTIONS_IPO -flto)
        set(CMAKE_CXX_COMPILE_OPTIONS_IPO -flto)
    endif()
else()
    message(WARNING "Link-time optimization unavailable: ${IPO_ERROR}")
endif()

if(EMSCRIPTEN)
    # CMake only passes -O3 when compiling; emcc also needs it when linking to optimize the wasm and JS
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " -O3")
endif()

# Static MSVC runtime for everything, including SDL, so the .exe needs no Visual C++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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

# --- Embedded font ---
# Turned into a C array so the program doesn't need the .ttf next to it
set(FONT_FILE "${CMAKE_SOURCE_DIR}/Roboto-Regular.ttf")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FONT_FILE}")
file(READ "${FONT_FILE}" FONT_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FONT_BYTES "${FONT_HEX}")
file(WRITE "${CMAKE_BINARY_DIR}/font_data.c.in"
    "#include <stddef.h>\n"
    "const unsigned char roboto_regular_ttf[] = {${FONT_BYTES}};\n"
    "const size_t roboto_regular_ttf_size = sizeof(roboto_regular_ttf);\n")
# Only replaces font_data.c when the contents change, so it isn't recompiled on every configure
configure_file("${CMAKE_BINARY_DIR}/font_data.c.in" "${CMAKE_BINARY_DIR}/font_data.c" COPYONLY)

# --- Executable ---
add_executable(fill_a_canvas main.c "${CMAKE_BINARY_DIR}/font_data.c")
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
        "-sENVIRONMENT=web"
        "-sMALLOC=emmalloc"
        "$<$<CONFIG:Release>:--closure=1>"
    )
else()
    set_target_properties(fill_a_canvas PROPERTIES
        OUTPUT_NAME "fill-a-canvas"
        # A GUI program on Windows, so no console window opens alongside it
        WIN32_EXECUTABLE TRUE
    )
    if(MINGW)
        # Link the GCC runtime in so the .exe runs on machines without MinGW's DLLs
        target_link_options(fill_a_canvas PRIVATE -static "$<$<CONFIG:Release>:-s>")
    endif()
endif()
