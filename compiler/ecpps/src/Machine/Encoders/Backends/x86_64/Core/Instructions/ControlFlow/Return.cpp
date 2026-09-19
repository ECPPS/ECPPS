#include <cstddef>
#include <format>
#include <new>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "RuntimeAssert.h"

using ecpps::abi::encoders::x8664::X8664InstructionName;
namespace instructionData = ecpps::abi::encoders::x8664::instructionSetData;

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Return>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(
          registerArray.size() <= 1,
          std::format("Invalid register array! Specified: {}, expected: [] or [value]", registerArray.size()));

     ir::abstract::Instruction instruction{};
     instruction.opcode = X8664InstructionName::Ret;
     instruction.description.resize(sizeof(RetInstruction));
     RetInstruction& ret = *new (instruction.description.data()) RetInstruction{};

     if (!registerArray.empty())
     {
          const auto& value = registerArray[0];
          built.append_range(EnsureMaterialisation(value));

          this->GetVRM().DereferenceRegister(value); // TODO: check use counter
          runtime_assert(this->GetVRM().IsMaterialised(value), "Failed to materialise the return value");

          ret.value = RegisterOperand{RegisterIndex::Rax};
     }

     built.push_back(instruction);
     return built;
}
