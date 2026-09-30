#include <cstddef>
#include <cstdint>
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
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::LoadStringAddress>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(registerArray.size() == 3,
                    std::format("Invalid register array! Specified: {}, expected: [destination, tableIndex, "
                                "tableOffset]",
                                registerArray.size()));

     const auto& destination = registerArray[0];
     const auto tableIndex = static_cast<std::uint32_t>(registerArray[1].index);
     const auto tableOffset = static_cast<std::uint32_t>(registerArray[2].index);

     runtime_assert(!this->IsSpilled(destination), "String address destination must not be spilled");

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::StringAddress));
     values::StringAddress& stringValue = *new (newState.data.data()) values::StringAddress{};
     stringValue.parameters = std::make_tuple(tableIndex, tableOffset);
     this->Redefine(destination, newState);

     if (this->IsMutable(destination)) built.append_range(EnsureMaterialisation(destination));

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::LoadStringAddress>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::StringAddress& stringValue =
          *std::launder(reinterpret_cast<const values::StringAddress*>(data.data()));
     const auto tableIndex = std::get<0>(stringValue.parameters);
     const auto tableOffset = std::get<1>(stringValue.parameters);

     const Width width = MapWidth(this->GetVRM().GetWidth(owner));
     const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);

     return {.instructions = {BuildLea(width, RegisterOperand{destinationRegister},
                                       StringAddressOperand{.tableIndex = static_cast<std::uint32_t>(tableIndex),
                                                            .offset = static_cast<std::uint32_t>(tableOffset)})},
             .assignedRegister = destinationRegister};
}
