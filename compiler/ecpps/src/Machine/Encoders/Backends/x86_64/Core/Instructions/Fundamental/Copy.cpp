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
    EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Copy>(
        const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(
         registerArray.size() == 2,
         std::format("Invalid register array! Specified: {}, expected: [destination, source]", registerArray.size()));

     const auto& destination = registerArray[0];
     const auto& source = registerArray[1];
     built.append_range(EnsureMaterialisation(source));

     this->_virtualRegisterMap->DereferenceRegister(source); // TODO: check use counter
     runtime_assert(this->_virtualRegisterMap->IsMaterialised(source),
                    "Failed to materialise the source"); // TODO: Diagnostics

     const auto& materialisedSourceStateOptional = this->_virtualRegisterMap->GetMaterialisation(source);
     runtime_assert(materialisedSourceStateOptional.has_value(), "Invalid state for the materialised register");
     const auto& materialisedSourceState = materialisedSourceStateOptional.value();
     runtime_assert(materialisedSourceState.type == ecpps::ir::abstract::StateType::Allocation,
                    "Unallocated states cannot be used as operands");
     const auto& sourceBase =
         *std::launder(reinterpret_cast<const MaterialisationBase*>(materialisedSourceState.data.data()));
     switch (sourceBase.type)
     {
     case ecpps::abi::encoders::x8664::materialisations::PhysicalRegister::ConstType:
     {
          ir::abstract::State newState{};
          newState.type = ir::abstract::StateType::Allocation;

          newState.data.resize(sizeof(values::CopyRegisterToRegister));
          values::CopyRegisterToRegister& copyValue = *new (newState.data.data()) values::CopyRegisterToRegister{};
          copyValue.parameters = std::make_tuple(destination, source);
          this->_virtualRegisterMap->UpdateValue(destination, newState);
     }
     break;
     }

     return built;
}

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
    MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Copy>(
        const std::span<const std::byte> data)
{
     const values::CopyRegisterToRegister& copyValue =
         *std::launder(reinterpret_cast<const values::CopyRegisterToRegister*>(data.data()));
     ir::abstract::Instruction instruction{};
     instruction.opcode = X8664InstructionName::Mov;
     instruction.description.resize(sizeof(MovInstruction));
     MovInstruction& mov = *new (instruction.description.data()) MovInstruction{};

     const auto& [virtualDestination, virtualSource] = copyValue.parameters;
     runtime_assert(this->_virtualRegisterMap->IsMaterialised(virtualSource), "Source must be materialised");
     const auto& source = this->_virtualRegisterMap->GetMaterialisation(virtualSource);
     runtime_assert(source.has_value() && source->type == ir::abstract::StateType::Allocation,
                    "Source must be materialised");
     mov.destination = RegisterOperand{RegisterIndex::Rax};
     mov.source = RegisterOperand{RegisterIndex::Rcx};

     return {instruction};
}
