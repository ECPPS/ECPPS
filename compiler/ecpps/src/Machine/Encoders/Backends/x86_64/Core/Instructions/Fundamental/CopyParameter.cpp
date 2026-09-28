#include <cstddef>
#include <format>
#include <new>
#include <span>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
#include "RuntimeAssert.h"

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::CopyParameter>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(registerArray.size() == 2,
                    std::format("Invalid register array! Specified: {}, expected: [destination, parameterIndex]",
                                registerArray.size()));

     const auto& destination = registerArray[0];
     const auto parameterIndex = static_cast<std::size_t>(registerArray[1].index);

     const auto& platform = *this->_target->platform;
     runtime_assert(parameterIndex < platform.IntegerParameterRegisterCount(),
                    std::format("Parameter {} is passed on the stack, which is not supported yet", parameterIndex));

     const auto abiRegister = static_cast<RegisterIndex>(platform.IntegerParameterRegisterIndex(parameterIndex));
     const Width width = MapWidth(this->GetVRM().GetWidth(destination));

     if (this->IsSpilled(destination))
     {
          built.push_back(BuildMov(width, this->EnsureStackSlot(destination), RegisterOperand{abiRegister}));
          return built;
     }

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::CopyParameterFromAbi));
     values::CopyParameterFromAbi& parameterValue = *new (newState.data.data()) values::CopyParameterFromAbi{};
     parameterValue.parameters = std::make_tuple(abiRegister);
     this->Redefine(destination, newState);

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
     const RegisterIndex abiRegister = std::get<0>(parameterValue.parameters);

     const Width width = MapWidth(this->GetVRM().GetWidth(owner));

     this->_registerAllocator.Prefer(abiRegister);
     const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);
     this->_registerAllocator.ClearPreference();

     if (destinationRegister == abiRegister) return {.instructions = {}, .assignedRegister = destinationRegister};

     return {.instructions = {BuildMov(width, RegisterOperand{destinationRegister}, RegisterOperand{abiRegister})},
             .assignedRegister = destinationRegister};
}
