#include <algorithm>
#include <cstddef>
#include <format>
#include <new>
#include <span>
#include <utility>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Execution/Context.h"
#include "Machine/Encoders/API/Aggregate.h"
#include "Machine/Encoders/API/Platform.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
#include "RuntimeAssert.h"

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::CopyParameter>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(
          registerArray.size() == 3 || registerArray.size() == 4,
          std::format("Invalid register array! Specified: {}, expected: [scopeIndex, destination, parameterIndex, "
                      "optional aggregate-storage flag]",
                      registerArray.size()));

     const auto scopeIndex = registerArray[0].index;
     const auto& destination = registerArray[1];
     const auto parameterIndex = static_cast<std::size_t>(registerArray[2].index);
     const bool aggregateStorage = registerArray.size() == 4;
     runtime_assert(!aggregateStorage || registerArray[3].index == 1,
                    "Aggregate parameter storage flag must be one");
     const ir::FunctionScope* scope = this->Scopes()[scopeIndex];
     const auto& platform = *this->_target->platform;
     const bool hasResultBuffer = scope->returnsByPointer && typeSystem::IsClass(scope->returnType);
     const bool directAggregateReturn =
          hasResultBuffer && api::IsSingleIntegerRegister(api::ClassReturnPassing(platform, scope->returnType),
                                                          scope->returnType->Size());
     const auto numberOfParameters = scope->CallingConventionParameterCount() -
                                     static_cast<std::size_t>(directAggregateReturn);

     if (aggregateStorage)
     {
          const Width width = MapWidth(this->GetVRM().GetWidth(destination));
          const auto destinationSlot = this->EnsureStackSlot(destination);
          if (parameterIndex >= platform.IntegerParameterRegisterCount())
          {
               const auto stackSlot = this->StackParameterSlot(parameterIndex, numberOfParameters);
               const auto minWidth = static_cast<Width>(platform.ParameterStackSlotWidth());
               const auto slotSize = static_cast<std::size_t>(std::to_underlying(minWidth)) / 8;
               const auto displacement = slotSize * stackSlot;

               const RegisterIndex scratch = RegisterIndex::R10;
               ecpps::detail::AppendRange(built, this->ClearRegister(scratch, destination));
               this->Lock(scratch);
               built.push_back(BuildMov(width, RegisterOperand{scratch},
                                        StackOperand{.offset = static_cast<std::uint32_t>(displacement),
                                                     .isIncomingParameter = true}));
               built.push_back(BuildMov(width, destinationSlot, RegisterOperand{scratch}));
               this->Unlock(scratch);
               return built;
          }

          const auto abiRegister = static_cast<RegisterIndex>(platform.IntegerParameterRegisterIndex(parameterIndex));
          built.push_back(BuildMov(width, destinationSlot, RegisterOperand{abiRegister}));
          this->Unlock(abiRegister);
          return built;
     }

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;
     newState.data.resize(sizeof(values::CopyParameterFromAbi));
     values::CopyParameterFromAbi& parameterValue = *new (newState.data.data()) values::CopyParameterFromAbi{};

     if (parameterIndex >= platform.IntegerParameterRegisterCount())
     {
          const auto stackSlot = this->StackParameterSlot(parameterIndex, numberOfParameters);

          Width width = MapWidth(this->GetVRM().GetWidth(destination));
          const auto minWidth = static_cast<Width>(platform.ParameterStackSlotWidth());
          width = std::max(width, minWidth);
          const auto slotSize = static_cast<std::size_t>(std::to_underlying(minWidth)) / 8;
          const auto displacement = slotSize * stackSlot;
          parameterValue.parameters = std::make_tuple(displacement, true);

          this->Redefine(destination, newState);
          ecpps::detail::AppendRange(built, EnsureMaterialisation(destination));
          return built;
     }
     const auto abiRegister = static_cast<RegisterIndex>(platform.IntegerParameterRegisterIndex(parameterIndex));
     const Width width = MapWidth(this->GetVRM().GetWidth(destination));

     if (this->IsSpilled(destination))
     {
          built.push_back(BuildMov(width, this->EnsureStackSlot(destination), RegisterOperand{abiRegister}));
          this->Unlock(abiRegister);
          return built;
     }

     parameterValue.parameters = std::make_tuple(static_cast<std::size_t>(abiRegister), false);
     this->Redefine(destination, newState);

     this->Unlock(abiRegister);
     ecpps::detail::AppendRange(built, EnsureMaterialisation(destination));

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::CopyParameter>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::CopyParameterFromAbi& parameterValue =
          *std::launder(reinterpret_cast<const values::CopyParameterFromAbi*>(data.data()));

     const auto rawValue = std::get<0>(parameterValue.parameters);
     const bool isStack = std::get<1>(parameterValue.parameters);

     const Width width = MapWidth(this->GetVRM().GetWidth(owner));

     if (isStack)
     {
          const auto displacement = static_cast<std::uint32_t>(rawValue);
          const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);

          return {.instructions = {BuildMov(
                       width, RegisterOperand{destinationRegister},
                       StackOperand{.offset = displacement, .isIncomingParameter = true})},
                  .assignedRegister = destinationRegister};
     }
     const RegisterIndex abiRegister = static_cast<RegisterIndex>(rawValue);

     this->_registerAllocator.Prefer(abiRegister);
     const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);
     this->_registerAllocator.ClearPreference();

     if (destinationRegister == abiRegister) return {.instructions = {}, .assignedRegister = destinationRegister};

     return {.instructions = {BuildMov(width, RegisterOperand{destinationRegister}, RegisterOperand{abiRegister})},
             .assignedRegister = destinationRegister};
}
