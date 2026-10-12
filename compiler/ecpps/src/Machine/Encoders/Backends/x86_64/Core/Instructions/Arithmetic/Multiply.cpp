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

     const auto leftImmediate = this->ImmediateOf(left);
     const auto rightImmediate = this->ImmediateOf(right);

     std::vector<ir::abstract::Instruction> built{};

     if (!leftImmediate.has_value() && this->IsSpilled(left)) built.append_range(this->EnsureMaterialisation(left));
     if (!rightImmediate.has_value() && this->IsSpilled(right)) built.append_range(this->EnsureMaterialisation(right));

     RegisterIndex sourceRegister{};

     if (rightImmediate.has_value())
     {
          sourceRegister = this->_registerAllocator.Allocate(owner);

          built.push_back(BuildMov(width, RegisterOperand{sourceRegister}, IntegerOperand{*rightImmediate}));
     }
     else
     {
          sourceRegister = this->PhysicalRegisterOf(right);
     }

     if (sourceRegister == rax || sourceRegister == rdx)
     {
          const RegisterIndex originalRegister = sourceRegister;

          this->_registerAllocator.Prefer(RegisterIndex::Rcx);
          sourceRegister = this->_registerAllocator.Allocate(owner);
          this->_registerAllocator.ClearPreference();

          built.push_back(BuildMov(width, RegisterOperand{sourceRegister}, RegisterOperand{originalRegister}));
     }

     RegisterIndex leftRegister{};

     if (leftImmediate.has_value())
     {
          leftRegister = rax;
     }
     else
     {
          leftRegister = this->PhysicalRegisterOf(left);
     }

     if (leftRegister == rdx)
     {
          this->_registerAllocator.Prefer(RegisterIndex::Rcx);
          const RegisterIndex temporary = this->_registerAllocator.Allocate(owner);
          this->_registerAllocator.ClearPreference();

          built.push_back(BuildMov(width, RegisterOperand{temporary}, RegisterOperand{leftRegister}));

          leftRegister = temporary;
     }

     if (!this->IsClobberable(rax)) built.append_range(this->ClearRegister(rax, owner));
     if (!this->IsClobberable(rdx)) built.append_range(this->ClearRegister(rdx, owner));

     if (leftImmediate.has_value())
     {
          built.push_back(BuildMov(width, RegisterOperand{rax}, IntegerOperand{*leftImmediate}));
     }
     else if (leftRegister != rax)
     {
          built.push_back(BuildMov(width, RegisterOperand{rax}, RegisterOperand{leftRegister}));
     }

     built.push_back(BuildMul(width, RegisterOperand{sourceRegister}));

     const auto leftRemainingUses = this->ConsumeUse(left);

     if (!leftImmediate.has_value() && leftRemainingUses == 0 && !this->IsMutable(left) && leftRegister != rax)
     {
          this->ReleaseRegister(left);
     }

     this->DereferenceAndMaybeFree(right);

     return {.instructions = std::move(built), .assignedRegister = rax};
}
