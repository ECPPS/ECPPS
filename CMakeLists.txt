cmake_minimum_required(VERSION 3.20)
project(ECPPS)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(compiler)
add_subdirectory(stdlib)
add_subdirectory(testing)
add_subdirectory(examples)