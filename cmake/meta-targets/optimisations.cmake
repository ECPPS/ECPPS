add_library(ecpps_optimisations INTERFACE)

if(MSVC)
    target_compile_options(ecpps_optimisations INTERFACE
	    $<$<CONFIG:Debug>:/Z7 /Od /sdl /GS>
        $<$<CONFIG:RelWithDebInfo>:/O2 /Z7 /Gy /Gw /Oi>
        $<$<CONFIG:Release>:/O2 /Z7 /Gy /Oi /GF /Ob3 /Gw>
    )
    target_link_options(ecpps_optimisations INTERFACE
        $<$<CONFIG:Debug>:/DEBUG:FASTLINK>
        $<$<CONFIG:RelWithDebInfo>:/DEBUG:FASTLINK /OPT:REF /OPT:ICF>
        $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>
    )
else()
    target_compile_options(ecpps_optimisations INTERFACE
	    $<$<CONFIG:Debug>:-g -gsplit-dwarf>
        $<$<CONFIG:RelWithDebInfo>:-O3 -march=x86-64-v3 -g -gsplit-dwarf -ffunction-sections -fdata-sections>
        $<$<CONFIG:Release>:-O3 -march=x86-64-v3 -ffunction-sections -fdata-sections>
    )
    target_link_options(ecpps_optimisations INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:-Wl,--gc-sections>
        $<$<CONFIG:Release,Release>:-Wl,--gc-sections>
    )
endif()
