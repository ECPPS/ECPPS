#pragma once
#include "Nodes.h"
#include "../Execution/NodeBase.h"
#include <vector>

namespace ecpps::codegen
{
	void Compile(SourceFile& source, const std::vector<ir::NodePointer>& intermediateRepresentation);
}