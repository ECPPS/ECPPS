#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <new>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Execution/Context.h"
#include "Machine/Encoders/API/Platform.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
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

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;
     newState.data.resize(sizeof(values::PassArgumentFromAbi));
     values::PassArgumentFromAbi& parameterValue = *new (newState.data.data()) values::PassArgumentFromAbi{};

     if (parameterIndex >= platform.IntegerParameterRegisterCount())
     {
          const auto stackParameterOffset = parameterIndex - platform.IntegerParameterRegisterCount();
          const auto stackSlot = platform.StackParameterOrder() == api::StackParameterOrdering::Forward
                                      ? stackParameterOffset
                                      : (numberOfParameters - 1 - parameterIndex);

          Width width = MapWidth(this->GetVRM().GetWidth(source));
          const auto minWidth = static_cast<Width>(platform.ParameterStackSlotWidth());
          width = std::max(width, minWidth);
          const auto slotSize = WidthBytes(minWidth);
          const auto rspOffset = platform.InitialStackReserve() + (slotSize * stackSlot);
          this->_outgoingReserve = std::max(this->_outgoingReserve, rspOffset + slotSize);

          const auto immediate = ImmediateOf(source);
          if (immediate)
          {
               if (width != Width::W64 || FitsImm32(*immediate))
               {
                    built.push_back(BuildMov(width, OutgoingSlot(rspOffset), IntegerOperand{*immediate}));
                    std::ignore = this->ConsumeUse(source);
                    return built;
               }

               const auto existing = this->_registerAllocator.ColourOf(source);
               const RegisterIndex tempReg = existing ? *existing : this->_registerAllocator.Allocate(source);
               built.push_back(BuildMov(width, RegisterOperand{tempReg}, IntegerOperand{*immediate}));
               built.push_back(BuildMov(width, OutgoingSlot(rspOffset), RegisterOperand{tempReg}));
               if (!existing) this->ReleaseRegister(source);
               std::ignore = this->ConsumeUse(source);
               return built;
          }

          parameterValue.parameters = std::make_tuple(rspOffset, true, source);

          this->Redefine(source, newState);
          built.append_range(EnsureMaterialisation(source));
          return built;
     }
     const auto abiRegister = static_cast<RegisterIndex>(platform.IntegerParameterRegisterIndex(parameterIndex));
     const Width width = MapWidth(this->GetVRM().GetWidth(source));

     if (this->IsSpilled(source))
     {
          built.append_range(this->ClearRegister(abiRegister, source));
          built.push_back(BuildMov(width, RegisterOperand{abiRegister}, this->EnsureStackSlot(source)));
          std::ignore = this->ConsumeUse(source);
          return built;
     }
     if (const auto immediate = ImmediateOf(source))
     {
          built.append_range(this->ClearRegister(abiRegister, source));
          built.push_back(BuildMov(width, RegisterOperand{abiRegister}, IntegerOperand{*immediate}));
          std::ignore = this->ConsumeUse(source);
          return built;
     }

     parameterValue.parameters = std::make_tuple(static_cast<std::size_t>(abiRegister), false, source);
     this->Redefine(source, newState);

     built.append_range(EnsureMaterialisation(source));
     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::PassArgument>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     runtime_assert(data.size() >= sizeof(values::PassArgumentFromAbi), "PassArgument state is truncated");

     const values::PassArgumentFromAbi& parameterValue =
          *std::launder(reinterpret_cast<const values::PassArgumentFromAbi*>(data.data()));

     const auto rawValue = std::get<0>(parameterValue.parameters);
     const bool isStack = std::get<1>(parameterValue.parameters);
     const auto virtualSource = std::get<2>(parameterValue.parameters);

     const Width width = MapWidth(this->GetVRM().GetWidth(owner));

     if (isStack)
     {
          const auto slot = OutgoingSlot(rawValue);

          if (this->IsSpilled(virtualSource))
          {
               const auto sourceSlot = this->EnsureStackSlot(virtualSource);
               std::ignore = this->ConsumeUse(virtualSource);
               const RegisterIndex tempReg = this->_registerAllocator.Allocate(owner);

               return {.instructions = {BuildMov(width, RegisterOperand{tempReg}, sourceSlot),
                                        BuildMov(width, slot, RegisterOperand{tempReg})},
                       .assignedRegister = tempReg};
          }

          const RegisterIndex sourceRegister = this->PhysicalRegisterOf(virtualSource);
          const auto remainingUses = this->ConsumeUse(virtualSource);

          if (remainingUses == 0 && !this->IsMutable(virtualSource)) this->ReleaseRegister(virtualSource);

          return {.instructions = {BuildMov(width, slot, RegisterOperand{sourceRegister})},
                  .assignedRegister = sourceRegister};
     }

     const RegisterIndex abiRegister = static_cast<RegisterIndex>(rawValue);

     if (this->IsSpilled(virtualSource))
     {
          const auto slot = this->EnsureStackSlot(virtualSource);
          std::ignore = this->ConsumeUse(virtualSource);

          auto instructions = this->ClearRegister(abiRegister, virtualSource);
          instructions.push_back(BuildMov(width, RegisterOperand{abiRegister}, slot));
          return {.instructions = std::move(instructions), .assignedRegister = abiRegister};
     }

     const RegisterIndex sourceRegister = this->PhysicalRegisterOf(virtualSource);
     const auto remainingUses = this->ConsumeUse(virtualSource);

     if (sourceRegister == abiRegister)
     {
          return {.instructions = {}, .assignedRegister = abiRegister};
     }

     auto instructions = this->ClearRegister(abiRegister, virtualSource);

     if (remainingUses == 0 && !this->IsMutable(virtualSource)) this->ReleaseRegister(virtualSource);

     instructions.push_back(BuildMov(width, RegisterOperand{abiRegister}, RegisterOperand{sourceRegister}));
     return {.instructions = std::move(instructions), .assignedRegister = abiRegister};
}
