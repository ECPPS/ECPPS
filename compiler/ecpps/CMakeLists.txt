include(libecpps.cmake)

set(CLI_SOURCES
    "src/cli/main.cpp"
)

add_executable(ecpps ${CLI_SOURCES})
target_link_libraries(ecpps PRIVATE libecpps ecpps_defaults)
set_target_properties(ecpps PROPERTIES CXX_STANDARD 23)
target_precompile_headers(ecpps PRIVATE
                          "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h")
