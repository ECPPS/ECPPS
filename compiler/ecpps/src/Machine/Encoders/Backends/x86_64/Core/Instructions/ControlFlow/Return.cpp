#include <format>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "RuntimeAssert.h"

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
          const auto returnRegister = static_cast<RegisterIndex>(this->_target->platform->IntegerReturnRegisterIndex());

          const Width width = WidthFromSize(this->GetVRM().GetSize(value.index));

          if (this->IsSpilled(value))
          {
               built.push_back(BuildMov(width, RegisterOperand{returnRegister}, this->EnsureStackSlot(value)));
          }
          else
          {
               this->_registerAllocator.Prefer(returnRegister);
               built.append_range(EnsureMaterialisation(value));
               this->_registerAllocator.ClearPreference();

               const RegisterIndex valueRegister = this->PhysicalRegisterOf(value);
               if (valueRegister != returnRegister)
                    built.push_back(BuildMov(width, RegisterOperand{returnRegister}, RegisterOperand{valueRegister}));
          }

          this->DereferenceAndMaybeFree(value);
     }

     ir::abstract::Instruction retInstruction{};
     retInstruction.opcode = X8664InstructionName::Ret;
     built.push_back(retInstruction);

     return built;
}
