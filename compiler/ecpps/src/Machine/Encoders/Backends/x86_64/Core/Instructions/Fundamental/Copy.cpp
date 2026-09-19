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

     this->DereferenceAndMaybeFree(source); // TODO: check use counter
     runtime_assert(this->GetVRM().IsMaterialised(source),
                    "Failed to materialise the source"); // TODO: Diagnostics

     const auto& materialisedSourceStateOptional = this->GetVRM().GetMaterialisation(source);
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
          this->Redefine(destination, newState);
     }
     break;
     }

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Copy>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::CopyRegisterToRegister& copyValue =
          *std::launder(reinterpret_cast<const values::CopyRegisterToRegister*>(data.data()));
     const auto& [virtualDestination, virtualSource] = copyValue.parameters;

     runtime_assert(this->GetVRM().IsMaterialised(virtualSource), "Source must be materialised");
     const auto& sourceOptional = this->GetVRM().GetMaterialisation(virtualSource);
     runtime_assert(sourceOptional.has_value() && sourceOptional->type == ir::abstract::StateType::Allocation,
                    "Source must be materialised");
     const auto& sourceBase = *std::launder(reinterpret_cast<const MaterialisationBase*>(sourceOptional->data.data()));
     runtime_assert(sourceBase.type == materialisations::PhysicalRegister::ConstType,
                    "Source must have been assigned a physical register");
     const auto& sourcePhysical =
          *std::launder(reinterpret_cast<const materialisations::PhysicalRegister*>(sourceOptional->data.data()));
     const auto& [sourceRegister] = sourcePhysical.parameters;

     const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);

     ir::abstract::Instruction instruction{};
     instruction.opcode = X8664InstructionName::Mov;
     instruction.description.resize(sizeof(MovInstruction));
     MovInstruction& mov = *new (instruction.description.data()) MovInstruction{};
     mov.destination = RegisterOperand{destinationRegister};
     mov.source = RegisterOperand{sourceRegister};

     return {.instructions = {instruction}, .assignedRegister = destinationRegister};
}
