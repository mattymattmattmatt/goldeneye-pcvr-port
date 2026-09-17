# ---------------------------------------------------------------------------
# GoldenEye 007 VR layer
#
# Builds independently of the N64 ROM target. The ROM is still built by the
# top-level Makefile with a MIPS toolchain; nothing here touches it, and the
# GE_VR define that activates the game-side hooks is never set for that build.
#
#   cmake -S vr -B build/vr -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/vr -j
#   ctest --test-dir build/vr --output-on-failure
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)

# The VR layer is deliberately independent of the game's headers, and that has
# to hold at the include-path level too.
#
# When vr/ is added as a subdirectory of the host port, it inherits the
# parent's directory-level include_directories(), the first of which is
# port/shim -- the game's replacements for stddef.h, stdarg.h, stdlib.h and
# string.h. The VR sources then get the game's <stddef.h> instead of the
# toolchain's, and offsetof, fmodf and friends quietly stop being declared.
#
# On glibc this goes unnoticed because the real declarations arrive anyway
# through some other transitive include. On MinGW they do not, and the build
# fails with "expected expression before 'gevr_config'" -- a message that
# points at the macro rather than at the shadowed header behind it.
#
# Removing just port/shim is not enough -- the repository's own include/ also
# carries a string.h, and PR/ultratypes.h there wants a size_t that only the
# shim's stddef.h was providing. Pulling one out exposes the next. The list has
# to go entirely.
#
# Which then takes versioninfo.h with it, since the parent injects
# `-include versioninfo.h` globally and its directory was in that list. The VR
# layer has no use for the game's build stamp, so the option goes too rather
# than the include path coming back. Everything else the parent sets --
# -fno-pie among it, which has to match the host's link -- is kept.
set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "")

get_property(_gevr_opt DIRECTORY PROPERTY COMPILE_OPTIONS)
list(FILTER _gevr_opt EXCLUDE REGEX "versioninfo")
list(FILTER _gevr_opt EXCLUDE REGEX "^-include$")
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "${_gevr_opt}")
unset(_gevr_opt)

project(gevr C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(GEVR_BUILD_CALIBRATE "Build the ge007vr-calibrate harness" ON)
option(GEVR_BUILD_TESTS     "Build the headless control tests"    ON)
option(GEVR_WERROR          "Treat warnings as errors"            OFF)

# The control mapping is the part with a right answer, so it is built and
# tested without any windowing or VR dependency at all.
add_library(gevr_core STATIC
    src/gevr_math.c
    src/gevr_controls.c
    src/gevr_headaim.c
    src/gevr_camera.c
    src/gevr_config.c
    src/gevr_engine.c
)
target_include_directories(gevr_core PUBLIC include)

if(NOT MSVC)
    target_compile_options(gevr_core PRIVATE -Wall -Wextra)
    if(GEVR_WERROR)
        target_compile_options(gevr_core PRIVATE -Werror)
    endif()
    find_library(M_LIB m)
    if(M_LIB)
        target_link_libraries(gevr_core PUBLIC ${M_LIB})
    endif()
endif()

# --------------------------------------------------------------- runtime ---

find_package(OpenXR QUIET)
if(NOT OpenXR_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OPENXR IMPORTED_TARGET openxr)
    endif()
endif()

find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2PC IMPORTED_TARGET sdl2)
    endif()
endif()

set(GEVR_HAVE_RUNTIME FALSE)
if((OpenXR_FOUND OR OPENXR_FOUND) AND (SDL2_FOUND OR SDL2PC_FOUND OR SDL2_LIBRARY))
    set(GEVR_HAVE_RUNTIME TRUE)
endif()

if(GEVR_HAVE_RUNTIME)
    add_library(gevr_runtime STATIC
        src/gevr_xr.c
        src/gevr_gl.c
    )
    target_include_directories(gevr_runtime PUBLIC include)
    target_link_libraries(gevr_runtime PUBLIC gevr_core)

    if(OpenXR_FOUND)
        target_link_libraries(gevr_runtime PUBLIC OpenXR::openxr_loader)
    else()
        target_link_libraries(gevr_runtime PUBLIC PkgConfig::OPENXR)
    endif()

    # SDL2 turns up in three shapes depending on who found it. Standalone, the
    # config package gives the SDL2::SDL2 imported target. As a subdirectory of
    # the host port, the parent has already run an old-style find_package that
    # sets SDL2_LIBRARY and no target at all -- linking SDL2::SDL2 there fails
    # configure with "target was not found". And on a pkg-config-only system
    # there is PkgConfig::SDL2PC. Check for what actually exists rather than
    # assuming the modern one.
    if(TARGET SDL2::SDL2)
        target_link_libraries(gevr_runtime PUBLIC SDL2::SDL2)
    elseif(SDL2_LIBRARY OR SDL2_LIBRARIES)
        target_include_directories(gevr_runtime PUBLIC ${SDL2_INCLUDE_DIR} ${SDL2_INCLUDE_DIRS})
        target_link_libraries(gevr_runtime PUBLIC ${SDL2_LIBRARY} ${SDL2_LIBRARIES})
    elseif(TARGET PkgConfig::SDL2PC)
        target_link_libraries(gevr_runtime PUBLIC PkgConfig::SDL2PC)
    else()
        message(FATAL_ERROR "gevr_runtime: SDL2 was reported found but exposes neither a target nor a library variable")
    endif()

    # glXGetCurrentContext / wglGetCurrentContext for the graphics binding.
    if(WIN32)
        target_link_libraries(gevr_runtime PUBLIC opengl32)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(gevr_runtime PUBLIC OpenGL::GL)
    endif()

    if(NOT MSVC)
        target_compile_options(gevr_runtime PRIVATE -Wall -Wextra)
        if(GEVR_WERROR)
            target_compile_options(gevr_runtime PRIVATE -Werror)
        endif()
    endif()

    if(GEVR_BUILD_CALIBRATE)
        add_executable(ge007vr-calibrate tools/gevr_calibrate.c)
        target_link_libraries(ge007vr-calibrate PRIVATE gevr_runtime)
    endif()
else()
    message(STATUS
        "gevr: OpenXR and/or SDL2 not found - building the control core and "
        "tests only. Install libopenxr-dev and libsdl2-dev (or the OpenXR SDK "
        "on Windows) for the calibrate harness.")
endif()

# ----------------------------------------------------------------- tests ---

if(GEVR_BUILD_TESTS)
    enable_testing()
    add_executable(gevr_tests tests/test_controls.c)
    target_link_libraries(gevr_tests PRIVATE gevr_core)
    add_test(NAME gevr_controls COMMAND gevr_tests)

    add_executable(gevr_headaim_tests tests/test_headaim.c)
    target_link_libraries(gevr_headaim_tests PRIVATE gevr_core)
    add_test(NAME gevr_headaim COMMAND gevr_headaim_tests)

    # The projection maths only; no renderer and no runtime, so the stereo
    # policy can be checked without a headset or a ROM.
    add_executable(gevr_stereo_tests tests/test_stereo.c shim/gevr_stereo.c)
    target_include_directories(gevr_stereo_tests PRIVATE shim ${CMAKE_CURRENT_SOURCE_DIR}/../port/fast3d)
    target_link_libraries(gevr_stereo_tests PRIVATE gevr_core)
    add_test(NAME gevr_stereo COMMAND gevr_stereo_tests)
endif()

# The shim is compiled as part of the game, not here: it needs the engine's
# headers. This target only proves it stays empty without GE_VR, which is what
# keeps the matching ROM build safe.
add_library(gevr_shim_inert STATIC shim/gevr_shim.c)
target_include_directories(gevr_shim_inert PRIVATE include shim)
