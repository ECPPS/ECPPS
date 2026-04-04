set(CATCH2_DOWNLOAD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/catch2")
set(CATCH2_HPP "${CATCH2_DOWNLOAD_DIR}/catch_amalgamated.hpp")
set(CATCH2_CPP "${CATCH2_DOWNLOAD_DIR}/catch_amalgamated.cpp")

if(NOT EXISTS "${CATCH2_HPP}" OR NOT EXISTS "${CATCH2_CPP}")
     message(STATUS "Downloading Catch2 v3.5.2 amalgamated files to ${CATCH2_DOWNLOAD_DIR}...")
     file(MAKE_DIRECTORY "${CATCH2_DOWNLOAD_DIR}")
     
     file(DOWNLOAD
          "https://github.com/catchorg/Catch2/releases/download/v3.5.2/catch_amalgamated.hpp"
          "${CATCH2_HPP}"
          SHOW_PROGRESS
          STATUS download_status_hpp
     )
     
     file(DOWNLOAD
          "https://github.com/catchorg/Catch2/releases/download/v3.5.2/catch_amalgamated.cpp"
          "${CATCH2_CPP}"
          SHOW_PROGRESS
          STATUS download_status_cpp
     )
     
     list(GET download_status_hpp 0 status_code_hpp)
     list(GET download_status_cpp 0 status_code_cpp)
     
     if(NOT status_code_hpp EQUAL 0 OR NOT status_code_cpp EQUAL 0)
          message(FATAL_ERROR "Failed to download Catch2 amalgamated files")
     endif()
     
     message(STATUS "Catch2 downloaded successfully")
endif()

add_library(Catch2 STATIC "${CATCH2_CPP}")
target_include_directories(Catch2 SYSTEM PUBLIC "${CATCH2_DOWNLOAD_DIR}")
target_compile_features(Catch2 PUBLIC cxx_std_17)

target_compile_options(Catch2 PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
    $<$<CXX_COMPILER_ID:Clang>:-w>
)

add_library(Catch2::Catch2WithMain ALIAS Catch2)
