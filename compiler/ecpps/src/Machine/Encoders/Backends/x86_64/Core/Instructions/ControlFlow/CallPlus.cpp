#include <format>
#include <new>
#include <span>
#include <tuple>
#include <utility>
#include "../../encoder.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
#include "RuntimeAssert.h"

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::CallWithResult>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(registerArray.size() == 2,
                    std::format("Invalid register array! Specified: {}, expected: [destination, function]",
                                registerArray.size()));

     const auto& destination = registerArray[0];
     const auto& function = registerArray[1];

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;

     newState.data.resize(sizeof(values::CallResult));
     values::CallResult& callValue = *new (newState.data.data()) values::CallResult{};
     callValue.parameters = std::make_tuple(function);
     this->Redefine(destination, newState);

     built.append_range(EnsureMaterialisation(destination));

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::CallWithResult>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     const values::CallResult& callValue = *std::launder(reinterpret_cast<const values::CallResult*>(data.data()));

     const Width width = MapWidth(this->GetVRM().GetWidth(owner));
     const auto returnRegister = static_cast<RegisterIndex>(this->_target->platform->IntegerReturnRegisterIndex());

     std::vector<ecpps::ir::abstract::Instruction> built{};
     built.push_back(BuildCall(std::get<0>(callValue.parameters).index));

     this->_registerAllocator.Prefer(returnRegister);
     const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);
     this->_registerAllocator.ClearPreference();

     if (destinationRegister != returnRegister)
          built.push_back(BuildMov(width, RegisterOperand{destinationRegister}, RegisterOperand{returnRegister}));

     return {.instructions = std::move(built), .assignedRegister = destinationRegister};
}
