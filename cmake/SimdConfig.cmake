# cmake/SimdConfig.cmake
#
# Per-architecture SIMD compile flag application for the SFS audio kernels.
# Specified in sfs-spec/08_implementation_roadmap.md §6.
#
# Usage from a CMake target:
#   add_library(sfs_dsp_simd OBJECT kernels_simd.cpp)
#   target_simd_flags(sfs_dsp_simd)
#
# Critical determinism notes:
#   * `-mavx2` is applied ONLY to x86_64 TUs. ARM64 builds must never see it
#     (they would fail to compile).
#   * Apple universal binaries use `-Xarch_x86_64` so the AVX2 flag attaches
#     only to the x86_64 architecture slice. CMAKE_OSX_ARCHITECTURES is the
#     mechanism (set externally).
#   * AVX2 kernel TUs must be isolated; the engine probes the CPU at startup
#     and falls back to SSE2 paths on machines without AVX2 (Phase 1+).
#   * If SFS_BUILD_ECO=ON, AVX2 is suppressed entirely (SSE2 / NEON only) for
#     older-CPU distribution. ECO mode still defines SFS_SIMD_X86 / SFS_SIMD_NEON
#     so the kernel selection logic compiles, but the `-mavx2` flag is omitted.
#
# This file MUST stay tiny and must not introduce any non-deterministic flag
# (anything that perturbs floating-point semantics belongs in CMakeLists.txt
# under the SFS::CompileFlags interface, not here).

include_guard(GLOBAL)

function(target_simd_flags target)
    set(_is_x86 FALSE)
    set(_is_arm FALSE)
    set(_is_apple_universal FALSE)

    # Detect target architecture(s). On Apple, CMAKE_OSX_ARCHITECTURES may list
    # multiple slices (universal); otherwise we use CMAKE_SYSTEM_PROCESSOR.
    if (APPLE AND DEFINED CMAKE_OSX_ARCHITECTURES AND CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES _n_archs)
        foreach (_arch IN LISTS CMAKE_OSX_ARCHITECTURES)
            if (_arch STREQUAL "x86_64")
                set(_is_x86 TRUE)
            elseif (_arch STREQUAL "arm64")
                set(_is_arm TRUE)
            endif()
        endforeach()
        if (_n_archs GREATER 1)
            set(_is_apple_universal TRUE)
        endif()
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|x64")
        set(_is_x86 TRUE)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
        set(_is_arm TRUE)
    else()
        message(WARNING
            "target_simd_flags(${target}): unknown architecture "
            "'${CMAKE_SYSTEM_PROCESSOR}' — no SIMD flags applied. "
            "The audio kernels will fall back to scalar paths.")
    endif()

    # ---------- x86_64 ----------
    if (_is_x86)
        target_compile_definitions(${target} PRIVATE SFS_SIMD_X86=1)
        if (NOT SFS_BUILD_ECO)
            if (_is_apple_universal)
                # Universal binary: attach -mavx2 only to the x86_64 slice.
                target_compile_options(${target} PRIVATE
                    -Xarch_x86_64 -mavx2
                    -Xarch_x86_64 -mfma)
            elseif (MSVC)
                target_compile_options(${target} PRIVATE /arch:AVX2)
            else()
                target_compile_options(${target} PRIVATE -mavx2 -mfma)
            endif()
            target_compile_definitions(${target} PRIVATE SFS_HAS_AVX2_BUILD=1)
        else()
            # ECO mode: SSE2 only. SSE2 is the x86_64 baseline so no extra flag
            # is required on Unix; MSVC implicitly enables it for x64.
            target_compile_definitions(${target} PRIVATE SFS_BUILD_ECO=1)
        endif()
    endif()

    # ---------- ARM64 (NEON is implicit; no flag) ----------
    if (_is_arm)
        target_compile_definitions(${target} PRIVATE SFS_SIMD_NEON=1)
        if (SFS_BUILD_ECO)
            target_compile_definitions(${target} PRIVATE SFS_BUILD_ECO=1)
        endif()
    endif()
endfunction()
