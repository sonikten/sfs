# tests/contract/CMakeLists.txt
#
# Sonic-corner contract tests per sfs-spec/08 §2.3. One executable per
# corner so each can be invoked independently and SKIP-marker logic stays
# clean (Phase 1 ships drone + pitched; organic + glitch arrive in Phase 2).

function (sfs_add_contract_target name source)
    add_executable (${name} ${source})
    target_link_libraries (${name}
        PRIVATE
            SFS::Engine
            SFS::DeterminismFlags
            SFS::StrictWarnings
            Catch2::Catch2WithMain
    )
    set_target_properties (${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test (NAME ${name}
              COMMAND ${name} --reporter compact
              WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endfunction()

sfs_add_contract_target (sfs_contract_drone   drone_test.cpp)
sfs_add_contract_target (sfs_contract_pitched pitched_test.cpp)

# Phase 1 plan §3 contract gate matrix:
#   organic    SKIP marker (Phase < 2)
#   glitch     SKIP marker (Phase < 2)
