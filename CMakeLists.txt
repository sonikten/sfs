cmake_minimum_required(VERSION 3.22)

# ------------------------------------------------------------------------------
# Stigmergic Field Synthesis (SFS) — top-level build
# ------------------------------------------------------------------------------
# Phase 0 step 2 (per docs/phase-plans/phase-0.md): no targets yet. This file
# establishes the project, language standard, deterministic floating-point
# flags, and the toolchain options that every later phase relies on.
#
# When subsystems start landing in Phase 1, add `add_subdirectory(src/...)`
# calls below in dependency order: dsp → engine/{rng,substrate,agents,harvester}
# → plugin → gui → tools → tests.
# ------------------------------------------------------------------------------

project(SFS
    VERSION 0.1.0
    DESCRIPTION "Stigmergic Field Synthesis — VST3 instrument"
    LANGUAGES C CXX        # C is needed by some JUCE module dependencies
)

# Always emit compile_commands.json so clangd / IDE indexers find headers
# without needing per-developer setup. Lives in the build directory; many
# editors auto-discover it there, but a symlink at the project root is
# more reliable.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Honour visibility presets, position-independent code, modern CMake policies.
if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)   # MSVC runtime library selectable via property
endif()
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)   # FetchContent timestamp behaviour
endif()

# ------------------------------------------------------------------------------
# Language standard — C++20, no extensions (per CLAUDE.md / 08 §3)
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ------------------------------------------------------------------------------
# Project options
# ------------------------------------------------------------------------------
option(SFS_BUILD_TESTS    "Build unit and integration tests" ON)
option(SFS_BUILD_TOOLS    "Build CLI tools (sfs_render, wav_diff)" ON)
option(SFS_BUILD_PLUGIN   "Build the VST3 plug-in target" ON)
option(SFS_BUILD_ECO      "Lower-CPU defaults (per 08 §6 ECO mode)" OFF)
option(SFS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(SFS_INSTALL_PLUGIN_AFTER_BUILD
       "Copy SFS.vst3 to the OS user-level VST3 directory after each build (macOS / Linux / Windows)"
       OFF)

# ------------------------------------------------------------------------------
# Compile flag interface libraries
#
# Two interface targets so the determinism flags can propagate everywhere
# (including TUs that include JUCE headers) without forcing our strict
# `-Werror -Wold-style-cast` posture onto third-party code.
#
#   SFS::DeterminismFlags   — `-fno-fast-math` family. PROPAGATE EVERYWHERE.
#                              Removing any of these breaks the bit-exact
#                              cross-platform contract (CLAUDE.md, 08 §6).
#
#   SFS::StrictWarnings     — `-Wall -Wextra -Werror -Wold-style-cast` etc.
#                              Apply ONLY to first-party TUs (src/dsp, src/engine,
#                              src/plugin/PluginProcessor.cpp etc.). JUCE TUs
#                              and other third-party code use their own posture
#                              (e.g. juce::juce_recommended_warning_flags).
#
# Per-arch SIMD flags live in cmake/SimdConfig.cmake.
# ------------------------------------------------------------------------------
add_library(sfs_determinism_flags INTERFACE)
add_library(SFS::DeterminismFlags ALIAS sfs_determinism_flags)

add_library(sfs_strict_warnings INTERFACE)
add_library(SFS::StrictWarnings ALIAS sfs_strict_warnings)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(sfs_determinism_flags INTERFACE
        -fno-fast-math
        -fno-finite-math-only
        -fno-associative-math
        -fno-reciprocal-math
        -ffp-contract=off
        -fno-unsafe-math-optimizations
    )
    target_compile_options(sfs_strict_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wno-unused-parameter
    )
    if (SFS_WARNINGS_AS_ERRORS)
        target_compile_options(sfs_strict_warnings INTERFACE -Werror)
    endif()

elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(sfs_determinism_flags INTERFACE
        /fp:precise
        /fp:except-
    )
    target_compile_options(sfs_strict_warnings INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /Zc:inline
        /utf-8
        /MP
    )
    if (SFS_WARNINGS_AS_ERRORS)
        target_compile_options(sfs_strict_warnings INTERFACE /WX)
    endif()
    # Static MSVC runtime keeps the determinism story simple (no MSVCR DLL drift).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Backwards-compat alias: most first-party libs want both. Plug-in / GUI / tools
# that include third-party headers should link SFS::DeterminismFlags only.
add_library(sfs_compile_flags INTERFACE)
add_library(SFS::CompileFlags ALIAS sfs_compile_flags)
target_link_libraries(sfs_compile_flags INTERFACE
    SFS::DeterminismFlags
    SFS::StrictWarnings
)

# Defensive: fail the configure if anyone passed -ffast-math via CXXFLAGS.
foreach (_flag "-ffast-math" "/fp:fast")
    if (CMAKE_CXX_FLAGS MATCHES "${_flag}")
        message(FATAL_ERROR
            "Refusing to configure: '${_flag}' is set in CMAKE_CXX_FLAGS. "
            "This breaks the SFS determinism contract (see CLAUDE.md / 08 §6).")
    endif()
endforeach()

# ------------------------------------------------------------------------------
# Subsystem assembly
# ------------------------------------------------------------------------------
# Per-arch SIMD flag helper (target_simd_flags). Header-only — applies flags
# only on the targets that explicitly opt in via the helper function.
include(cmake/SimdConfig.cmake)

# DSP primitives (dm_sin etc.). No external dependencies; safe to always build
# and link from tests.
add_subdirectory(src/dsp)

# Engine subsystems. Each subdirectory creates its own static library and the
# engine glue (added in a later Phase 1 step) links them together. Adding
# subsystems here in dependency order keeps the build narrow when only some
# pieces are needed.
add_subdirectory(src/engine/rng)
add_subdirectory(src/engine/substrate)
add_subdirectory(src/engine/agents)
add_subdirectory(src/engine/macros)
add_subdirectory(src/engine/envelope)
add_subdirectory(src/engine/lfo)
add_subdirectory(src/engine/mod_matrix)
add_subdirectory(src/engine)

# nlohmann/json — header-only, used by the preset loader. Vendored as
# a submodule under third_party/. We add it here (after engine/) because
# src/preset depends on the engine types it persists.
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(third_party/nlohmann_json EXCLUDE_FROM_ALL)
add_subdirectory(src/preset)

# JUCE submodule lives at third_party/JUCE (pinned via tools/check_submodule_pins.sh).
# Only wired in when the plug-in is being built — keeps DSP-only configures fast.
if (SFS_BUILD_PLUGIN)
    add_subdirectory(third_party/JUCE)
    add_subdirectory(src/plugin)
endif()

# Headless tools — depend on JUCE for AudioProcessor + WAV writer + file I/O.
# Gated behind SFS_BUILD_TOOLS so a DSP-only configure stays JUCE-free.
if (SFS_BUILD_TOOLS AND SFS_BUILD_PLUGIN)
    add_subdirectory(tools/sfs_render)
    add_subdirectory(tools/wav_diff)
    add_subdirectory(tools/list_params)
    add_subdirectory(tools/render_presets)
endif()

# Preset audio-hash render harness — JUCE-free, depends only on the
# engine + preset modules.
if (SFS_BUILD_TOOLS)
    add_subdirectory(tools/sfs_preset_render)
endif()

# CPU profile micro-benchmark — JUCE-free, only depends on the engine.
if (SFS_BUILD_TOOLS)
    add_subdirectory(tools/sfs_profile)
endif()

# ------------------------------------------------------------------------------
# Deferred to later Phase 0 steps (recorded here so the order is visible)
# ------------------------------------------------------------------------------
# Phase 0 step 8: include(cmake/DeterminismChecks.cmake)
# Phase 0 step 9: include(cmake/Pluginval.cmake)
# Phase 1+:       add_subdirectory(src/engine/...)
#                 add_subdirectory(tests)

if (SFS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "SFS ${PROJECT_VERSION} — configured for ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD} (extensions: ${CMAKE_CXX_EXTENSIONS})")
message(STATUS "  Warnings as errors: ${SFS_WARNINGS_AS_ERRORS}")
message(STATUS "  ECO mode: ${SFS_BUILD_ECO}")
