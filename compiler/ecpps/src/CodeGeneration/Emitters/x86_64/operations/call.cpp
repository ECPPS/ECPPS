#include <cstddef>
#include <vector>
#include "../x86_64.h"
#include "CodeGeneration/Emitters/x86_64/Opcodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"

using namespace ecpps::abi::encoders::x8664;
using namespace ecpps::codegen::x86_64;

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitCall(
     const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::CallInstruction), "Invalid CALL instruction");

     const auto& call =
          *std::launder(reinterpret_cast<const abi::encoders::x8664::CallInstruction*>(description.data()));
     this->_callPatches.push_back(this->_currentInstructionBase);

     auto rel = GenerateCallRel32(static_cast<std::int32_t>(call.indexToTable));
     rel.append_range(GenerateNopN(2));
     return rel;
}
