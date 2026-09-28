#include <cstddef>
#include <format>
#include <new>
#include <span>
#include <utility>
#include <vector>

#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
#include "RuntimeAssert.h"

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::PassArgument>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(registerArray.size() == 2,
                    std::format("Invalid register array! Specified: {}, expected: [source, parameterIndex]",
                                registerArray.size()));

     const auto& source = registerArray[0];
     const auto parameterIndex = static_cast<std::size_t>(registerArray[1].index);
     const auto& platform = *this->_target->platform;

     runtime_assert(parameterIndex < platform.IntegerParameterRegisterCount(),
                    std::format("Parameter {} is passed on the stack, which is not supported yet", parameterIndex));

     const RegisterIndex abiRegister =
          static_cast<RegisterIndex>(platform.IntegerParameterRegisterIndex(parameterIndex));

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

     built.append_range(EnsureMaterialisation(source));

     built.push_back(BuildMov(width, RegisterOperand{abiRegister}, RegisterOperand{PhysicalRegisterOf(source)}));
     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::PassArgument>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::PassArgumentFromAbi& parameterValue =
          *std::launder(reinterpret_cast<const values::PassArgumentFromAbi*>(data.data()));

     const RegisterIndex abiRegister = std::get<0>(parameterValue.parameters);
     const auto virtualSource = std::get<1>(parameterValue.parameters);

     const Width width = MapWidth(this->GetVRM().GetWidth(owner));

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

          return {.instructions = {BuildMov(width, RegisterOperand{abiRegister}, RegisterOperand{sourceRegister})},
                  .assignedRegister = abiRegister};
     }

     return {.instructions = {BuildMov(width, RegisterOperand{abiRegister}, RegisterOperand{sourceRegister})},
             .assignedRegister = abiRegister};
}
