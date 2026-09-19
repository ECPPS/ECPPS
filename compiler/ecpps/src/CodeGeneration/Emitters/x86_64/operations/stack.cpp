#include <cstddef>
#include <utility>
#include <vector>
#include "../x86_64.h"
#include "CodeGeneration/Emitters/x86_64/Opcodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"

using namespace ecpps::abi::encoders::x8664;
using namespace ecpps::codegen::x86_64;

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitPush(
     const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::PushInstruction), "Invalid PUSH instruction");

     const auto& push =
          *std::launder(reinterpret_cast<const abi::encoders::x8664::PushInstruction*>(description.data()));

     return GeneratePushReg64(std::to_underlying(push.reg.index));
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitPop(const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(abi::encoders::x8664::PopInstruction), "Invalid PUSH instruction");

     const auto& pop = *std::launder(reinterpret_cast<const abi::encoders::x8664::PopInstruction*>(description.data()));

     return GeneratePopReg64(std::to_underlying(pop.reg.index));
}
