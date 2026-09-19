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
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Add>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(registerArray.size() == 3,
                    std::format("Invalid register array! Specified: {}, expected: [destination, left, right]",
                                registerArray.size()));

     const auto& destination = registerArray[0];
     const auto& left = registerArray[1];
     const auto& right = registerArray[2];

     built.append_range(EnsureMaterialisation(left));
     built.append_range(EnsureMaterialisation(right));

     this->DereferenceAndMaybeFree(left);  // TODO: check use counter
     this->DereferenceAndMaybeFree(right); // TODO: check use counter
     runtime_assert(this->GetVRM().IsMaterialised(left), "Failed to materialise the left operand");
     runtime_assert(this->GetVRM().IsMaterialised(right), "Failed to materialise the right operand");

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::AddRegisters));
     values::AddRegisters& addValue = *new (newState.data.data()) values::AddRegisters{};
     addValue.parameters = std::make_tuple(left, right);
     this->GetVRM().UpdateValue(destination, newState);

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Add>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::AddRegisters& addValue = *std::launder(reinterpret_cast<const values::AddRegisters*>(data.data()));
     const auto& [virtualLeft, virtualRight] = addValue.parameters;

     runtime_assert(this->GetVRM().IsMaterialised(virtualLeft), "Left operand must be materialised");
     runtime_assert(this->GetVRM().IsMaterialised(virtualRight), "Right operand must be materialised");
     const auto& leftOptional = this->GetVRM().GetMaterialisation(virtualLeft);
     const auto& rightOptional = this->GetVRM().GetMaterialisation(virtualRight);
     runtime_assert(leftOptional.has_value() && leftOptional->type == ir::abstract::StateType::Allocation,
                    "Left operand must be materialised");
     runtime_assert(rightOptional.has_value() && rightOptional->type == ir::abstract::StateType::Allocation,
                    "Right operand must be materialised");

     const auto& leftBase = *std::launder(reinterpret_cast<const MaterialisationBase*>(leftOptional->data.data()));
     const auto& rightBase = *std::launder(reinterpret_cast<const MaterialisationBase*>(rightOptional->data.data()));
     runtime_assert(leftBase.type == materialisations::PhysicalRegister::ConstType,
                    "Left operand must have been assigned a physical register");
     runtime_assert(rightBase.type == materialisations::PhysicalRegister::ConstType,
                    "Right operand must have been assigned a physical register");

     const auto& leftPhysical =
          *std::launder(reinterpret_cast<const materialisations::PhysicalRegister*>(leftOptional->data.data()));
     const auto& rightPhysical =
          *std::launder(reinterpret_cast<const materialisations::PhysicalRegister*>(rightOptional->data.data()));
     const auto& [leftRegister] = leftPhysical.parameters;
     const auto& [rightRegister] = rightPhysical.parameters;

     const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);

     ir::abstract::Instruction movInstruction{};
     movInstruction.opcode = X8664InstructionName::Mov;
     movInstruction.description.resize(sizeof(MovInstruction));
     MovInstruction& mov = *new (movInstruction.description.data()) MovInstruction{};
     mov.destination = RegisterOperand{destinationRegister};
     mov.source = RegisterOperand{leftRegister};

     ir::abstract::Instruction addInstruction{};
     addInstruction.opcode = X8664InstructionName::Add;
     addInstruction.description.resize(sizeof(AddInstruction));
     AddInstruction& add = *new (addInstruction.description.data()) AddInstruction{};
     add.modifiedDestination = RegisterOperand{destinationRegister};
     add.source = RegisterOperand{rightRegister};

     return {.instructions = {movInstruction, addInstruction}, .assignedRegister = destinationRegister};
}
