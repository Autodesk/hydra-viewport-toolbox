if(NOT DEFINED ENV{HVT_BASE_TRIPLET_FILE})
    # Load a default triplet to initialize the variables.
    # This assumes that we're not cross-compiling; that we want
    # to target the same arch as the host system. If we need to
    # cross-compile then we should use a target arch variable set
    # by the user or a preset.
    # See for processor names: https://stackoverflow.com/a/70498851/22814152
    if(APPLE)
        if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL x86_64)
            set(arch x64)
        elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL arm64)
            set(arch arm64)
        endif()
        set(platform osx)
    elseif(UNIX) # UNIX AND NOT APPLE == LINUX
        if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL x86_64)
            set(arch x64)
        elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL aarch64)
            set(arch arm64)
        endif()
        set(platform linux)
    elseif(WIN32)
        if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL AMD64)
            set(arch x64)
        elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL ARM64)
            set(arch arm64)
        endif()
        set(platform windows)
    endif()

    if(NOT arch OR NOT platform)
        message(FATAL_ERROR "Unsupported platform: ${CMAKE_HOST_SYSTEM_PROCESSOR}-${CMAKE_HOST_SYSTEM_NAME}."
            " Add it to \"${CMAKE_CURRENT_LIST_DIR}\" if required.")
    endif()

    message(STATUS "Automatically selected base triplet ${arch}-${platform} for the default-customized triplet")

    # This environment variable will be read by our "default-customized" triplet to
    # load the base triplet.
    set(base_triplet_path "${CMAKE_TOOLCHAIN_FILE}/../../../triplets/${arch}-${platform}.cmake")
    cmake_path(NORMAL_PATH base_triplet_path)
    set(ENV{HVT_BASE_TRIPLET_FILE} "${base_triplet_path}")
endif()

set(ENV{VCPKG_KEEP_ENV_VARS} HVT_BASE_TRIPLET_FILE)

set(VCPKG_TARGET_TRIPLET "default-customized")
