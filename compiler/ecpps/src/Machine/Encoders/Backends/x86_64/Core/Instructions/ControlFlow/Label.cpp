#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Label>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     runtime_assert(registerArray.size() == 1, "Label expects [id]");
     return {BuildLabel(registerArray[0].index)};
}
