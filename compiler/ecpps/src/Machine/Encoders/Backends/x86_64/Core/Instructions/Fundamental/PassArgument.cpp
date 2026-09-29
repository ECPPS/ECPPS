#include <cstddef>
#include <format>
#include <new>
#include <span>
#include <utility>
#include <vector>

#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Execution/Context.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
#include "RuntimeAssert.h"

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

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;
     newState.data.resize(sizeof(values::PassArgumentFromAbi));
     values::PassArgumentFromAbi& parameterValue = *new (newState.data.data()) values::PassArgumentFromAbi{};

     if (parameterIndex >= platform.IntegerParameterRegisterCount())
     {
          const auto stackParameterOffset = parameterIndex - platform.IntegerParameterRegisterCount();
          const auto stackSlot = platform.StackParameterOrder() == api::StackParameterOrdering::Forward
                                      ? stackParameterOffset
                                      : (numberOfParameters - parameterIndex);

          Width width = MapWidth(this->GetVRM().GetWidth(source));
          const auto minWidth = static_cast<Width>(platform.ParameterStackSlotWidth());
          width = std::max(width, minWidth);
          const auto displacement = std::to_underlying(width) * stackSlot;
          this->_parameterReserve = std::max(this->_parameterReserve, displacement);

          parameterValue.parameters = std::make_tuple(displacement, true, source);

          this->Redefine(source, newState);
          built.append_range(EnsureMaterialisation(source));
          return built;
     }
     const auto abiRegister = static_cast<RegisterIndex>(platform.IntegerParameterRegisterIndex(parameterIndex));
     const Width width = MapWidth(this->GetVRM().GetWidth(source));

     if (this->IsSpilled(source))
     {
          built.push_back(BuildMov(width, RegisterOperand{abiRegister}, this->EnsureStackSlot(source)));
          return built;
     }
     const auto immediate = ImmediateOf(source);
     if (immediate)
     {
          built.push_back(BuildMov(width, RegisterOperand{abiRegister}, IntegerOperand{*immediate}));
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
     const values::PassArgumentFromAbi& parameterValue =
          *std::launder(reinterpret_cast<const values::PassArgumentFromAbi*>(data.data()));

     const auto rawValue = std::get<0>(parameterValue.parameters);
     const bool isStack = std::get<1>(parameterValue.parameters);
     const auto virtualSource = std::get<2>(parameterValue.parameters);

     const Width width = MapWidth(this->GetVRM().GetWidth(owner));

     if (isStack)
     {
          const auto displacement = static_cast<std::uint32_t>(rawValue);
          std::ignore = this->ConsumeUse(virtualSource);

          if (this->IsSpilled(virtualSource))
          {
               const auto sourceSlot = this->EnsureStackSlot(virtualSource);
               const RegisterIndex tempReg = this->_registerAllocator.Allocate(owner);

               return {.instructions = {BuildMov(width, RegisterOperand{tempReg}, sourceSlot),
                                        BuildMov(width, StackOperand{displacement}, RegisterOperand{tempReg})},
                       .assignedRegister = tempReg};
          }

          const RegisterIndex sourceRegister = this->PhysicalRegisterOf(virtualSource);
          this->ReleaseRegister(virtualSource);

          return {.instructions = {BuildMov(width, StackOperand{displacement}, RegisterOperand{sourceRegister})},
                  .assignedRegister = sourceRegister};
     }

     const RegisterIndex abiRegister = static_cast<RegisterIndex>(rawValue);

     if (this->IsSpilled(virtualSource))
     {
          const auto slot = this->EnsureStackSlot(virtualSource);
          std::ignore = this->ConsumeUse(virtualSource);

          return {.instructions = {BuildMov(width, RegisterOperand{abiRegister}, slot)},
                  .assignedRegister = abiRegister};
     }

     const RegisterIndex sourceRegister = this->PhysicalRegisterOf(virtualSource);
     const auto remainingUses = this->ConsumeUse(virtualSource);

     if (sourceRegister == abiRegister)
     {
          return {.instructions = {}, .assignedRegister = abiRegister};
     }

     if (remainingUses == 0 && !this->IsMutable(virtualSource))
     {
          this->ReleaseRegister(virtualSource);
     }

     return {.instructions = {BuildMov(width, RegisterOperand{abiRegister}, RegisterOperand{sourceRegister})},
             .assignedRegister = abiRegister};
}
