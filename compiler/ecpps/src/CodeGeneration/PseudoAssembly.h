#pragma once
#include <vector>
#include "../Execution/NodeBase.h"
#include "Nodes.h"

namespace ecpps::codegen
{
     void Compile(SourceFile& source, const std::vector<ir::NodePointer>& intermediateRepresentation);
}