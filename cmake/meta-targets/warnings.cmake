add_library(ecpps_warnings INTERFACE)

if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
     target_compile_options(ecpps_warnings INTERFACE /W4 /WX)
     target_link_options(ecpps_warnings INTERFACE /WX)
else()
     target_compile_options(ecpps_warnings INTERFACE
          -Wall
          -Wextra
     )
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
     target_compile_options(ecpps_warnings INTERFACE
          # Initial
          -Weverything
          -Werror

          # Useless compatibility warnings
          -Wno-c++98-compat
          -Wno-c++20-compat
          -Wno-pre-c++17-compat
          -Wno-pre-c++23-compat
          -Wno-c++98-compat-pedantic
          -Wno-c++98-c++11-compat-binary-literal

          # Don't care
          -Wno-global-constructors
          -Wno-exit-time-destructors
          -Wno-source-uses-openmp
          -Wno-missing-noreturn
          -Wno-unknown-warning-option # clang-cl doesn't support -Wno-nvro

          # Genually stupid
          -Wno-nonportable-system-include-path
          -Wno-potentially-evaluated-expression
          -Wno-shadow-field-in-constructor
          -Wno-comma
          -Wno-padded # good intentions, but way too many false positives

          # Integers
          -Wno-shorten-64-to-32
          -Wno-implicit-int-conversion

          # Other
          -Wno-covered-switch-default
          -Wno-shadow-uncaptured-local
          -Wno-switch-default
          -Wno-unsafe-buffer-usage
          -Wno-nrvo
          -Wno-ctad-maybe-unsupported
          -Wno-undefined-reinterpret-cast
          -Wno-switch-enum
          -Wno-unused-macros
     )
endif()
