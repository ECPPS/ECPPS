#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/VirtualInstructionEncoder.h"
#include "Machine/Machine.h"
#include "RuntimeAssert.h"
namespace ecpps::abi::encoders::x8664
{
     enum struct OperandType : std::uint8_t
     {
          Register,
          Memory,
          Integer
     };

     struct X8664InstructionName
     {
          explicit X8664InstructionName(void) = delete;

          constexpr static std::size_t Mov = 0;
          constexpr static std::size_t Add = 1;
          constexpr static std::size_t Ret = 2;
     };

     enum struct Optimisation : std::uint8_t
     {
          None,
          Moderate,
          Aggressive
     };

     [[nodiscard]] constexpr ir::abstract::AllocationClass SpillThreshold(const Optimisation optimisation) noexcept
     {
          switch (optimisation)
          {
          case Optimisation::None: return ir::abstract::AllocationClass::Allocation;
          case Optimisation::Moderate: return ir::abstract::AllocationClass::ColdAllocation;
          case Optimisation::Aggressive: return ir::abstract::AllocationClass::Invalid;
          }
          std::unreachable();
     }

     inline namespace instructionSetData
     {
          enum struct RegisterIndex : std::uint8_t
          {
               Rax,
               Rcx,
               Rdx,
               Rbx,
               Rsp,
               Rbp,
               Rsi,
               Rdi,
               R8,
               R9,
               R10,
               R11,
               R12,
               R13,
               R14,
               R15,
               Rip
          };

          struct RegisterOperand
          {
               RegisterIndex index{};
          };
          struct MemoryOperand
          {
               RegisterIndex relativeTo{};
               std::int32_t offset{};
          };
          struct IntegerOperand
          {
               std::uint64_t value{};
          };
          using Operand = std::variant<RegisterOperand, MemoryOperand, IntegerOperand>;

          struct AddInstruction
          {
               Operand modifiedDestination{};
               Operand source{};
          };
          struct MovInstruction
          {
               Operand destination{};
               Operand source{};
          };

          [[nodiscard]] std::string ToString(const Operand& operand);
          class PhysicalRegisterAllocator
          {
          public:
               struct Cell
               {
                    RegisterIndex reg;
                    std::optional<ir::abstract::VirtualRegister> owner;
               };

               [[nodiscard]] RegisterIndex Allocate(ir::abstract::VirtualRegister owner)
               {
                    runtime_assert(!this->_colourOf.contains(owner.index),
                                   "Virtual register already holds a physical colour");

                    if (this->_preferred.has_value() && std::ranges::contains(_pool, *this->_preferred) &&
                        !this->_occupancy.contains(*this->_preferred))
                         return this->Claim(owner, *this->_preferred);

                    for (const auto candidate : _pool)
                    {
                         if (this->_occupancy.contains(candidate)) continue;

                         return this->Claim(owner, candidate);
                    }

                    throw TracedException("Out of physical registers: spilling is not implemented");
               }

               void Prefer(const RegisterIndex reg) noexcept
               {
                    this->_preferred = reg;
               }

               void ClearPreference(void) noexcept
               {
                    this->_preferred = std::nullopt;
               }

               void Free(ir::abstract::VirtualRegister owner) noexcept
               {
                    const auto colourIterator = this->_colourOf.find(owner.index);
                    if (colourIterator == this->_colourOf.end()) return;

                    this->_occupancy.erase(colourIterator->second);
                    this->_colourOf.erase(colourIterator);
               }

               void Reassign(ir::abstract::VirtualRegister from, ir::abstract::VirtualRegister to)
               {
                    const auto colourIterator = this->_colourOf.find(from.index);
                    runtime_assert(colourIterator != this->_colourOf.end(),
                                   "Virtual register does not hold a physical colour");
                    runtime_assert(!this->_colourOf.contains(to.index),
                                   "Virtual register already holds a physical colour");

                    const auto colour = colourIterator->second;
                    this->_colourOf.erase(colourIterator);
                    this->_occupancy.insert_or_assign(colour, to);
                    this->_colourOf.emplace(to.index, colour);
               }

               [[nodiscard]] bool IsFree(RegisterIndex reg) const noexcept
               {
                    return !this->_occupancy.contains(reg);
               }

               [[nodiscard]] std::optional<RegisterIndex> ColourOf(ir::abstract::VirtualRegister owner) const noexcept
               {
                    const auto colourIterator = this->_colourOf.find(owner.index);
                    if (colourIterator == this->_colourOf.end()) return std::nullopt;
                    return colourIterator->second;
               }

               [[nodiscard]] std::vector<Cell> Snapshot(void) const
               {
                    std::vector<Cell> cells{};
                    cells.reserve(_pool.size());

                    for (const auto reg : _pool)
                    {
                         const auto occupancyIterator = this->_occupancy.find(reg);
                         cells.push_back(Cell{.reg = reg,
                                              .owner = occupancyIterator == this->_occupancy.end()
                                                            ? std::nullopt
                                                            : std::optional{occupancyIterator->second}});
                    }

                    return cells;
               }

          private:
               RegisterIndex Claim(ir::abstract::VirtualRegister owner, RegisterIndex candidate)
               {
                    this->_occupancy.emplace(candidate, owner);
                    this->_colourOf.emplace(owner.index, candidate);
                    return candidate;
               }

               constexpr static std::array<RegisterIndex, 13> _pool{
                    RegisterIndex::Rax, RegisterIndex::Rcx, RegisterIndex::Rdx, RegisterIndex::Rbx, RegisterIndex::Rsi,
                    RegisterIndex::Rdi, RegisterIndex::R8,  RegisterIndex::R9,  RegisterIndex::R10, RegisterIndex::R11,
                    RegisterIndex::R12, RegisterIndex::R13, RegisterIndex::R14,
               };

               std::unordered_map<RegisterIndex, ir::abstract::VirtualRegister> _occupancy;
               std::unordered_map<std::size_t, RegisterIndex> _colourOf;
               std::optional<RegisterIndex> _preferred{};
          };
     } // namespace instructionSetData

     struct MaterialisationOutcome
     {
          std::vector<ir::abstract::Instruction> instructions;
          RegisterIndex assignedRegister;
     };

     struct X8664VirtualInstructionEncoder final : api::VirtualInstructionEncoder
     {
          explicit X8664VirtualInstructionEncoder(api::Target& target,
                                                  const Optimisation optimisation = Optimisation::None)
              : VirtualInstructionEncoder(ISA::x86_64, target), _optimisation(optimisation)
          {
          }

          [[nodiscard]] std::vector<ir::abstract::Instruction> Encode(
               const std::vector<ir::abstract::VirtualInstruction>& input) final;
          [[nodiscard]] std::string Stringify(const ir::abstract::Instruction& instruction) const final;

          [[nodiscard]] std::size_t StackFrameSize(void) const noexcept
          {
               return this->_stackFrameSize;
          }

     private:
          std::vector<ir::abstract::Instruction> EncodeSingle(const ir::abstract::VirtualInstruction&);
          [[nodiscard]] ir::abstract::VirtualRegisterMap& GetVRM(void) noexcept;

          std::vector<ecpps::ir::abstract::Instruction> EnsureMaterialisation(
               ir::abstract::VirtualRegister virtualRegister);

          void DereferenceAndMaybeFree(ir::abstract::VirtualRegister reg);
          [[nodiscard]] std::size_t ConsumeUse(ir::abstract::VirtualRegister reg);
          void ReleaseRegister(ir::abstract::VirtualRegister reg);
          void TransferRegister(ir::abstract::VirtualRegister from, ir::abstract::VirtualRegister to);

          void Redefine(ir::abstract::VirtualRegister reg, ir::abstract::State value);

          [[nodiscard]] bool IsMutable(ir::abstract::VirtualRegister reg);
          [[nodiscard]] bool IsSpilled(ir::abstract::VirtualRegister reg);
          [[nodiscard]] std::int32_t EnsureStackSlot(ir::abstract::VirtualRegister reg);
          [[nodiscard]] RegisterIndex PhysicalRegisterOf(ir::abstract::VirtualRegister reg);
          [[nodiscard]] std::optional<std::uint64_t> ImmediateOf(ir::abstract::VirtualRegister reg);

          [[nodiscard]] static ir::abstract::Instruction BuildMov(Operand destination, Operand source);
          [[nodiscard]] static ir::abstract::Instruction BuildAdd(Operand modifiedDestination, Operand source);

          template <ir::abstract::VirtualInstructionType TType>
          std::vector<ir::abstract::Instruction> EncoderImplementation(
               const std::vector<ir::abstract::VirtualRegister>& registerArray);

          template <ir::abstract::VirtualInstructionType TType>
          MaterialisationOutcome MaterialisationImplementation(ir::abstract::VirtualRegister owner,
                                                               std::span<const std::byte> data);

          Optimisation _optimisation;
          PhysicalRegisterAllocator _registerAllocator{};
          std::unordered_map<std::size_t, std::int32_t> _stackSlots{};
          std::unordered_map<std::size_t, std::size_t> _remainingUses{};
          std::size_t _stackFrameSize{};
     };
} // namespace ecpps::abi::encoders::x8664
