add_library(ecpps_platform INTERFACE)
if(MSVC)
   target_link_libraries(ecpps_platform INTERFACE dbghelp)
endif()
