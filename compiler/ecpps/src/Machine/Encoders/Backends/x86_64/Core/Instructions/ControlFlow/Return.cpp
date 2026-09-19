#include <format>
#include <new>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
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

     if (!registerArray.empty())
     {
          const auto& value = registerArray[0];
          built.append_range(EnsureMaterialisation(value));

          this->DereferenceAndMaybeFree(value); // TODO: check use counter
          runtime_assert(this->GetVRM().IsMaterialised(value), "Failed to materialise the return value");

          const auto& valueOptional = this->GetVRM().GetMaterialisation(value);
          runtime_assert(valueOptional.has_value() && valueOptional->type == ir::abstract::StateType::Allocation,
                         "Return value must be materialised");
          const auto& valueBase =
               *std::launder(reinterpret_cast<const MaterialisationBase*>(valueOptional->data.data()));
          runtime_assert(valueBase.type == materialisations::PhysicalRegister::ConstType,
                         "Return value must have been assigned a physical register");
          const auto& valuePhysical =
               *std::launder(reinterpret_cast<const materialisations::PhysicalRegister*>(valueOptional->data.data()));
          const auto& [valueRegister] = valuePhysical.parameters;

          const auto returnRegister = static_cast<RegisterIndex>(this->_target->platform->IntegerReturnRegisterIndex());

          if (valueRegister != returnRegister)
          {
               ir::abstract::Instruction movInstruction{};
               movInstruction.opcode = X8664InstructionName::Mov;
               movInstruction.description.resize(sizeof(MovInstruction));
               MovInstruction& mov = *new (movInstruction.description.data()) MovInstruction{};
               mov.destination = RegisterOperand{returnRegister};
               mov.source = RegisterOperand{valueRegister};
               built.push_back(movInstruction);
          }
     }

     ir::abstract::Instruction retInstruction{};
     retInstruction.opcode = X8664InstructionName::Ret; // no description: RET takes no operand
     built.push_back(retInstruction);

     return built;
}
