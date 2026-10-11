
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::CompareAndJump>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     runtime_assert(registerArray.size() == 4, "CompareAndJump expects [lhs, rhs, condition, label id]");
     const auto lhs = registerArray[0];
     const auto rhs = registerArray[1];
     const auto condition = static_cast<ir::abstract::ConditionCode>(registerArray[2].index);

     std::vector<ir::abstract::Instruction> built = this->EnsureMaterialisation(lhs);

     Operand right{};
     if (const auto immediate = this->ImmediateOf(rhs); immediate.has_value() && *immediate <= 0x7FFFFFFFu)
          right = IntegerOperand{*immediate};
     else
     {
          built.append_range(this->EnsureMaterialisation(rhs));
          right = RegisterOperand{this->PhysicalRegisterOf(rhs)};
     }
     built.push_back(
          BuildCmp(MapWidth(this->GetVRM().GetWidth(lhs)), RegisterOperand{this->PhysicalRegisterOf(lhs)}, right));
     built.push_back(BuildJcc(condition, registerArray[3].index));

     this->DereferenceAndMaybeFree(lhs);
     this->DereferenceAndMaybeFree(rhs);
     return built;
}
