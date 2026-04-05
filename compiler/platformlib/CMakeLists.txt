set(PLATFORMLIB_SOURCES
    "src/identify.cpp"
    "src/platformlib.cpp"
)

add_library(platformlib STATIC ${PLATFORMLIB_SOURCES})

target_link_libraries(platformlib PUBLIC ecpps_defaults)

target_include_directories(platformlib PRIVATE "include/platformlib")
target_include_directories(platformlib INTERFACE "include")

target_precompile_headers(platformlib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h")
target_link_libraries(platformlib PRIVATE utilitieslib)
