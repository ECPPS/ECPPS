cmake_minimum_required(VERSION 3.20)
project(ECPPS)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)                                              
    add_compile_options("$<$<CONFIG:Release>:/O2;/GL;/Gw;/GF>")
    add_link_options("$<$<CONFIG:Release>:/LTCG>")
endif()

function(enable_pch target)
    if(MSVC AND EXISTS "${PCH_HEADER}")
        target_precompile_headers(${target} PRIVATE "${PCH_HEADER}")
    endif()
endfunction()

add_subdirectory(compiler)
add_subdirectory(stdlib)
add_subdirectory(testing)
add_subdirectory(examples)