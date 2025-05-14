message(STATUS "Using custom overlay glew port!")

if(VCPKG_TARGET_IS_LINUX)
    message(WARNING "${PORT} may require: libxmu-dev, libxi-dev, libgl-dev.")
endif()

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://git.autodesk.com/autodesk-forks/glew-cmake"
    REF "79985c4a52f7e7dc0ac178d03cc87df99603d643"
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/build/cmake"
    OPTIONS
        -DBUILD_UTILS=OFF
        -DBUILD_SHARED_LIBS=OFF # or ON for shared
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/glew)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

# Licensing
file(INSTALL "${SOURCE_PATH}/LICENSE.txt"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)