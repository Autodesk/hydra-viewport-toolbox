include(CMakePrintHelpers)

# Function to set a variable if it is not defined and set the cache string.
function(set_if_not_defined VAR_NAME VAR_VALUE VAR_DOC)
    if(NOT DEFINED ${VAR_NAME})
        set(${VAR_NAME} ${VAR_VALUE} CACHE STRING "${VAR_DOC}")
    endif()
endfunction()

# Create a private sub-library target.
#
# Usage:
#   hvt_add_sublibrary(<target>
#       SOURCES <files...>
#       HEADERS <files...>
#   )
#
# NOTE: Sub-libraries are built as OBJECT libraries so the parent library can bake their
# object files in directly (for shared builds). For the Emscripten generator, STATIC libraries
# must be used instead because it needs real archive targets.
function(hvt_add_sublibrary TARGET)
    cmake_parse_arguments(ARG "" "" "SOURCES;HEADERS" ${ARGN})

    if (EMSCRIPTEN)
        add_library(${TARGET} STATIC ${ARG_SOURCES} ${ARG_HEADERS})
    else()
        add_library(${TARGET} OBJECT ${ARG_SOURCES} ${ARG_HEADERS})
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
        foreach(_lib ${ARGN})
            target_sources(${TARGET} PRIVATE $<TARGET_OBJECTS:${_lib}>)
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
