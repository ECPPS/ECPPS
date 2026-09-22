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
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Sub>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(registerArray.size() == 3,
                    std::format("Invalid register array! Specified: {}, expected: [destination, left, right]",
                                registerArray.size()));

     const auto& destination = registerArray[0];
     const auto& left = registerArray[1];
     const auto& right = registerArray[2];

     if (!this->ImmediateOf(left).has_value()) built.append_range(EnsureMaterialisation(left));
     if (!this->ImmediateOf(right).has_value() && right != left) built.append_range(EnsureMaterialisation(right));

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::SubRegisters));
     values::SubRegisters& subValue = *new (newState.data.data()) values::SubRegisters{};
     subValue.parameters = std::make_tuple(left, right);
     this->Redefine(destination, newState);

     if (this->IsMutable(left) || this->IsMutable(right) || this->IsMutable(destination))
          built.append_range(EnsureMaterialisation(destination));

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Sub>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::SubRegisters& subValue = *std::launder(reinterpret_cast<const values::SubRegisters*>(data.data()));
     const auto accumulator = std::get<0>(subValue.parameters);
     const auto other = std::get<1>(subValue.parameters);

     const Width width = WidthFromSize(this->GetVRM().GetSize(owner));

     const auto accumulatorImmediate = this->ImmediateOf(accumulator);
     const auto otherImmediate = this->ImmediateOf(other);
     std::vector<ecpps::ir::abstract::Instruction> built{};
     RegisterIndex destinationRegister{};

     Operand source{};
     if (otherImmediate.has_value()) source = IntegerOperand{*otherImmediate};
     else if (this->GetVRM().IsMaterialised(other))
          source = RegisterOperand{this->PhysicalRegisterOf(other)};
     else
          source = MemoryOperand{.relativeTo = this->PhysicalRegisterOf(other)};

     if (accumulator == other)
     {
          const auto remainingUses = this->ConsumeUse(accumulator);

          destinationRegister = this->_registerAllocator.Allocate(owner);

          built.push_back(BuildMov(width, RegisterOperand{destinationRegister}, IntegerOperand{0}));

          if (remainingUses == 0 && !this->IsMutable(accumulator)) this->ReleaseRegister(accumulator);

          return {.instructions = std::move(built), .assignedRegister = destinationRegister};
     }

     if (accumulatorImmediate.has_value())
     {
          std::ignore = this->ConsumeUse(accumulator);
          destinationRegister = this->_registerAllocator.Allocate(owner);
          built.push_back(BuildMov(width, RegisterOperand{destinationRegister}, IntegerOperand{*accumulatorImmediate}));
     }
     else
     {
          const RegisterIndex accumulatorRegister = this->PhysicalRegisterOf(accumulator);
          const auto accumulatorRemainingUses = this->ConsumeUse(accumulator);

          if (accumulatorRemainingUses == 0 && !this->IsMutable(accumulator))
          {
               this->TransferRegister(accumulator, owner);
               destinationRegister = accumulatorRegister;
          }
          else
          {
               destinationRegister = this->_registerAllocator.Allocate(owner);
               built.push_back(
                    BuildMov(width, RegisterOperand{destinationRegister}, RegisterOperand{accumulatorRegister}));
               if (accumulatorRemainingUses == 0) this->ReleaseRegister(accumulator);
          }
     }

     built.push_back(BuildSub(width, RegisterOperand{destinationRegister}, source));
     this->DereferenceAndMaybeFree(other);

     return {.instructions = std::move(built), .assignedRegister = destinationRegister};
}
