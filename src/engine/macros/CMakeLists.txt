# src/engine/macros/CMakeLists.txt
#
# Six primary macros and their fan-out math. Header-only: there's nothing
# to link, but we declare a target for IDE visibility and to give callers
# a clean dependency edge.

add_library (sfs_engine_macros INTERFACE)
add_library (SFS::Engine::Macros ALIAS sfs_engine_macros)

target_include_directories (sfs_engine_macros INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries (sfs_engine_macros
    INTERFACE
        SFS::DeterminismFlags
)
