#include <cstddef>
#include <format>
#include <new>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
#include "RuntimeAssert.h"

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::ArithmeticNegate>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(
          registerArray.size() == 2,
          std::format("Invalid register array! Specified: {}, expected: [destination, operand]", registerArray.size()));

     const auto& destination = registerArray[0];
     const auto& operand = registerArray[1];

     runtime_assert(!this->IsSpilled(operand) && !this->IsSpilled(destination),
                    "Binary complement operands must not be spilled");

     if (!this->ImmediateOf(operand).has_value()) built.append_range(EnsureMaterialisation(operand));

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::ArithmeticNegationRegisters));
     values::ArithmeticNegationRegisters& complementValue =
          *new (newState.data.data()) values::ArithmeticNegationRegisters{};
     complementValue.parameters = std::make_tuple(operand);
     this->Redefine(destination, newState);

     if (this->IsMutable(operand) || this->IsMutable(destination))
          built.append_range(EnsureMaterialisation(destination));

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::ArithmeticNegate>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::ArithmeticNegationRegisters& complementValue =
          *std::launder(reinterpret_cast<const values::ArithmeticNegationRegisters*>(data.data()));
     const auto operand = std::get<0>(complementValue.parameters);

     const Width width = WidthFromSize(this->GetVRM().GetSize(owner));
     const auto operandImmediate = this->ImmediateOf(operand);

     std::vector<ecpps::ir::abstract::Instruction> built{};
     RegisterIndex destinationRegister{};

     if (operandImmediate.has_value())
     {
          std::ignore = this->ConsumeUse(operand);
          destinationRegister = this->_registerAllocator.Allocate(owner);
          built.push_back(BuildMov(width, RegisterOperand{destinationRegister}, IntegerOperand{*operandImmediate}));
     }
     else
     {
          const RegisterIndex operandRegister = this->PhysicalRegisterOf(operand);
          const auto operandRemainingUses = this->ConsumeUse(operand);

          if (operandRemainingUses == 0 && !this->IsMutable(operand))
          {
               this->TransferRegister(operand, owner);
               destinationRegister = operandRegister;
          }
          else
          {
               destinationRegister = this->_registerAllocator.Allocate(owner);
               built.push_back(BuildMov(width, RegisterOperand{destinationRegister}, RegisterOperand{operandRegister}));
               if (operandRemainingUses == 0) this->ReleaseRegister(operand);
          }
     }

     built.push_back(BuildArithmeticNegatation(width, RegisterOperand{destinationRegister}));

     return {.instructions = std::move(built), .assignedRegister = destinationRegister};
}
