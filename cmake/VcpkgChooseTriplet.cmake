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

    set(base_triplet "${arch}-${platform}")

    if(HVT_RELEASE_ONLY_VCPKG_DEPS)
        set(candidate "${base_triplet}-release")
    else()
        set(candidate "${base_triplet}")
    endif()

    # Define root of vcpkg
    set(vcpkg_root "${CMAKE_CURRENT_LIST_DIR}/../externals/vcpkg")
    cmake_path(NORMAL_PATH vcpkg_root)

    # Try primary location
    set(candidate_path "${vcpkg_root}/triplets/${candidate}.cmake")

    # If not found, try community location
    if(NOT EXISTS "${candidate_path}")
        set(community_path "${vcpkg_root}/triplets/community/${candidate}.cmake")

        if(EXISTS "${community_path}")
            set(candidate_path "${community_path}")
        else()
            message(WARNING "Triplet '${candidate}' not found in either 'triplets' or 'triplets/community'. Falling back to ${base_triplet}")
            set(candidate "${base_triplet}")
            set(candidate_path "${vcpkg_root}/triplets/${base_triplet}.cmake")
        endif()
    endif()

    # Get relative path for output message
    file(RELATIVE_PATH relative_triplet_path "${vcpkg_root}/triplets" "${candidate_path}")
    message(STATUS "Automatically selected base triplet ${relative_triplet_path} for the default-customized triplet")

    # This environment variable will be read by our "default-customized" triplet to
    # load the base triplet.
    set(ENV{HVT_BASE_TRIPLET_FILE} "${candidate_path}")
endif()

set(ENV{VCPKG_KEEP_ENV_VARS} HVT_BASE_TRIPLET_FILE)

set(VCPKG_TARGET_TRIPLET "default-customized")