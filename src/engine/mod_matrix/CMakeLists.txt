# src/engine/mod_matrix/CMakeLists.txt
#
# Modulation matrix — header-only, evaluated once per block by Voice.
# 16 slots × source enum × destination enum + signed depth.

add_library (sfs_engine_mod_matrix INTERFACE)
add_library (SFS::Engine::ModMatrix ALIAS sfs_engine_mod_matrix)

target_include_directories (sfs_engine_mod_matrix INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries (sfs_engine_mod_matrix
    INTERFACE
        SFS::DeterminismFlags
)
