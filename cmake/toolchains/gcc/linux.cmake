include(${CMAKE_CURRENT_LIST_DIR}/../base.cmake)

set(GCC_VERSION "" CACHE STRING "GCC version")

if(GCC_VERSION)
   set(CMAKE_C_COMPILER "gcc-${GCC_VERSION}")
   set(CMAKE_CXX_COMPILER "g++-${GCC_VERSION}")
else()
   set(CMAKE_C_COMPILER gcc)
   set(CMAKE_CXX_COMPILER g++)
endif()
