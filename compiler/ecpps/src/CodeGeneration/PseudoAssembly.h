#pragma once
#include <string>
#include <unordered_set>
#include <vector>
#include "../Execution/NodeBase.h"
#include "../Parsing/SourceMap.h"

namespace ecpps::codegen
{
     extern std::unordered_set<std::string> g_functionImports;

     void Compile(SourceFile& source, const std::vector<ir::NodePointer>& intermediateRepresentation);
} // namespace ecpps::codegen
