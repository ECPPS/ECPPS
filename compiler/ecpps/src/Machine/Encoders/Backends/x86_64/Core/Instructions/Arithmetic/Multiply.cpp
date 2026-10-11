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
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Multiplication>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     using namespace ecpps;
     using namespace ecpps::abi::encoders::x8664;

     runtime_assert(registerArray.size() == 3,
                    std::format("Invalid register array! Specified: {}, expected: [destination, left, right]",
                                registerArray.size()));

     const auto& destination = registerArray[0];
     const auto& left = registerArray[1];
     const auto& right = registerArray[2];

     runtime_assert(!this->IsSpilled(left) && !this->IsSpilled(right) && !this->IsSpilled(destination),
                    "Multiplication operands must not be spilled");

     std::vector<ir::abstract::Instruction> built{};

     if (!this->ImmediateOf(right).has_value()) built.append_range(this->EnsureMaterialisation(right));

     if (!this->ImmediateOf(left).has_value()) built.append_range(this->EnsureMaterialisation(left));

     ir::abstract::State newState{};
     newState.type = ir::abstract::StateType::Allocation;
     newState.data.resize(sizeof(values::MultiplicationRegisters));

     auto& multiplication = *new (newState.data.data()) values::MultiplicationRegisters{};

     multiplication.parameters = std::make_tuple(left, right);

     this->Redefine(destination, newState);

     if (this->IsMutable(left) || this->IsMutable(right) || this->IsMutable(destination))
     {
          built.append_range(this->EnsureMaterialisation(destination));
     }

     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Multiplication>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     using namespace ecpps;
     using namespace ecpps::abi::encoders::x8664;

     const auto& value = *std::launder(reinterpret_cast<const values::MultiplicationRegisters*>(data.data()));

     const auto left = std::get<0>(value.parameters);
     const auto right = std::get<1>(value.parameters);

     constexpr auto rax = RegisterIndex::Rax;
     constexpr auto rdx = RegisterIndex::Rdx;

     const Width width = MapWidth(this->GetVRM().GetWidth(owner));

     std::vector<ir::abstract::Instruction> built{};

     const auto leftImmediate = this->ImmediateOf(left);
     const auto rightImmediate = this->ImmediateOf(right);

     RegisterIndex rightRegister{};

     if (rightImmediate.has_value())
     {
          rightRegister = this->_registerAllocator.Allocate(owner);

          built.push_back(BuildMov(width, RegisterOperand{rightRegister}, IntegerOperand{*rightImmediate}));
     }
     else
     {
          rightRegister = this->PhysicalRegisterOf(right);
     }

     runtime_assert(rightRegister != rax && rightRegister != rdx, "MUL source must not use RAX or RDX");

     if (leftImmediate.has_value())
     {
          built.push_back(BuildMov(width, RegisterOperand{rax}, IntegerOperand{*leftImmediate}));
     }
     else
     {
          const RegisterIndex leftRegister = this->PhysicalRegisterOf(left);

          if (leftRegister != rax)
          {
               built.push_back(BuildMov(width, RegisterOperand{rax}, RegisterOperand{leftRegister}));
          }
     }

     built.push_back(BuildMul(width, RegisterOperand{rightRegister}));

     if (!leftImmediate.has_value()) this->DereferenceAndMaybeFree(left);

     if (!rightImmediate.has_value()) this->DereferenceAndMaybeFree(right);

     return {.instructions = std::move(built), .assignedRegister = rax};
}
