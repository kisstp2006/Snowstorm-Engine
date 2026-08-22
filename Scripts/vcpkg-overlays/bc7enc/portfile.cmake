vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

# Local overlay port: bc7enc has no upstream vcpkg port. Fetched over the GIT protocol rather than
# vcpkg_from_github, for the same reason the polyclipping overlay next door does: behind a filtering
# proxy the GitHub codeload archive endpoint returns HTTP 403, while git against github.com is allowed.
# Pinned to an immutable commit.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/richgel999/bc7enc
    REF f66c2e489b07138f2673a2fb3d27c1aa1d565c48
)

# Upstream's CMakeLists builds a command-line test tool; replace it with one that builds just the encoder.
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME bc7enc CONFIG_PATH share/bc7enc)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
