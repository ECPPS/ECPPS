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

constexpr std::int64_t SignExtendImmediate(const std::uint64_t value, const unsigned sourceBits) noexcept
{
     if (sourceBits >= 64) return static_cast<std::int64_t>(value);
     const std::uint64_t mask = (std::uint64_t{1} << sourceBits) - 1;
     const std::uint64_t signBit = std::uint64_t{1} << (sourceBits - 1);
     const std::uint64_t truncated = value & mask;
     return static_cast<std::int64_t>((truncated ^ signBit) - signBit);
}

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::SignExtension>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(
          registerArray.size() == 2,
          std::format("Invalid register array! Specified: {}, expected: [destination, source]", registerArray.size()));

     const auto& destination = registerArray[0];
     const auto& source = registerArray[1];

     if (this->IsSpilled(destination))
     {
          const Width destinationWidth = MapWidth(this->GetVRM().GetWidth(destination));
          const Width sourceWidth = MapWidth(this->GetVRM().GetWidth(source));

          if (const auto immediate = this->ImmediateOf(source); immediate.has_value())
          {
               using ImmediateType = std::remove_cvref_t<decltype(*immediate)>;
               const auto extended = static_cast<ImmediateType>(
                    SignExtendImmediate(static_cast<std::uint64_t>(*immediate),
                                        static_cast<unsigned>(std::to_underlying(sourceWidth)) * 8U));

               built.push_back(
                    BuildMov(destinationWidth, this->EnsureStackSlot(destination), IntegerOperand{extended}));

               this->DereferenceAndMaybeFree(source);
               return built;
          }

          Operand sourceOperand{};
          if (this->IsSpilled(source)) sourceOperand = this->EnsureStackSlot(source);
          else
          {
               ecpps::detail::AppendRange(built, EnsureMaterialisation(source));
               sourceOperand = RegisterOperand{this->PhysicalRegisterOf(source)};
          }

          const StackOperand destinationSlot = this->EnsureStackSlot(destination);
          const RegisterIndex scratchRegister = this->_registerAllocator.Allocate(destination);

          built.push_back(BuildMovsx(destinationWidth, sourceWidth, RegisterOperand{scratchRegister}, sourceOperand));
          built.push_back(BuildMov(destinationWidth, destinationSlot, RegisterOperand{scratchRegister}));

          this->_registerAllocator.Free(destination);
          this->DereferenceAndMaybeFree(source);
          return built;
     }

     if (!this->ImmediateOf(source).has_value() && !this->IsSpilled(source))
          ecpps::detail::AppendRange(built, EnsureMaterialisation(source));

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::SignExtendToRegister));
     values::SignExtendToRegister& copyValue = *new (newState.data.data()) values::SignExtendToRegister{};
     copyValue.parameters = std::make_tuple(destination, source);
     this->Redefine(destination, newState);

     if (this->IsMutable(destination) || this->IsMutable(source))
          ecpps::detail::AppendRange(built, EnsureMaterialisation(destination));

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::SignExtension>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::SignExtendToRegister& copyValue =
          *std::launder(reinterpret_cast<const values::SignExtendToRegister*>(data.data()));
     const auto virtualSource = std::get<1>(copyValue.parameters);

     const Width sourceWidth = MapWidth(this->GetVRM().GetWidth(virtualSource));
     const Width destinationWidth = MapWidth(this->GetVRM().GetWidth(owner));

     if (const auto immediate = this->ImmediateOf(virtualSource); immediate.has_value())
     {
          using ImmediateType = std::remove_cvref_t<decltype(*immediate)>;
          const auto extended = static_cast<ImmediateType>(SignExtendImmediate(
               static_cast<std::uint64_t>(*immediate), static_cast<unsigned>(std::to_underlying(sourceWidth)) * 8U));

          const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);
          this->DereferenceAndMaybeFree(virtualSource);

          return {.instructions = {BuildMov(destinationWidth, RegisterOperand{destinationRegister},
                                            IntegerOperand{extended})},
                  .assignedRegister = destinationRegister};
     }

     if (this->IsSpilled(virtualSource))
     {
          const auto slot = this->EnsureStackSlot(virtualSource);
          const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);
          const auto remainingUses = this->ConsumeUse(virtualSource);
          if (remainingUses == 0) this->ReleaseRegister(virtualSource);

          return {
               .instructions = {BuildMovsx(destinationWidth, sourceWidth, RegisterOperand{destinationRegister}, slot)},
               .assignedRegister = destinationRegister};
     }

     const RegisterIndex sourceRegister = this->PhysicalRegisterOf(virtualSource);
     const auto remainingUses = this->ConsumeUse(virtualSource);

     RegisterIndex destinationRegister{};

     if (remainingUses == 0 && !this->IsMutable(virtualSource))
     {
          this->TransferRegister(virtualSource, owner);
          destinationRegister = sourceRegister;
     }
     else
     {
          destinationRegister = this->_registerAllocator.Allocate(owner);
          if (remainingUses == 0) this->ReleaseRegister(virtualSource);
     }

     return {.instructions = {BuildMovsx(destinationWidth, sourceWidth, RegisterOperand{destinationRegister},
                                         RegisterOperand{sourceRegister})},
             .assignedRegister = destinationRegister};
}
