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

     this->GetVRM().DereferenceRegister(left);  // TODO: check use counter
     this->GetVRM().DereferenceRegister(right); // TODO: check use counter
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
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Add>(
          const std::span<const std::byte> data)
{
     const values::AddRegisters& addValue = *std::launder(reinterpret_cast<const values::AddRegisters*>(data.data()));
     const auto& [virtualLeft, virtualRight] = addValue.parameters;

     runtime_assert(this->GetVRM().IsMaterialised(virtualLeft), "Left operand must be materialised");
     runtime_assert(this->GetVRM().IsMaterialised(virtualRight), "Right operand must be materialised");
     const auto& left = this->GetVRM().GetMaterialisation(virtualLeft);
     const auto& right = this->GetVRM().GetMaterialisation(virtualRight);
     runtime_assert(left.has_value() && left->type == ir::abstract::StateType::Allocation,
                    "Left operand must be materialised");
     runtime_assert(right.has_value() && right->type == ir::abstract::StateType::Allocation,
                    "Right operand must be materialised");

     ir::abstract::Instruction movInstruction{};
     movInstruction.opcode = X8664InstructionName::Mov;
     movInstruction.description.resize(sizeof(MovInstruction));
     MovInstruction& mov = *new (movInstruction.description.data()) MovInstruction{};
     mov.destination = RegisterOperand{RegisterIndex::Rax};
     mov.source = RegisterOperand{RegisterIndex::Rcx};

     ir::abstract::Instruction addInstruction{};
     addInstruction.opcode = X8664InstructionName::Add;
     addInstruction.description.resize(sizeof(AddInstruction));
     AddInstruction& add = *new (addInstruction.description.data()) AddInstruction{};
     add.modifiedDestination = RegisterOperand{RegisterIndex::Rax};
     add.source = RegisterOperand{RegisterIndex::Rdx};

     return {movInstruction, addInstruction};
}
