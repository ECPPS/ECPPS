#include <cstddef>
#include <format>
#include <new>
#include <span>
#include <tuple>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
#include "RuntimeAssert.h"

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::AddressOf>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(
          registerArray.size() == 2,
          std::format("Invalid register array! Specified: {}, expected: [destination, source]", registerArray.size()));

     const auto& destination = registerArray[0];
     const auto& source = registerArray[1];

     runtime_assert(!this->IsSpilled(destination), "Address-of destination must not be spilled");

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::AddressOfAllocation));
     values::AddressOfAllocation& addressValue = *new (newState.data.data()) values::AddressOfAllocation{};
     addressValue.parameters = std::make_tuple(source);
     this->Redefine(destination, newState);

     if (this->IsMutable(destination)) built.append_range(EnsureMaterialisation(destination));

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::AddressOf>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::AddressOfAllocation& addressValue =
          *std::launder(reinterpret_cast<const values::AddressOfAllocation*>(data.data()));
     const auto virtualSource = std::get<0>(addressValue.parameters);

     const Width width = MapWidth(this->GetVRM().GetWidth(owner));

     const auto slot = this->EnsureStackSlot(virtualSource);
     std::ignore = this->ConsumeUse(virtualSource);

     const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);

     return {.instructions = {BuildLea(width, RegisterOperand{destinationRegister}, slot)},
             .assignedRegister = destinationRegister};
}
