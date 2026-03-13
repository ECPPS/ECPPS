add_library(stdlib-rt SHARED
runtime/dllmain.cpp
)

add_library(stdlib-crtStartup STATIC
crtStartup/crtmain.cpp
)
target_include_directories(stdlib-rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(stdlib-crtStartup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)