set(PLATFORMLIB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/identify.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platformlib.cpp"
)

add_library(platformlib STATIC)

target_sources(platformlib PUBLIC ${PLATFORMLIB_SOURCES})

target_link_libraries(platformlib PUBLIC ecpps_defaults)

target_include_directories(platformlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_precompile_headers(platformlib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h")
target_link_libraries(platformlib PRIVATE utilitieslib)
