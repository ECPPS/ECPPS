set(PARSING_SOURCES
    "src/Parsing/AST.cpp"
    "src/Parsing/ASTContext.cpp"
    "src/Parsing/Preprocessor.cpp"
    "src/Parsing/SourceMap.cpp"
    "src/Parsing/Tokeniser.cpp"
)

set(TYPESYSTEM_SOURCES
    "src/TypeSystem/ArithmeticTypes.cpp"
    "src/TypeSystem/CompoundTypes.cpp"
    "src/TypeSystem/TypeBase.cpp"
)

set(BACKEND_SOURCES
    "src/CodeGeneration/Emitters/x86_64/Opcodes.cpp"
    "src/CodeGeneration/Emitters/x86_64.cpp"
    "src/CodeGeneration/CodeEmitter.cpp"
    "src/CodeGeneration/Nodes.cpp"
    "src/CodeGeneration/PseudoAssembly.cpp"
    "src/Machine/Vendor/aARM/ISA.cpp"
    "src/Machine/Vendor/ax86/ISA.cpp"
    "src/Machine/ABI.cpp"
    "src/Machine/Machine.cpp"
    "src/Machine/Mangling.cpp"
    "src/Linker/CoffLinker.cpp"
    "src/Linker/dllhelp.cpp"
    "src/Linker/Linker.cpp"
    "src/Linker/PE.cpp"
    "src/Linker/WindowsLinker.cpp"
    "src/Execution/IR.cpp"
    "src/Execution/NodeBase.cpp"
    "src/Execution/Entities.cpp"
    "src/Execution/Context.cpp"
)

set(SHARED_SOURCES
    "src/Shared/BumpAllocator.cpp"
    "src/Shared/Config.cpp"
    "src/Shared/Diagnostics.cpp"
    "src/Shared/Error.cpp"
    "src/Shared/mimalloc.cpp"
    "src/Debugger/Debuggers/Win64.cpp"
    "src/Debugger/Debugger.cpp"
)

add_library(parserlib STATIC ${PARSING_SOURCES})
target_include_directories(parserlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
                           "../utilitieslib/include/utilitieslib"
                           "../platformlib/include")
set_target_properties(parserlib PROPERTIES CXX_STANDARD 23)
target_link_libraries(parserlib PRIVATE ecpps_defaults)
target_precompile_headers(parserlib PRIVATE
                          "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h")

add_library(typesystemlib STATIC ${TYPESYSTEM_SOURCES})
target_include_directories(typesystemlib PUBLIC
                           "${CMAKE_CURRENT_SOURCE_DIR}/src"
                           "../utilitieslib/include/utilitieslib"
                           "../platformlib/include")
set_target_properties(typesystemlib PROPERTIES CXX_STANDARD 23)
target_link_libraries(typesystemlib PRIVATE ecpps_defaults)
target_precompile_headers(typesystemlib PRIVATE
                          "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../utilitieslib/src/pch.h"
)

add_library(backendlib STATIC ${BACKEND_SOURCES})
target_include_directories(backendlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
                           "../utilitieslib/include/utilitieslib"
                           "../platformlib/include")
set_target_properties(backendlib PROPERTIES CXX_STANDARD 23)
target_precompile_headers(backendlib PRIVATE
                          "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h")
target_link_libraries(backendlib PRIVATE ecpps_defaults)

add_library(libecpps STATIC ${SHARED_SOURCES})
target_link_libraries(libecpps PRIVATE ecpps_defaults)

set_target_properties(libecpps PROPERTIES CXX_STANDARD 23)
target_precompile_headers(libecpps PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h")

target_link_libraries(libecpps PRIVATE platformlib utilitieslib parserlib
                      typesystemlib backendlib)
target_include_directories(libecpps
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "../platformlib/include"
        "../utilitieslib/include/utilitieslib"
)
