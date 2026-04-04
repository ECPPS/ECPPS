add_library(ecpps_pgo INTERFACE)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND ENABLE_PGO)
    if(PGO_REBUILD)
        target_compile_options(ecpps_pgo INTERFACE
            $<$<CONFIG:Release>:-fprofile-create>
            $<$<CONFIG:RelWithDebInfo>:-fprofile-create>
        )
        target_link_options(ecpps_pgo INTERFACE
            $<$<CONFIG:Release>:-fprofile-create>
            $<$<CONFIG:RelWithDebInfo>:-fprofile-create>
        )
    else()
        target_compile_options(ecpps_pgo INTERFACE
            $<$<CONFIG:Release>:-fprofile-use>
            $<$<CONFIG:RelWithDebInfo>:-fprofile-use>
        )
        target_link_options(ecpps_pgo INTERFACE
            $<$<CONFIG:Release>:-fprofile-use>
            $<$<CONFIG:Release>:-fprofile-correction>
            $<$<CONFIG:RelWithDebInfo>:-fprofile-use>
            $<$<CONFIG:RelWithDebInfo>:-fprofile-correction>
        )
    endif()
endif()
