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
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::LeftShift>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(registerArray.size() == 3,
                    std::format("Invalid register array! Specified: {}, expected: [destination, left, right]",
                                registerArray.size()));

     const auto& destination = registerArray[0];
     const auto& left = registerArray[1];
     const auto& right = registerArray[2];

     runtime_assert(!this->IsSpilled(left) && !this->IsSpilled(right) && !this->IsSpilled(destination),
                    "LeftShift operands must not be spilled");

     if (!this->ImmediateOf(right).has_value()) this->_registerAllocator.Prefer(RegisterIndex::Rcx);
     built.append_range(EnsureMaterialisation(right));
     this->_registerAllocator.ClearPreference();

     if (!this->ImmediateOf(left).has_value()) built.append_range(EnsureMaterialisation(left));
     if (!this->ImmediateOf(right).has_value()) built.append_range(EnsureMaterialisation(right));

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::LeftShiftRegisters));
     values::LeftShiftRegisters& leftShiftValue = *new (newState.data.data()) values::LeftShiftRegisters{};
     leftShiftValue.parameters = std::make_tuple(left, right);
     this->Redefine(destination, newState);

     if (this->IsMutable(left) || this->IsMutable(right) || this->IsMutable(destination))
          built.append_range(EnsureMaterialisation(destination));

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::LeftShift>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::LeftShiftRegisters& value =
          *std::launder(reinterpret_cast<const values::LeftShiftRegisters*>(data.data()));
     const auto accumulator = std::get<0>(value.parameters);
     const auto count = std::get<1>(value.parameters);

     const Width width = WidthFromSize(this->GetVRM().GetSize(owner));
     constexpr auto rcx = RegisterIndex::Rcx;

     const auto accumulatorImmediate = this->ImmediateOf(accumulator);
     const auto countImmediate = this->ImmediateOf(count);

     std::vector<ecpps::ir::abstract::Instruction> built{};

     RegisterIndex destinationRegister{};
     if (accumulatorImmediate.has_value())
     {
          std::ignore = this->ConsumeUse(accumulator);
          destinationRegister = this->_registerAllocator.Allocate(owner);
          built.push_back(BuildMov(width, RegisterOperand{destinationRegister}, IntegerOperand{*accumulatorImmediate}));
     }
     else
     {
          const RegisterIndex accumulatorRegister = this->PhysicalRegisterOf(accumulator);
          const auto remainingUses = this->ConsumeUse(accumulator);

          if (remainingUses == 0 && !this->IsMutable(accumulator))
          {
               this->TransferRegister(accumulator, owner);
               destinationRegister = accumulatorRegister;
          }
          else
          {
               destinationRegister = this->_registerAllocator.Allocate(owner);
               built.push_back(
                    BuildMov(width, RegisterOperand{destinationRegister}, RegisterOperand{accumulatorRegister}));
               if (remainingUses == 0) this->ReleaseRegister(accumulator);
          }
     }

     if (countImmediate.has_value())
     {
          built.push_back(BuildLeftShift(width, RegisterOperand{destinationRegister}, IntegerOperand{*countImmediate}));
          this->DereferenceAndMaybeFree(count);
          return {.instructions = std::move(built), .assignedRegister = destinationRegister};
     }

     const RegisterIndex countRegister = this->PhysicalRegisterOf(count);
     runtime_assert(countRegister != destinationRegister, "shift count and destination must not share a register");
     if (countRegister == rcx)
     {
          built.push_back(BuildLeftShift(width, RegisterOperand{destinationRegister}, RegisterOperand{rcx}));
     }
     else if (destinationRegister != rcx && this->IsClobberable(rcx))
     {
          built.push_back(BuildMov(Width::W32, RegisterOperand{rcx}, RegisterOperand{countRegister}));
          built.push_back(BuildLeftShift(width, RegisterOperand{destinationRegister}, RegisterOperand{rcx}));
     }
     else
     {
          const RegisterIndex shiftTarget = destinationRegister == rcx ? countRegister : destinationRegister;
          built.push_back(BuildXchg(Width::W64, RegisterOperand{rcx}, RegisterOperand{countRegister}));
          built.push_back(BuildLeftShift(width, RegisterOperand{shiftTarget}, RegisterOperand{rcx}));
          built.push_back(BuildXchg(Width::W64, RegisterOperand{rcx}, RegisterOperand{countRegister}));
     }

     this->DereferenceAndMaybeFree(count);
     return {.instructions = std::move(built), .assignedRegister = destinationRegister};
}

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::RightShift>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(registerArray.size() == 3,
                    std::format("Invalid register array! Specified: {}, expected: [destination, left, right]",
                                registerArray.size()));

     const auto& destination = registerArray[0];
     const auto& left = registerArray[1];
     const auto& right = registerArray[2];

     runtime_assert(!this->IsSpilled(left) && !this->IsSpilled(right) && !this->IsSpilled(destination),
                    "RightShift operands must not be spilled");

     if (!this->ImmediateOf(right).has_value()) this->_registerAllocator.Prefer(RegisterIndex::Rcx);
     built.append_range(EnsureMaterialisation(right));
     this->_registerAllocator.ClearPreference();

     if (!this->ImmediateOf(left).has_value()) built.append_range(EnsureMaterialisation(left));
     if (!this->ImmediateOf(right).has_value()) built.append_range(EnsureMaterialisation(right));

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::RightShiftRegisters));
     values::RightShiftRegisters& rightShiftValue = *new (newState.data.data()) values::RightShiftRegisters{};
     rightShiftValue.parameters = std::make_tuple(left, right);
     this->Redefine(destination, newState);

     if (this->IsMutable(left) || this->IsMutable(right) || this->IsMutable(destination))
          built.append_range(EnsureMaterialisation(destination));

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::RightShift>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::RightShiftRegisters& value =
          *std::launder(reinterpret_cast<const values::RightShiftRegisters*>(data.data()));
     const auto accumulator = std::get<0>(value.parameters);
     const auto count = std::get<1>(value.parameters);

     const Width width = WidthFromSize(this->GetVRM().GetSize(owner));
     constexpr auto rcx = RegisterIndex::Rcx;

     const auto accumulatorImmediate = this->ImmediateOf(accumulator);
     const auto countImmediate = this->ImmediateOf(count);

     std::vector<ecpps::ir::abstract::Instruction> built{};

     RegisterIndex destinationRegister{};
     if (accumulatorImmediate.has_value())
     {
          std::ignore = this->ConsumeUse(accumulator);
          destinationRegister = this->_registerAllocator.Allocate(owner);
          built.push_back(BuildMov(width, RegisterOperand{destinationRegister}, IntegerOperand{*accumulatorImmediate}));
     }
     else
     {
          const RegisterIndex accumulatorRegister = this->PhysicalRegisterOf(accumulator);
          const auto remainingUses = this->ConsumeUse(accumulator);

          if (remainingUses == 0 && !this->IsMutable(accumulator))
          {
               this->TransferRegister(accumulator, owner);
               destinationRegister = accumulatorRegister;
          }
          else
          {
               destinationRegister = this->_registerAllocator.Allocate(owner);
               built.push_back(
                    BuildMov(width, RegisterOperand{destinationRegister}, RegisterOperand{accumulatorRegister}));
               if (remainingUses == 0) this->ReleaseRegister(accumulator);
          }
     }

     if (countImmediate.has_value())
     {
          built.push_back(
               BuildRightShift(width, RegisterOperand{destinationRegister}, IntegerOperand{*countImmediate}));
          this->DereferenceAndMaybeFree(count);
          return {.instructions = std::move(built), .assignedRegister = destinationRegister};
     }

     const RegisterIndex countRegister = this->PhysicalRegisterOf(count);
     runtime_assert(countRegister != destinationRegister, "shift count and destination must not share a register");
     if (countRegister == rcx)
     {
          built.push_back(BuildRightShift(width, RegisterOperand{destinationRegister}, RegisterOperand{rcx}));
     }
     else if (destinationRegister != rcx && this->IsClobberable(rcx))
     {
          built.push_back(BuildMov(Width::W32, RegisterOperand{rcx}, RegisterOperand{countRegister}));
          built.push_back(BuildRightShift(width, RegisterOperand{destinationRegister}, RegisterOperand{rcx}));
     }
     else
     {
          const RegisterIndex shiftTarget = destinationRegister == rcx ? countRegister : destinationRegister;
          built.push_back(BuildXchg(Width::W64, RegisterOperand{rcx}, RegisterOperand{countRegister}));
          built.push_back(BuildRightShift(width, RegisterOperand{shiftTarget}, RegisterOperand{rcx}));
          built.push_back(BuildXchg(Width::W64, RegisterOperand{rcx}, RegisterOperand{countRegister}));
     }

     this->DereferenceAndMaybeFree(count);
     return {.instructions = std::move(built), .assignedRegister = destinationRegister};
}
