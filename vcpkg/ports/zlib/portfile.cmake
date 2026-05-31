vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO zlib-ng/zlib-ng
    REF "${VERSION}"
    SHA512 e2057c764f1d5aaee738edee7e977182c5b097e3c95489dcd8de813f237d92a05daaa86d68d44b331d9fec5d1802586a8f6cfb658ba849874aaa14e72a8107f5
    HEAD_REF develop
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DZLIB_COMPAT=ON
        -DZLIB_ALIASES=ON
        -DWITH_GZFILEOP=ON
        -DINSTALL_UTILS=OFF
        -DBUILD_TESTING=OFF
        -DZLIB_ENABLE_TESTS=OFF
        -DWITH_GTEST=OFF
        -DWITH_FUZZERS=OFF
        -DWITH_BENCHMARKS=OFF
        -DWITH_BENCHMARK_APPS=OFF
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/ZLIB)
vcpkg_fixup_pkgconfig()

file(INSTALL "${CURRENT_PORT_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${CURRENT_PORT_DIR}/vcpkg-cmake-wrapper.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)
