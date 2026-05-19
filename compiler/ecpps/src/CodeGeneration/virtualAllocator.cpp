#include <unordered_map>
#include "CodeGeneration/Nodes.h"
#include "PseudoAssembly.h"
#include "Shared/Diagnostics.h"

using ecpps::codegen::ParsingContext;

ParsingContext::ParsingContext(ecpps::abi::ABI& abi) : abi(&abi)
{
}
