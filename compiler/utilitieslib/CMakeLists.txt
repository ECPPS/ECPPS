add_library(utilitieslib STATIC
    src/SBOVector.cpp include/utilitieslib/SBOVector.h include/utilitieslib/Numbers.h "include/utilitieslib/Queue.h" "include/utilitieslib/RuntimeAssert.h" src/RuntimeAssert.cpp)

target_include_directories(utilitieslib PUBLIC "include/utilitieslib")

target_precompile_headers(utilitieslib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h")
