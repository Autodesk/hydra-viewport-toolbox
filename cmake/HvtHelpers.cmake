include(CMakePrintHelpers)

# Function to set a variable if it is not defined and set the cache string.
function(set_if_not_defined VAR_NAME VAR_VALUE VAR_DOC)
    if(NOT DEFINED ${VAR_NAME})
        set(${VAR_NAME} ${VAR_VALUE} CACHE STRING "${VAR_DOC}")
    endif()
endfunction()

# Route compilation through a compiler cache (ccache or sccache) when one is installed.
#
# Usage:
#   hvt_setup_compiler_cache()
#
# NOTE: A compiler cache does nothing for a clean build; it pays off when rebuilding after
# switching branches or reverting a change, where it turns a recompile into a cache hit.
# NOTE: A launcher already set by the caller (a parent project, or -DCMAKE_CXX_COMPILER_LAUNCHER
# on the command line) is left alone, but still gets the precompiled header adjustment below.
# NOTE: Nothing is detected when HVT is built as a sub-project. Whether to use a compiler cache is
# the top-level project's decision, and CMAKE_CXX_COMPILER_LAUNCHER would apply to all of it.
function(hvt_setup_compiler_cache)
    if (NOT ENABLE_COMPILER_CACHE)
        return()
    endif()

    if (NOT CMAKE_CXX_COMPILER_LAUNCHER)
        if (NOT PROJECT_IS_TOP_LEVEL)
            return()
        endif()

        find_program(HVT_COMPILER_CACHE NAMES ccache sccache)
        if (NOT HVT_COMPILER_CACHE)
            return()
        endif()

        # Set in the caller's directory scope rather than the cache, so that the setting is
        # inherited by the sub-directories without being written to a parent project's cache.
        set(CMAKE_CXX_COMPILER_LAUNCHER "${HVT_COMPILER_CACHE}" PARENT_SCOPE)
    endif()

    # A compiler cache and precompiled headers do not combine out of the box. Clang stamps the
    # precompiled header with a timestamp, which changes the input on every rebuild and defeats
    # the cache; -fno-pch-timestamp makes the content alone decide. Callers additionally need
    # CCACHE_SLOPPINESS to include pch_defines and time_macros; see docs/build.md.
    if (ENABLE_PRECOMPILED_HEADERS AND CMAKE_CXX_COMPILER_ID MATCHES "[C|c]lang")
        add_compile_options("SHELL:-Xclang -fno-pch-timestamp")
    endif()
endfunction()

# Create a private sub-library target.
#
# Usage:
#   hvt_add_sublibrary(<target>
#       SOURCES <files...>
#       HEADERS <files...>
#       [NO_PRECOMPILED_HEADERS]
#   )
#
# Pass NO_PRECOMPILED_HEADERS for a sub-library that cannot consume the shared C++ precompiled
# header, e.g. one whose sources are compiled as Objective-C++.
#
# NOTE: Sub-libraries are built as OBJECT libraries so the parent library can bake their
# object files in directly (for shared builds). For the Emscripten generator, STATIC libraries
# must be used instead because it needs real archive targets.
function(hvt_add_sublibrary TARGET)
    cmake_parse_arguments(ARG "NO_PRECOMPILED_HEADERS" "" "SOURCES;HEADERS" ${ARGN})

    if (EMSCRIPTEN)
        add_library(${TARGET} STATIC ${ARG_SOURCES} ${ARG_HEADERS})
    else()
        add_library(${TARGET} OBJECT ${ARG_SOURCES} ${ARG_HEADERS})
    endif()

    if (NOT ARG_NO_PRECOMPILED_HEADERS)
        hvt_enable_precompiled_header(${TARGET})
    endif()
endfunction()

# Make a target use the shared precompiled header, if precompiled headers are enabled.
#
# Usage:
#   hvt_enable_precompiled_header(<target>)
#
# The first target to call this compiles source/pch.h; every later target reuses that same
# precompiled header instead of compiling its own. Compiling it once rather than once per
# sub-library saves both build time and about a gigabyte of disk in a debug build tree.
#
# NOTE: Reuse requires the sharing targets to compile with the same preprocessor definitions,
# which is true today because every sub-library pulls the same definitions from the OpenUSD
# targets it links. A sub-library that needs different definitions has to be given
# NO_PRECOMPILED_HEADERS; otherwise the compiler rejects the precompiled header and says which
# definition differs.
function(hvt_enable_precompiled_header TARGET)
    if (NOT ENABLE_PRECOMPILED_HEADERS)
        return()
    endif()

    get_property(_donor GLOBAL PROPERTY HVT_PCH_DONOR)
    if (_donor)
        target_precompile_headers(${TARGET} REUSE_FROM ${_donor})
    else()
        target_precompile_headers(${TARGET} PRIVATE "${_HVT_SOURCE_DIR}/pch.h")
        set_property(GLOBAL PROPERTY HVT_PCH_DONOR ${TARGET})
    endif()
endfunction()

# Add a private sub-library target to the given export set if needed.
#
# Usage:
#   hvt_install_sublibrary(<target> <export_name>)
#
# NOTE: Sub-libraries are private and are normally baked into the parent library, so they are
# only installed/exported for the Emscripten generator which needs access to every target.
function(hvt_install_sublibrary TARGET EXPORT_NAME)
    if (EMSCRIPTEN)
        install(TARGETS ${TARGET}
            EXPORT ${EXPORT_NAME}Targets
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib
        )
    endif()
endfunction()

# Embed the given private sub-libraries into a parent target.
#
# Usage:
#   hvt_embed_sublibraries(<target> <sublib>...)
#
# NOTE: For the Emscripten generator the sub-libraries are linked in privately. For every other
# generator the sub-libraries' object files are baked directly into the parent target.
function(hvt_embed_sublibraries TARGET)
    if (EMSCRIPTEN)
        # Add the private sub-libraries to the target.
        target_link_libraries(${TARGET} PRIVATE ${ARGN})
    else()
        # Bake the private sub-libraries' object files directly into the parent target.
        # BUILD_INTERFACE keeps TARGET_OBJECTS out of the installed export (consumers link libhvt only).
        foreach(_lib ${ARGN})
            target_link_libraries(${TARGET} PRIVATE "$<BUILD_INTERFACE:$<TARGET_OBJECTS:${_lib}>>")
        endforeach()
    endif()
endfunction()

# Function to detect if Python debug libraries are available.
function(check_python_debug_libraries result_var)
    # Assume no Python debug libraries are available by default.
    set(${result_var} FALSE PARENT_SCOPE)

    # Try to find Python3, if the parent project hasn't already found it.
    if(NOT TARGET Python3::Python)
        find_package(Python3 COMPONENTS Development QUIET)
    endif()

    # Check if the Python3::Python target has a debug location set.
    if(TARGET Python3::Python)
        get_target_property(python_debug_lib Python3::Python IMPORTED_LOCATION_DEBUG)

        if(python_debug_lib AND EXISTS "${python_debug_lib}")
            set(${result_var} TRUE PARENT_SCOPE)
        endif()
    endif()
endfunction()
