#include <cstddef>
#include <format>
#include <new>
#include <span>
#include <tuple>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
#include "RuntimeAssert.h"

using ecpps::abi::encoders::x8664::X8664InstructionName;
namespace instructionData = ecpps::abi::encoders::x8664::instructionSetData;

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::CopyInteger>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     runtime_assert(
          registerArray.size() == 2,
          std::format("Invalid register array! Specified: {}, expected: [destination, literal]", registerArray.size()));

     const auto& destination = registerArray[0];
     const std::uint64_t immediate = static_cast<std::uint64_t>(registerArray[1].index);

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::CopyIntegerToRegister));
     values::CopyIntegerToRegister& copyValue = *new (newState.data.data()) values::CopyIntegerToRegister{};
     copyValue.parameters = std::make_tuple(immediate);
     this->GetVRM().UpdateValue(destination, newState);

     return {};
}

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::CopyInteger>(
          const std::span<const std::byte> data)
{
     const values::CopyIntegerToRegister& copyValue =
          *std::launder(reinterpret_cast<const values::CopyIntegerToRegister*>(data.data()));
     const auto& [immediate] = copyValue.parameters;

     ir::abstract::Instruction instruction{};
     instruction.opcode = X8664InstructionName::Mov;
     instruction.description.resize(sizeof(MovInstruction));
     MovInstruction& mov = *new (instruction.description.data()) MovInstruction{};

     mov.destination = RegisterOperand{RegisterIndex::Rax};
     mov.source = IntegerOperand{immediate};

     return {instruction};
}
