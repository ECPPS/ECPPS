add_library(utilitieslib STATIC
    "src/SBOVector.cpp"
    "src/RuntimeAssert.cpp"
)

target_link_libraries(utilitieslib PUBLIC ecpps_defaults)

target_include_directories(utilitieslib PUBLIC "include/utilitieslib")

target_precompile_headers(utilitieslib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h")
