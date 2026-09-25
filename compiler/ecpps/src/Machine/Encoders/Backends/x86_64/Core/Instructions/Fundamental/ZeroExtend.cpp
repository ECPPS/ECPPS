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
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::ZeroExtension>(
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
          const Width width = WidthFromSize(this->GetVRM().GetSize(destination));

          if (const auto immediate = this->ImmediateOf(source); immediate.has_value())
          {
               built.push_back(BuildMov(width, this->EnsureStackSlot(destination), IntegerOperand{*immediate}));

               this->DereferenceAndMaybeFree(source);
               return built;
          }

          runtime_assert(!this->IsSpilled(source), "Memory to memory copies are not supported");

          built.append_range(EnsureMaterialisation(source));

          const RegisterIndex sourceRegister = this->PhysicalRegisterOf(source);
          built.push_back(BuildMov(width, this->EnsureStackSlot(destination), RegisterOperand{sourceRegister}));

          this->DereferenceAndMaybeFree(source);
          return built;
     }

     if (!this->ImmediateOf(source).has_value()) built.append_range(EnsureMaterialisation(source));

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::ZeroExtendToRegister));
     values::ZeroExtendToRegister& copyValue = *new (newState.data.data()) values::ZeroExtendToRegister{};
     copyValue.parameters = std::make_tuple(destination, source);
     this->Redefine(destination, newState);

     if (this->IsMutable(destination) || this->IsMutable(source))
          built.append_range(EnsureMaterialisation(destination));

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::ZeroExtension>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::ZeroExtendToRegister& copyValue =
          *std::launder(reinterpret_cast<const values::ZeroExtendToRegister*>(data.data()));
     const auto virtualSource = std::get<1>(copyValue.parameters);

     const Width sourceWidth = WidthFromSize(this->GetVRM().GetSize(virtualSource));
     const Width destinationWidth = WidthFromSize(this->GetVRM().GetSize(owner));

     if (this->IsSpilled(virtualSource))
     {
          const auto slot = this->EnsureStackSlot(virtualSource);
          const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);
          std::ignore = this->ConsumeUse(virtualSource);

          return {
               .instructions = {BuildMovzx(destinationWidth, sourceWidth, RegisterOperand{destinationRegister}, slot)},
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
     if (remainingUses == 0) this->ReleaseRegister(virtualSource);

     return {.instructions = {BuildMovzx(destinationWidth, sourceWidth, RegisterOperand{destinationRegister},
                                         RegisterOperand{sourceRegister})},
             .assignedRegister = destinationRegister};
}
