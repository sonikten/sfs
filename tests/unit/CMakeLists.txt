# tests/unit/CMakeLists.txt
#
# Catch2 unit tests for first-party DSP / engine / utility code.

add_executable (sfs_unit_tests
    dm_sin_test.cpp
    dm_primitives_test.cpp
    rng_philox_test.cpp
    substrate_1d_test.cpp
    agent_pool_test.cpp
    voice_test.cpp
    no_alloc_test.cpp
    denormal_flush_test.cpp
)

target_link_libraries (sfs_unit_tests
    PRIVATE
        SFS::Dsp
        SFS::Engine::Rng
        SFS::Engine::Substrate
        SFS::Engine::Agents
        SFS::Engine
        SFS::DeterminismFlags
        SFS::StrictWarnings
        Catch2::Catch2WithMain
)

set_target_properties (sfs_unit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Register sfs_unit_tests as a single CTest entry. We deliberately don't use
# catch_discover_tests because Catch2's POST_BUILD discovery misparses test
# names that contain `[`, `(`, `,` or `)` — every nontrivial test name in
# this project triggers it (e.g. "Philox4x32Stream::nextFloat01 is in [0, 1)").
# The single-binary registration runs all 33 cases / 4395 assertions in one
# invocation; granular per-test reporting still happens via Catch2's own
# output, just not as separate CTest entries.
add_test (NAME sfs_unit_tests
          COMMAND sfs_unit_tests --reporter compact
          WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
