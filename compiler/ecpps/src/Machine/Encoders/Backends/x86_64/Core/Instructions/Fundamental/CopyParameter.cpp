#include <algorithm>
#include <cstddef>
#include <format>
#include <new>
#include <span>
#include <utility>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Execution/Context.h"
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
          registerArray.size() == 3,
          std::format("Invalid register array! Specified: {}, expected: [scopeIndex, destination, parameterIndex]",
                      registerArray.size()));

     const auto scopeIndex = registerArray[0].index;
     const auto& destination = registerArray[1];
     const auto parameterIndex = static_cast<std::size_t>(registerArray[2].index);
     const ir::FunctionScope* scope = this->Scopes()[scopeIndex];
     const auto numberOfParameters = scope->parameters.size();
     const auto& platform = *this->_target->platform;

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
          const auto displacement = std::to_underlying(width) * stackSlot;
          this->_parameterReserve = std::max(this->_parameterReserve, displacement);
          parameterValue.parameters = std::make_tuple(displacement, true);

          this->Redefine(destination, newState);
          built.append_range(EnsureMaterialisation(destination));
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
     built.append_range(EnsureMaterialisation(destination));

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

          return {.instructions = {BuildMov(width, RegisterOperand{destinationRegister}, StackOperand{displacement})},
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
