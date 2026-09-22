#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
          constexpr static std::size_t Sub = 3;
          constexpr static std::size_t Push = 4;
          constexpr static std::size_t Pop = 5;
          constexpr static std::size_t LeftShift = 6;
          constexpr static std::size_t RightShift = 7;
          constexpr static std::size_t Xchg = 8;
          constexpr static std::size_t BinaryOr = 9;
          constexpr static std::size_t BinaryAnd = 10;
          constexpr static std::size_t BinaryXor = 11;
          constexpr static std::size_t BitwiseNot = 12;
          constexpr static std::size_t Neg = 13;
          constexpr static std::size_t SignExtend = 14;
     };

     enum struct Optimisation : std::uint8_t
     {
          None,
          Moderate,
          Aggressive
     };

     enum struct FramePointer : std::uint8_t
     {
          Keep,
          Omit
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

          enum struct Width : std::uint8_t
          {
               W8 = 1,
               W16 = 2,
               W32 = 4,
               W64 = 8
          };
          [[nodiscard]] std::string ToString(Width width);
          [[nodiscard]] constexpr Width WidthFromSize(const std::size_t size)
          {
               switch (size)
               {
               case 1: return Width::W8;
               case 2: return Width::W16;
               case 4: return Width::W32;
               case 8: return Width::W64;
               default: throw TracedException(std::format("Unsupported register size for integer copy: {}", size));
               }
          }
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
          struct StackOperand
          {
               std::uint32_t offset{};
          };
          using Operand = std::variant<RegisterOperand, MemoryOperand, IntegerOperand, StackOperand>;

          struct AddInstruction
          {
               Width width{};
               Operand modifiedDestination{};
               Operand source{};
          };
          struct SubInstruction
          {
               Width width{};
               Operand modifiedDestination{};
               Operand source{};
          };
          struct ShiftInstruction
          {
               Width width{};
               Operand modifiedDestination{};
               Operand source{};
          };
          struct MovInstruction
          {
               Width width{};
               Operand destination{};
               Operand source{};
          };
          struct MovExtendInstruction
          {
               Width destinationWidth{};
               Width sourceWidth{};
               Operand destination{};
               Operand source{};
          };
          struct PushInstruction
          {
               RegisterOperand reg{};
          };
          struct PopInstruction
          {
               RegisterOperand reg{};
          };
          struct XchgInstruction
          {
               Width width{};
               Operand modifiedDestination{};
               Operand modifiedSource{};
          };
          struct BinaryOrInstruction
          {
               Width width{};
               Operand modifiedDestination{};
               Operand source{};
          };
          struct BinaryAndInstruction
          {
               Width width{};
               Operand modifiedDestination{};
               Operand source{};
          };

          struct BinaryXorInstruction
          {
               Width width{};
               Operand modifiedDestination{};
               Operand source{};
          };
          struct BitwiseNotInstruction
          {
               Width width{};
               Operand modifiedOperand{};
          };
          struct ArithmeticNegationInstruction
          {
               Width width{};
               Operand modifiedOperand{};
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
               PhysicalRegisterAllocator(void) = default;

               explicit PhysicalRegisterAllocator(const std::function<bool(RegisterIndex)>& isCalleeSaved)
               {
                    std::ranges::stable_partition(this->_pool,
                                                  [&isCalleeSaved](const RegisterIndex candidate)
                                                  {
                                                       return !isCalleeSaved(candidate);
                                                  });
               }

               [[nodiscard]] RegisterIndex Allocate(ir::abstract::VirtualRegister owner)
               {
                    runtime_assert(!this->_colourOf.contains(owner.index),
                                   "Virtual register already holds a physical colour");

                    while (true)
                    {
                         if (this->_preferred.has_value() && std::ranges::contains(_pool, *this->_preferred) &&
                             !this->_occupancy.contains(*this->_preferred))
                              return this->Claim(owner, *this->_preferred);

                         for (const auto candidate : _pool)
                         {
                              if (this->_occupancy.contains(candidate)) continue;

                              return this->Claim(owner, candidate);
                         }

                         if (!this->_onExhausted || !this->_onExhausted())
                              throw TracedException("Out of physical registers and nothing could be evicted");
                    }
               }

               void SetExhaustionHandler(std::function<bool(void)> handler)
               {
                    this->_onExhausted = std::move(handler);
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

               [[nodiscard]] std::size_t FreeCount(void) const noexcept
               {
                    return _pool.size() - this->_occupancy.size();
               }

          private:
               RegisterIndex Claim(ir::abstract::VirtualRegister owner, RegisterIndex candidate)
               {
                    this->_occupancy.emplace(candidate, owner);
                    this->_colourOf.emplace(owner.index, candidate);
                    return candidate;
               }

               std::array<RegisterIndex, 13> _pool{
                    RegisterIndex::Rax, RegisterIndex::Rcx, RegisterIndex::Rdx, RegisterIndex::Rbx, RegisterIndex::Rsi,
                    RegisterIndex::Rdi, RegisterIndex::R8,  RegisterIndex::R9,  RegisterIndex::R10, RegisterIndex::R11,
                    RegisterIndex::R12, RegisterIndex::R13, RegisterIndex::R14,
               };

               std::unordered_map<RegisterIndex, ir::abstract::VirtualRegister> _occupancy;
               std::unordered_map<std::size_t, RegisterIndex> _colourOf;
               std::optional<RegisterIndex> _preferred{};
               std::function<bool(void)> _onExhausted{};
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
                                                  const Optimisation optimisation = Optimisation::Aggressive,
                                                  const FramePointer framePointer = FramePointer::Keep)
              : VirtualInstructionEncoder(ISA::x86_64, target), _optimisation(optimisation), _framePointer(framePointer)
          {
          }

          [[nodiscard]] std::vector<ir::abstract::Instruction> Encode(
               const std::vector<ir::abstract::VirtualInstruction>& input) final;
          [[nodiscard]] std::string Stringify(const ir::abstract::Instruction& instruction) const final;

          void Finalise(std::vector<ir::abstract::Instruction>& instructions) final;

          [[nodiscard]] std::size_t StackFrameSize(void) const noexcept
          {
               return this->_stackFrameSize;
          }

     private:
          [[nodiscard]] std::size_t NextUse(ir::abstract::VirtualRegister reg) const;
          [[nodiscard]] std::vector<ir::abstract::Instruction> Evict(ir::abstract::VirtualRegister victim);
          void SetMaterialisedRegister(ir::abstract::VirtualRegister reg, RegisterIndex physical);
          [[nodiscard]] bool EvictOne(void);
          void CollectDependencies(ir::abstract::VirtualRegister reg, std::unordered_set<std::size_t>& required);
          [[nodiscard]] std::string DescribeRegisterState(void) const;

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
          [[nodiscard]] StackOperand EnsureStackSlot(ir::abstract::VirtualRegister reg);
          [[nodiscard]] RegisterIndex PhysicalRegisterOf(ir::abstract::VirtualRegister reg);
          [[nodiscard]] std::optional<std::uint64_t> ImmediateOf(ir::abstract::VirtualRegister reg);

          [[nodiscard]] static ir::abstract::Instruction BuildMov(Width width, Operand destination, Operand source);
          [[nodiscard]] static ir::abstract::Instruction BuildAdd(Width width, Operand modifiedDestination,
                                                                  Operand source);
          [[nodiscard]] static ir::abstract::Instruction BuildSub(Width width, Operand modifiedDestination,
                                                                  Operand source);
          [[nodiscard]] static ir::abstract::Instruction BuildPush(RegisterOperand reg);
          [[nodiscard]] static ir::abstract::Instruction BuildPop(RegisterOperand reg);
          [[nodiscard]] static ir::abstract::Instruction BuildLeftShift(Width width, Operand modifiedDestination,
                                                                        Operand source);
          [[nodiscard]] static ir::abstract::Instruction BuildRightShift(Width width, Operand modifiedDestination,
                                                                         Operand source);
          [[nodiscard]] static ir::abstract::Instruction BuildXchg(Width width, RegisterOperand modifiedDestination,
                                                                   RegisterOperand source);
          [[nodiscard]] static ir::abstract::Instruction BuildBinaryOr(Width width, Operand modifiedDestination,
                                                                       Operand source);
          [[nodiscard]] static ir::abstract::Instruction BuildBinaryAnd(Width width, Operand modifiedDestination,
                                                                        Operand source);
          [[nodiscard]] static ir::abstract::Instruction BuildBinaryXor(Width width, Operand modifiedDestination,
                                                                        Operand source);
          [[nodiscard]] static ir::abstract::Instruction BuildBitwiseNot(Width width, Operand modifiedOperand);
          [[nodiscard]] static ir::abstract::Instruction BuildArithmeticNegatation(Width width,
                                                                                   Operand modifiedOperand);
          [[nodiscard]] static ir::abstract::Instruction BuildMovsx(Width destinationWidth, Width sourceWidth, Operand destination, Operand source);

          template <ir::abstract::VirtualInstructionType TType>
          std::vector<ir::abstract::Instruction> EncoderImplementation(
               const std::vector<ir::abstract::VirtualRegister>& registerArray);

          template <ir::abstract::VirtualInstructionType TType>
          MaterialisationOutcome MaterialisationImplementation(ir::abstract::VirtualRegister owner,
                                                               std::span<const std::byte> data);

          [[nodiscard]] bool OmitsFramePointer(void) const noexcept
          {
               return this->_framePointer == FramePointer::Omit;
          }

          [[nodiscard]] bool IsClobberable(const RegisterIndex reg)
          {
               if (this->_registerAllocator.IsFree(reg)) return true;
               for (const auto& cell : this->_registerAllocator.Snapshot())
                    if (cell.reg == reg && cell.owner.has_value())
                         return this->_remainingUses[cell.owner->index] == 0; // dead: nobody will read it again
               return false;
          }

          [[nodiscard]] Operand ResolveStackOperand(const Operand& operand) const;
          void ResolveStackOperands(std::vector<ir::abstract::Instruction>& instructions) const;

          [[nodiscard]] std::vector<RegisterIndex> CollectCalleeSavedWrites(
               const std::vector<ir::abstract::Instruction>& instructions) const;
          [[nodiscard]] std::size_t SavedRegistersSize(void) const noexcept
          {
               return this->_savedRegisters.size() * sizeof(std::uint64_t);
          }

          void InsertPrologue(std::vector<ir::abstract::Instruction>& instructions);
          void InsertEpilogue(std::vector<ir::abstract::Instruction>& instructions);

          Optimisation _optimisation;
          FramePointer _framePointer;
          PhysicalRegisterAllocator _registerAllocator{};
          std::unordered_map<std::size_t, std::uint32_t> _stackSlots{};
          std::unordered_map<std::size_t, std::size_t> _remainingUses{};
          std::size_t _localsSize{};
          std::size_t _outgoingReserve{};
          std::size_t _stackFrameSize{};
          std::unordered_set<std::size_t> _evicted{};
          std::unordered_map<std::size_t, std::vector<std::size_t>> _useSites{};
          std::vector<ir::abstract::Instruction> _pendingSpills{};
          std::unordered_set<std::size_t> _evictable{};
          std::vector<RegisterIndex> _savedRegisters{};
     };
} // namespace ecpps::abi::encoders::x8664
