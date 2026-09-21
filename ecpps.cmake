# Protect against multiple inclusions
include_policy(SET CMP0025 NEW)
if(_COMPILER_ECPPS_CXX)
  return()
endif()
set(_COMPILER_ECPPS_CXX 1)

# 1. Core Compilation Commands & Output Naming
# This overrides CMake's standard behavior to natively use your /out: syntax
set(CMAKE_CXX_COMPILE_OBJECT "<CMAKE_CXX_COMPILER> <FLAGS> <DEFINES> <INCLUDES> /out:<OBJECT> <SOURCE>")
set(CMAKE_CXX_OUTPUT_EXTENSION ".obj")

# 2. Map Standard Levels to ecpps.exe Flags
# We define how to trigger each C++ standard version
set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "/v:time") # Enforce C++23 flag

# 3. Define Default Standard Settings
# Set C++23 as the baseline default if a user doesn't specify CMAKE_CXX_STANDARD
set(CMAKE_CXX_STANDARD_DEFAULT "23")
set(CMAKE_CXX_STANDARD_REQUIRED_DEFAULT ON)
set(CMAKE_CXX_EXTENSIONS_DEFAULT OFF)

# 4. Enable the Core Compiler Framework Configuration
# This internal macro forces CMake to populate standard variables based on the options above
include(Compiler/CMakeCommonCompilerMacros)

# Inform CMake which standards this compiler driver officially supports
set(CMAKE_CXX_LEVELS 23)
foreach(lang CXX)
  set(CMAKE_${lang}_AVAILABLE_DEFAULTS 23)
endforeach()

# 5. Native Feature Detection & Support Tracking
# Tell CMake that C++23 compiler features are supported natively when -std=c++23 is active
if(NOT CMAKE_CXX_COMPILER_VERSION_PREV RESOLVED)
  set(_CXX_STD_23_FLAGS "/v:time")

  # Register core C++23 language features that CMake tracks
  set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "/v:time")
  set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "/v:time")

  # List of mandatory CMake internal feature keys for C++23
  # This tells CMake it doesn't need to guess if the compiler supports these
  set(CMAKE_CXX23_COMPILE_FEATURES
    cxx_std_23
    cxx_size_t_suffix
    cxx_multiline_strings
    # Add other explicit ecpps-supported C++23 feature tokens here
  )
endif()
