add_executable(ecpps
    src/main.cpp
    "src/Shared/Config.cpp"
 "src/Parsing/SourceMap.h" "src/Shared/Diagnostics.h" "src/Parsing/SourceMap.cpp" "src/Parsing/Preprocessor.h" "src/Parsing/Preprocessor.cpp" "src/Parsing/Tokeniser.cpp" "src/Parsing/AST.h" "src/Parsing/AST.cpp" "src/Parsing/ASTs/General.h" "src/Parsing/ASTs/Type.h" "src/Execution/NodeBase.h" "src/Execution/Procedural.h" "src/TypeSystem/TypeBase.h" "src/Execution/ControlFlow.h" "src/Execution/Expressions.h" "src/Execution/Operations.h" "src/Execution/IR.h" "src/Execution/IR.cpp" "src/CodeGeneration/Nodes.h" "src/CodeGeneration/Nodes.cpp" "src/CodeGeneration/PseudoAssembly.h" "src/CodeGeneration/PseudoAssembly.cpp" "src/Machine/ABI.h" "src/Machine/ABI.cpp" "src/Machine/Machine.cpp" "src/Machine/Vendor/ax86/ISA.cpp" "src/Machine/Vendor/Shared/ISA.h" "src/Machine/Vendor/aARM/ISA.cpp")

target_link_libraries(ecpps platformlib)
target_include_directories(ecpps PUBLIC "../platformlib/include")