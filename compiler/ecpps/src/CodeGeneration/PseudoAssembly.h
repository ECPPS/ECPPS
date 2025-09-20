#pragma once
#include <vector>
#include "../Execution/NodeBase.h"
#include "Nodes.h"
#include <string>
#include <unordered_set>

namespace ecpps::codegen
{
     extern std::unordered_set<std::string> g_functionImports;

     void Compile(SourceFile& source, const std::vector<ir::NodePointer>& intermediateRepresentation);
}