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
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::CopyInteger>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(
          registerArray.size() == 2,
          std::format("Invalid register array! Specified: {}, expected: [destination, literal]", registerArray.size()));

     const auto& destination = registerArray[0];
     const std::uint64_t immediate = static_cast<std::uint64_t>(registerArray[1].index);

     if (this->IsSpilled(destination))
     {
          const Width width = WidthFromSize(this->GetVRM().GetSize(destination));

          built.push_back(BuildMov(width, this->EnsureStackSlot(destination), IntegerOperand{immediate}));
          return built;
     }

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::CopyIntegerToRegister));
     values::CopyIntegerToRegister& copyValue = *new (newState.data.data()) values::CopyIntegerToRegister{};
     copyValue.parameters = std::make_tuple(immediate);
     this->Redefine(destination, newState);

     if (this->IsMutable(destination)) built.append_range(EnsureMaterialisation(destination));

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::CopyInteger>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::CopyIntegerToRegister& copyValue =
          *std::launder(reinterpret_cast<const values::CopyIntegerToRegister*>(data.data()));
     const auto& [immediate] = copyValue.parameters;

     const Width width = WidthFromSize(this->GetVRM().GetSize(owner));
     const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);

     return {.instructions = {BuildMov(width, RegisterOperand{destinationRegister}, IntegerOperand{immediate})},
             .assignedRegister = destinationRegister};
}
