# tests/contract/CMakeLists.txt
#
# Sonic-corner contract tests per sfs-spec/08 §2.3. One executable per
# corner so each can be invoked independently and SKIP-marker logic stays
# clean (Phase 1 ships drone + pitched; organic + glitch arrive in Phase 2).

add_executable (sfs_contract_drone
    drone_test.cpp
)

target_link_libraries (sfs_contract_drone
    PRIVATE
        SFS::Engine
        SFS::DeterminismFlags
        SFS::StrictWarnings
        Catch2::Catch2WithMain
)

set_target_properties (sfs_contract_drone PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_test (NAME contract_drone
          COMMAND sfs_contract_drone --reporter compact
          WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Phase 1 plan §3 contract gate matrix:
#   pitched    arrives in a follow-up commit (needs YIN / autocorrelation)
#   organic    SKIP marker (Phase < 2)
#   glitch     SKIP marker (Phase < 2)
