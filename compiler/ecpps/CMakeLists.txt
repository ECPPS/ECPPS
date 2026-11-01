include(libecpps.cmake)

set(CLI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/main.cpp"
)

add_executable(ecpps ${CLI_SOURCES})
target_link_libraries(ecpps PRIVATE libecpps ecpps_defaults)
set_target_properties(ecpps PROPERTIES CXX_STANDARD 23)
target_precompile_headers(ecpps PRIVATE
                          "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h")

install(TARGETS ecpps RUNTIME DESTINATION bin COMPONENT compiler)
install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/stdlib/include/"
    DESTINATION include
    COMPONENT compiler
    PATTERN ".clang-tidy" EXCLUDE
)
