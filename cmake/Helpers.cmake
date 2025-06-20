include(CMakePrintHelpers)

# Function to set a variable if it is not defined and set the cache string.
function(set_if_not_defined VAR_NAME VAR_VALUE VAR_DOC)
    if(NOT DEFINED ${VAR_NAME})
        set(${VAR_NAME} ${VAR_VALUE} CACHE STRING "${VAR_DOC}")
    endif()
endfunction()
