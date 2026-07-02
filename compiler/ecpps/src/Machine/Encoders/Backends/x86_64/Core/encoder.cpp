#include "encoder.h"
#include <cstddef>
#include <new>
#include <span>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
#include "RuntimeAssert.h"
#include "Shared/Diagnostics.h"

extern template std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::
    X8664VirtualInstructionEncoder::EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Copy>(
        const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray);

extern template std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::
    X8664VirtualInstructionEncoder::MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Copy>(
        std::span<const std::byte> data);

std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::Encode(
    const std::vector<ir::abstract::VirtualInstruction>& input)
{
     std::vector<ecpps::ir::abstract::Instruction> instructions{};

     for (const auto& instruction : input) instructions.append_range(EncodeSingle(instruction));

     return instructions;
}

std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::EncodeSingle(
    const ir::abstract::VirtualInstruction& instruction)
{
     std::vector<ecpps::ir::abstract::Instruction> instructions{};

     switch (instruction.type)
     {
     case ecpps::ir::abstract::VirtualInstructionType::Copy:
          instructions.append_range(
              EncoderImplementation<ir::abstract::VirtualInstructionType::Copy>(instruction.operands));
          break;
     default: throw TracedException("Invalid instruction"); // TODO: Diagnostics
     }

     return instructions;
}

std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
    EnsureMaterialisation(ecpps::ir::abstract::VirtualRegister virtualRegister)
{
     if (this->_virtualRegisterMap->IsMaterialised(virtualRegister)) return {};

     const auto& value = this->_virtualRegisterMap->GetValue(virtualRegister);
     runtime_assert(value.type != ir::abstract::StateType::Unknown,
                    "Cannot materialise a  register with unknown value"); // TODO: Diagnostics
     runtime_assert(value.type != ir::abstract::StateType::Impossible,
                    "Cannot materialise a  register with impossible state"); // TODO: Diagnostics

     const auto& valueBase = *std::launder(reinterpret_cast<const AssignedValueBase*>(value.data.data()));
     switch (valueBase.type)
     {
     case ecpps::abi::encoders::x8664::AssignedValueType::Copy:
          return MaterialisationImplementation<ir::abstract::VirtualInstructionType::Copy>(
              std::span<const std::byte>{value.data});
     }

     throw TracedException("Invalid opcode");
}
