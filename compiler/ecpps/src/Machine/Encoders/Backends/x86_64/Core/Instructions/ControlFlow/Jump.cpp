#include <format>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "RuntimeAssert.h"

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::UnconditionalJump>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     runtime_assert(registerArray.size() == 1, "Jump expects [label id]");
     return {BuildJmp(registerArray[0].index)};
}
