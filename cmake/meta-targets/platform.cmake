add_library(ecpps_platform INTERFACE)
if(WIN32)
   target_link_libraries(ecpps_platform INTERFACE dbghelp)
endif()
