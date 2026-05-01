add_library(ecpps_optimisations INTERFACE)

if(MSVC)
    target_compile_options(ecpps_optimisations INTERFACE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/O2>
    )
else()
    target_compile_options(ecpps_optimisations INTERFACE
	     $<$<CONFIG:Debug>:-Og>
        $<$<CONFIG:Release>:-O3 -march=x86-64-v3>
        $<$<CONFIG:RelWithDebInfo>:-O3 -march=x86-64-v3>
    )
endif()
