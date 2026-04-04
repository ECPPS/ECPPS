add_library(ecpps_lto INTERFACE)

if(MSVC)
    target_compile_options(ecpps_lto INTERFACE
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:RelWithDebInfo>:/GL>
    )
    target_link_options(ecpps_lto INTERFACE
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:RelWithDebInfo>:/LTCG>
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ecpps_lto INTERFACE
        $<$<CONFIG:Release>:-flto=thin>
        $<$<CONFIG:RelWithDebInfo>:-flto=thin>
    )
    target_link_options(ecpps_lto INTERFACE
        $<$<CONFIG:Release>:-flto=thin>
        $<$<CONFIG:RelWithDebInfo>:-flto=thin>
    )
endif()
