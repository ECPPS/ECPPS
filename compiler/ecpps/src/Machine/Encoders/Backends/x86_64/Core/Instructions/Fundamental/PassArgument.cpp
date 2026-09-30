#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Execution/Context.h"
#include "Machine/Encoders/API/Platform.h"
#include "Machine/Encoders/API/Target.h"
#include "RuntimeAssert.h"

constexpr bool FitsImm32(const std::uint64_t value) noexcept
{
     const auto signedValue = static_cast<std::int64_t>(value);
     return signedValue >= std::numeric_limits<std::int32_t>::min() &&
            signedValue <= std::numeric_limits<std::int32_t>::max();
}

constexpr std::size_t WidthBytes(const ecpps::abi::encoders::x8664::Width width) noexcept
{
     return static_cast<std::size_t>(std::to_underlying(width)) / 8;
}

static ecpps::abi::encoders::x8664::MemoryOperand OutgoingSlot(const std::size_t rspOffset) noexcept
{
     return {.relativeTo = ecpps::abi::encoders::x8664::RegisterIndex::Rsp,
             .offset = static_cast<std::int32_t>(rspOffset)};
}
template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::PassArgument>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(registerArray.size() == 3,
                    std::format("Invalid register array! Specified: {}, expected: [scopeIndex, source, parameterIndex]",
                                registerArray.size()));

     const auto scopeIndex = registerArray[0].index;
     const auto& source = registerArray[1];
     const auto parameterIndex = static_cast<std::size_t>(registerArray[2].index);
     const ir::FunctionScope* scope = this->Scopes()[scopeIndex];
     const auto numberOfParameters = scope->parameters.size();
     const auto& platform = *this->_target->platform;

     runtime_assert(parameterIndex < numberOfParameters,
                    std::format("Parameter index {} out of range ({} parameters)", parameterIndex, numberOfParameters));

     const auto immediate = ImmediateOf(source);

     if (parameterIndex >= platform.IntegerParameterRegisterCount())
     {
          const auto stackSlot = this->StackParameterSlot(parameterIndex, numberOfParameters);

          const auto minWidth = static_cast<Width>(platform.ParameterStackSlotWidth());
          const auto originalWidth = MapWidth(this->GetVRM().GetWidth(source));
          const Width width = std::max(originalWidth, minWidth);
          const auto slotSize = WidthBytes(minWidth);
          const auto rspOffset = platform.InitialStackReserve() + (slotSize * stackSlot);
          this->_outgoingReserve = std::max(this->_outgoingReserve, rspOffset + slotSize);

          if (immediate)
          {
               if (width != Width::W64 || FitsImm32(*immediate))
               {
                    built.push_back(BuildMov(width, OutgoingSlot(rspOffset), IntegerOperand{*immediate}));
                    std::ignore = this->ConsumeUse(source);
                    return built;
               }
               const RegisterIndex tempReg = this->_registerAllocator.Allocate(source);
               built.push_back(BuildMov(width, RegisterOperand{tempReg}, IntegerOperand{*immediate}));
               built.push_back(BuildMov(width, OutgoingSlot(rspOffset), RegisterOperand{tempReg}));
               this->_registerAllocator.Free(source);
               std::ignore = this->ConsumeUse(source);
               return built;
          }

          if (this->IsSpilled(source))
          {
               const auto sourceSlot = this->EnsureStackSlot(source);
               const RegisterIndex tempReg = this->_registerAllocator.Allocate(source);
               built.push_back(BuildMov(width, RegisterOperand{tempReg}, sourceSlot));
               built.push_back(BuildMov(width, OutgoingSlot(rspOffset), RegisterOperand{tempReg}));
               this->_registerAllocator.Free(source);
               std::ignore = this->ConsumeUse(source);
               return built;
          }

          built.append_range(EnsureMaterialisation(source));
          const RegisterIndex sourceRegister = this->PhysicalRegisterOf(source);
          built.push_back(BuildMov(width, OutgoingSlot(rspOffset), RegisterOperand{sourceRegister}));

          const auto remainingUses = this->ConsumeUse(source);
          if (remainingUses == 0) this->ReleaseRegister(source);
          return built;
     }

     const auto abiRegister = static_cast<RegisterIndex>(platform.IntegerParameterRegisterIndex(parameterIndex));
     this->Lock(abiRegister);

     if (immediate)
     {
          const Width width = MapWidth(this->GetVRM().GetWidth(source));
          built.append_range(this->ClearRegister(abiRegister, source));
          built.push_back(BuildMov(width, RegisterOperand{abiRegister}, IntegerOperand{*immediate}));
          std::ignore = this->ConsumeUse(source);
          return built;
     }

     if (this->IsSpilled(source))
     {
          const Width width = MapWidth(this->GetVRM().GetWidth(source));
          built.append_range(this->ClearRegister(abiRegister, source));
          built.push_back(BuildMov(width, RegisterOperand{abiRegister}, this->EnsureStackSlot(source)));
          std::ignore = this->ConsumeUse(source);
          return built;
     }

     built.append_range(EnsureMaterialisation(source));
     const Width width = MapWidth(this->GetVRM().GetWidth(source));
     const RegisterIndex sourceRegister = this->PhysicalRegisterOf(source);
     const auto remainingUses = this->ConsumeUse(source);

     if (sourceRegister == abiRegister) return built;

     built.append_range(this->ClearRegister(abiRegister, source));
     if (remainingUses == 0) this->ReleaseRegister(source);
     built.push_back(BuildMov(width, RegisterOperand{abiRegister}, RegisterOperand{sourceRegister}));
     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::PassArgument>(
          [[maybe_unused]] const ecpps::ir::abstract::VirtualRegister owner,
          [[maybe_unused]] const std::span<const std::byte> data)
{
     runtime_assert(false, "PassArgument is lowered eagerly and must never be materialised lazily");
     return {};
}
