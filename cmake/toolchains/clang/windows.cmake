include(${CMAKE_CURRENT_LIST_DIR}/../base.cmake)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_EXE_LINKER_FLAGS -Xlinker --stack:10000000)
