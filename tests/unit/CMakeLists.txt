# tests/unit/CMakeLists.txt
#
# Catch2 unit tests for first-party DSP / engine / utility code.

add_executable (sfs_unit_tests
    dm_sin_test.cpp
    rng_philox_test.cpp
    substrate_1d_test.cpp
    agent_pool_test.cpp
    voice_test.cpp
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

# Register each TEST_CASE with CTest so `ctest` runs them individually and
# parallel-friendly.
catch_discover_tests (sfs_unit_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
