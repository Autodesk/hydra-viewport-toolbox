# Load the base triplet to initialize the variables
include("$ENV{HVT_BASE_TRIPLET_FILE}")

# Customize here

# Unsupported by libjpeg-turbo
# set(VCPKG_OSX_ARCHITECTURES arm64 x86_64)
set(VCPKG_OSX_DEPLOYMENT_TARGET "12.0")

if(PORT STREQUAL "tbb")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
