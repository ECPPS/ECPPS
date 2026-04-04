add_library(ecpps_warnings INTERFACE)

if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
     target_compile_options(ecpps_warnings INTERFACE /W4)
else()
     target_compile_options(ecpps_warnings INTERFACE
          -Wall
          -Wextra
     )
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
     target_compile_options(ecpps_warnings INTERFACE
          -Weverything
          -Werror

          -Wno-padded
          -Wno-unused-parameter
          -Wno-unused-variable
          -Wno-unused-function
          -Wno-c++98-compat
          -Wno-c++20-compat
          -Wno-c++98-compat-pedantic
          -Wno-pre-c++17-compat
          -Wno-pre-c++23-compat
          -Wno-global-constructors
          -Wno-exit-time-destructors
          -Wno-c++98-c++11-compat-binary-literal
          -Wno-source-uses-openmp
          -Wno-covered-switch-default
          -Wno-missing-noreturn
          -Wno-shadow-field-in-constructor
          -Wno-shadow-uncaptured-local
          -Wno-nonportable-system-include-path
          -Wno-sign-conversion
          -Wno-switch-default
          -Wno-unsafe-buffer-usage
          -Wno-nrvo
          -Wno-ctad-maybe-unsupported
          -Wno-shorten-64-to-32
          -Wno-implicit-int-conversion
          -Wno-undefined-reinterpret-cast
          -Wno-switch-enum
          -Wno-comma
          -Wno-unused-macros
          -Wno-potentially-evaluated-expression
     )
endif()
