#include <format>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "RuntimeAssert.h"

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Call>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(registerArray.size() == 1,
                    std::format("Invalid register array! Specified: {}, expected: [index]", registerArray.size()));

     const auto& functionIndex = registerArray[0];

     built.push_back(BuildCall(functionIndex.index));

     return built;
}
