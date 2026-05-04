# tests/CMakeLists.txt
#
# Unit + integration test tree. Catch2 v3 (third_party/Catch2, pinned by
# tools/check_submodule_pins.sh).
#
# To build only tests, use:
#   cmake -B build -DSFS_BUILD_TESTS=ON -DSFS_BUILD_PLUGIN=OFF -DSFS_BUILD_TOOLS=OFF
#
# Subdirs:
#   unit/          per-class unit tests, fast, no audio I/O
#   integration/   end-to-end render tests (Phase 1+)
#   contract/      sonic-corner contract tests (Phase 1+)
#   determinism/   cross-platform hash + per-block divergence (Phase 0+)

include(CTest)

# Catch2 v3 lives at third_party/Catch2 (pinned to v3.7.1 via the submodule
# pin script). add_subdirectory pulls in the targets Catch2::Catch2 and
# Catch2::Catch2WithMain plus the Catch CMake helpers.
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/Catch2 ${CMAKE_BINARY_DIR}/_deps/catch2)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/third_party/Catch2/extras)
include(Catch)

add_subdirectory(unit)
