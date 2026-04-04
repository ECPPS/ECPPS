add_library(ecpps_options INTERFACE)

target_compile_definitions(ecpps_options INTERFACE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ecpps_options INTERFACE
        /permissive-
        /Zc:__cplusplus
        /Zc:inline
        /Zc:throwingNew
        /Zc:preprocessor
        /Zc:lambda
        /Zc:externConstexpr
        /Zc:referenceBinding
        /Zc:sizedDealloc
        /Zc:strictStrings
        /Zc:templateScope
        /EHsc
    )
endif()

add_library(ecpps_defaults INTERFACE)

target_link_libraries(ecpps_defaults INTERFACE
    ecpps_options
    ecpps_warnings
    ecpps_optimisations
    ecpps_lto
    ecpps_pgo
    ecpps_mimalloc
)
