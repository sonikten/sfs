# tools/sfs_profile/CMakeLists.txt
#
# Per-voice CPU profile micro-benchmark. Phase 1 §9 step 14.

add_executable (sfs_profile
    main.cpp
)

target_link_libraries (sfs_profile
    PRIVATE
        SFS::Engine
        SFS::DeterminismFlags
        SFS::StrictWarnings
)

set_target_properties (sfs_profile PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
