if(NOT HVT_BASE_TRIPLET_FILE)
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

    set(HVT_BASE_TRIPLET_FILE "${candidate_path}")
endif()

# We will prepend the contents of the file given by HVT_BASE_TRIPLET_FILE to
# CustomTriplet.cmake.in to provide the base configuration. This new file will
# be placed in the binary directory, and found by vcpkg. This way we can
# customize existing triplets without needing to duplicate them manually.

cmake_path(GET HVT_BASE_TRIPLET_FILE STEM base_triplet_name)
# Add an "-hvt" suffix to make it clear it's a customized triplet
set(hvt_triplet_name "${base_triplet_name}-hvt")
set(hvt_triplet_file "${CMAKE_CURRENT_BINARY_DIR}/overlay-triplets/${hvt_triplet_name}.cmake")

file(READ "${HVT_BASE_TRIPLET_FILE}" HVT_BASE_TRIPLET_CONTENTS)
configure_file("${CMAKE_CURRENT_LIST_DIR}/CustomTriplet.cmake.in" "${hvt_triplet_file}" @ONLY)

# Tell vcpkg how to find our custom triplet
set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_BINARY_DIR}/overlay-triplets")
set(VCPKG_TARGET_TRIPLET "${hvt_triplet_name}")
