# Source sets
set(PARSING_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Parsing/AST.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Parsing/ASTContext.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Parsing/Preprocessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Parsing/SourceMap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Parsing/Tokeniser.cpp"
)

set(TYPESYSTEM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/TypeSystem/ArithmeticTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/TypeSystem/CompoundTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/TypeSystem/TypeBase.cpp"
)

set(BACKEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CodeGeneration/Emitters/x86_64/Opcodes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CodeGeneration/Emitters/x86_64.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CodeGeneration/CodeEmitter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CodeGeneration/Nodes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CodeGeneration/PseudoAssembly.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CodeGeneration/virtualAllocator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CodeGeneration/AbstractNodes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Machine/Vendor/aARM/ISA.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Machine/Vendor/ax86/ISA.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Machine/ABI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Machine/Machine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Machine/Mangling.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Machine/Encoders/InstructionEncoder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Linker/CoffLinker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Linker/dllHelp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Linker/Linker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Linker/PE.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Linker/WindowsLinker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Execution/IR.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Execution/NodeBase.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Execution/Entities.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Execution/Context.cpp"
)
include(src/Machine/Encoders/Backends/sources.cmake)

set(SHARED_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/BumpAllocator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/Config.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/Diagnostics.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/Error.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/mimalloc.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Debugger/Debuggers/Win64.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Debugger/Debugger.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/FileSystem/SourceScanner.cpp"
)

# Libraries

add_library(parserlib STATIC)
add_library(typesystemlib STATIC)
add_library(backendlib STATIC)
add_library(libecpps STATIC)

set_target_properties(parserlib PROPERTIES CXX_STANDARD 23)
set_target_properties(typesystemlib PROPERTIES CXX_STANDARD 23)
set_target_properties(backendlib PROPERTIES CXX_STANDARD 23)
set_target_properties(libecpps PROPERTIES CXX_STANDARD 23)

# Dependencies

target_link_libraries(parserlib PRIVATE ecpps_defaults platformlib utilitieslib)
target_link_libraries(typesystemlib PRIVATE ecpps_defaults platformlib utilitieslib backendlib)
target_link_libraries(backendlib PRIVATE ecpps_defaults platformlib utilitieslib typesystemlib)
target_link_libraries(libecpps PRIVATE ecpps_defaults platformlib utilitieslib)

target_link_libraries(libecpps PUBLIC platformlib utilitieslib parserlib
                      typesystemlib backendlib)

target_include_directories(parserlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_include_directories(typesystemlib PUBLIC
                           "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_include_directories(backendlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_include_directories(libecpps
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "../platformlib/include"
        "../utilitieslib/include/utilitieslib"
)

# PCH
target_precompile_headers(parserlib PRIVATE
                          "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h"
)
target_precompile_headers(backendlib PRIVATE
                          "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h"
)
target_precompile_headers(typesystemlib PRIVATE
                          "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h"
)
target_precompile_headers(libecpps PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h"
)

# Sources

target_sources(parserlib PRIVATE ${PARSING_SOURCES})
target_sources(backendlib PRIVATE ${BACKEND_SOURCES})
target_sources(typesystemlib PRIVATE ${TYPESYSTEM_SOURCES})
target_sources(libecpps PUBLIC ${SHARED_SOURCES})
