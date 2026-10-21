#include <algorithm>
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
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/MemoryOperations.h"
#include "RuntimeAssert.h"

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Indirection>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray)
{
     std::vector<ecpps::ir::abstract::Instruction> built{};

     runtime_assert(
          registerArray.size() == 2,
          std::format("Invalid register array! Specified: {}, expected: [destination, source]", registerArray.size()));

     const auto destination = registerArray[0];
     const auto source = registerArray[1];

     const bool large = this->IsLargeObject(destination);
     const std::size_t size = std::max<std::size_t>(this->GetVRM().GetSize(destination), 1);

     const bool disposable =
          source != destination && !this->IsMutable(source) && this->_remainingUses[source.index] == 1;

     struct SourcePointer
     {
          RegisterIndex reg;
          std::optional<ScratchRegister> owned;
     };

     const auto acquirePointer = [&]() -> SourcePointer
     {
          if (const auto immediate = this->ImmediateOf(source); immediate.has_value())
          {
               ScratchRegister scratch{this->_registerAllocator};
               built.push_back(BuildMov(Width::W64, scratch.AsOperand(), IntegerOperand{*immediate}));
               const auto reg = scratch.Register();
               return SourcePointer{.reg = reg, .owned = std::move(scratch)};
          }

          if (this->IsSpilled(source))
          {
               const StackOperand slot = this->EnsureStackSlot(source);
               ScratchRegister scratch{this->_registerAllocator};
               built.push_back(BuildMov(Width::W64, scratch.AsOperand(), slot));
               const auto reg = scratch.Register();
               return SourcePointer{.reg = reg, .owned = std::move(scratch)};
          }

          built.append_range(this->EnsureMaterialisation(source));
          const RegisterIndex reg = this->PhysicalRegisterOf(source);
          if (!disposable) return SourcePointer{.reg = reg, .owned = std::nullopt};

          auto taken = ScratchRegister::Take(this->_registerAllocator, source);
          this->GetVRM().ClearMaterialisation(source);
          return SourcePointer{.reg = reg, .owned = std::move(taken)};
     };

     if (large)
     {
          auto pointer = acquirePointer();
          const MemoryLocation sourceLocation{.base = MemoryOperand{.relativeTo = pointer.reg, .offset = 0},
                                              .disposable = pointer.owned.has_value()};

          this->EmitMemoryCopy(built, MemoryLocation{.base = this->EnsureStackSlot(destination)}, sourceLocation, size);

          this->Redefine(destination, ir::abstract::State{});
          this->DereferenceAndMaybeFree(source);
          return built;
     }

     const Width width = MapWidth(this->GetVRM().GetWidth(destination));

     auto pointer = acquirePointer();

     std::optional<ScratchRegister> extra{};
     if (!pointer.owned.has_value()) extra.emplace(this->_registerAllocator);
     ScratchRegister& value = pointer.owned.has_value() ? *pointer.owned : *extra;

     built.push_back(BuildMov(width, value.AsOperand(), MemoryOperand{.relativeTo = pointer.reg, .offset = 0}));

     if (this->IsSpilled(destination))
     {
          built.push_back(BuildMov(width, this->EnsureStackSlot(destination), value.AsOperand()));

          this->Redefine(destination, ir::abstract::State{});
          this->_evicted.insert(destination.index);
          this->DereferenceAndMaybeFree(source);
          return built;
     }

     this->Redefine(destination, ir::abstract::State{});
     value.Commit(destination);
     this->SetMaterialisedRegister(destination, value.Register());
     this->DereferenceAndMaybeFree(source);
     return built;
}

template <>
ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Indirection>(
          const ecpps::ir::abstract::VirtualRegister owner, const std::span<const std::byte> data)
{
     runtime_assert(!this->IsLargeObject(owner), "Large objects cannot be materialised into a register");

     const values::IndirectToRegister& copyValue =
          *std::launder(reinterpret_cast<const values::IndirectToRegister*>(data.data()));
     const auto virtualSource = std::get<1>(copyValue.parameters);

     const Width width = MapWidth(this->GetVRM().GetWidth(owner));

     if (this->IsSpilled(virtualSource))
     {
          const auto slot = this->EnsureStackSlot(virtualSource);
          const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);
          const auto remainingUses = this->ConsumeUse(virtualSource);
          if (remainingUses == 0) this->ReleaseRegister(virtualSource);

          std::vector<ecpps::ir::abstract::Instruction> built{};
          built.emplace_back(BuildMov(Width::W64, RegisterOperand{destinationRegister}, slot));
          built.emplace_back(BuildMov(width, RegisterOperand{destinationRegister},
                                      MemoryOperand{.relativeTo = destinationRegister, .offset = 0}));

          return {.instructions = built, .assignedRegister = destinationRegister};
     }

     const RegisterIndex sourceRegister = this->PhysicalRegisterOf(virtualSource);
     const auto remainingUses = this->ConsumeUse(virtualSource);

     if (remainingUses == 0 && !this->IsMutable(virtualSource))
     {
          this->TransferRegister(virtualSource, owner);
          return {.instructions = {BuildMov(width, RegisterOperand{sourceRegister},
                                            MemoryOperand{.relativeTo = sourceRegister, .offset = 0})},
                  .assignedRegister = sourceRegister};
     }

     const RegisterIndex destinationRegister = this->_registerAllocator.Allocate(owner);
     if (remainingUses == 0) this->ReleaseRegister(virtualSource);

     return {.instructions = {BuildMov(width, RegisterOperand{destinationRegister},
                                       MemoryOperand{.relativeTo = sourceRegister, .offset = 0})},
             .assignedRegister = destinationRegister};
}
