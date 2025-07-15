//-----------------------------------------------------------------------------

#pragma once

//-----------------------------------------------------------------------------

#if defined _MSC_VER

#define PXR_USD_IMPORT_BEGIN \
    __pragma(warning(push)) \
    __pragma(warning(disable:4244)) \
    __pragma(warning(disable:4305)) \
    __pragma(warning(disable:4003)) \
    __pragma(warning(disable:4267))

#define PXR_USD_IMPORT_END \
    __pragma(warning(pop))

#elif __clang__

#define PXR_COMMON_IGNORES           \
    _Pragma("clang diagnostic push") \
    _Pragma("clang diagnostic ignored \"-Wundefined-var-template\"") \
    _Pragma("clang diagnostic ignored \"-Wdeprecated-copy\"") \
    _Pragma("clang diagnostic ignored \"-Wunused-parameter\"") \
    _Pragma("clang diagnostic ignored \"-Wgnu-zero-variadic-macro-arguments\"") \
    _Pragma("clang diagnostic ignored \"-Wmissing-field-initializers\"") \
    _Pragma("clang diagnostic ignored \"-Wunused-private-field\"") \
    _Pragma("clang diagnostic ignored \"-Wgnu-anonymous-struct\"") \
    _Pragma("clang diagnostic ignored \"-Wnested-anon-types\"") \
    _Pragma("clang diagnostic ignored \"-Wextra-semi\"") \
    _Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"") \
    _Pragma("clang diagnostic ignored \"-Wdtor-name\"") \
    _Pragma("clang diagnostic ignored \"-Wshorten-64-to-32\"") \
    _Pragma("clang diagnostic ignored \"-Winconsistent-missing-override\"")

#if __clang_major__ > 11
#define PXR_VERSIONED_IGNORES \
    _Pragma("clang diagnostic ignored \"-Wdeprecated-copy-with-user-provided-copy\"")
#else
#define PXR_VERSIONED_IGNORES
#endif

#define PXR_USD_IMPORT_BEGIN  \
    PXR_COMMON_IGNORES        \
    PXR_VERSIONED_IGNORES

#define PXR_USD_IMPORT_END \
    _Pragma("clang diagnostic pop")

#else

#define PXR_USD_IMPORT_BEGIN
#define PXR_USD_IMPORT_END 

#endif  // defined _MSC_VER

//-----------------------------------------------------------------------------
