#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
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

          constexpr static std::size_t Mov = 0; // b = a
          constexpr static std::size_t Add = 1; // c = a + b
          constexpr static std::size_t Ret = 2; // return [a]
     };

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
               std::uint32_t offset{};
          };
          struct IntegerOperand
          {
               std::uint64_t value{};
          };
          using Operand = std::variant<RegisterOperand, MemoryOperand, IntegerOperand>;

          struct AddInstruction // modifiedDestination += source
          {
               Operand modifiedDestination{};
               Operand source{};
          };
          struct MovInstruction // destination = source
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

                    for (const auto candidate : _pool)
                    {
                         if (this->_occupancy.contains(candidate)) continue;

                         this->_occupancy.emplace(candidate, owner);
                         this->_colourOf.emplace(owner.index, candidate);
                         return candidate;
                    }

                    throw TracedException("Out of physical registers: spilling is not implemented");
               }

               void Free(ir::abstract::VirtualRegister owner) noexcept
               {
                    const auto colourIterator = this->_colourOf.find(owner.index);
                    if (colourIterator == this->_colourOf.end()) return;

                    this->_occupancy.erase(colourIterator->second);
                    this->_colourOf.erase(colourIterator);
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
               constexpr static std::array<RegisterIndex, 13> _pool{
                    RegisterIndex::Rax, RegisterIndex::Rcx, RegisterIndex::Rdx, RegisterIndex::Rbx, RegisterIndex::Rsi,
                    RegisterIndex::Rdi, RegisterIndex::R8,  RegisterIndex::R9,  RegisterIndex::R10, RegisterIndex::R11,
                    RegisterIndex::R12, RegisterIndex::R13, RegisterIndex::R14,
               };

               std::unordered_map<RegisterIndex, ir::abstract::VirtualRegister> _occupancy;
               std::unordered_map<std::size_t, RegisterIndex> _colourOf;
          };
     } // namespace instructionSetData

     struct MaterialisationOutcome
     {
          std::vector<ir::abstract::Instruction> instructions;
          RegisterIndex assignedRegister;
     };

     struct X8664VirtualInstructionEncoder final : api::VirtualInstructionEncoder
     {
          explicit X8664VirtualInstructionEncoder(api::Target& target) : VirtualInstructionEncoder(ISA::x86_64, target)
          {
          }

          [[nodiscard]] std::vector<ir::abstract::Instruction> Encode(
               const std::vector<ir::abstract::VirtualInstruction>& input) final;
          [[nodiscard]] std::string Stringify(const ir::abstract::Instruction& instruction) const final;

     private:
          std::vector<ir::abstract::Instruction> EncodeSingle(const ir::abstract::VirtualInstruction&);
          [[nodiscard]] ir::abstract::VirtualRegisterMap& GetVRM(void) noexcept;

          std::vector<ecpps::ir::abstract::Instruction> EnsureMaterialisation(
               ir::abstract::VirtualRegister virtualRegister);

          void DereferenceAndMaybeFree(ir::abstract::VirtualRegister reg);

          void Redefine(ir::abstract::VirtualRegister reg, ir::abstract::State value);

          template <ir::abstract::VirtualInstructionType TType>
          std::vector<ir::abstract::Instruction> EncoderImplementation(
               const std::vector<ir::abstract::VirtualRegister>& registerArray);

          template <ir::abstract::VirtualInstructionType TType>
          MaterialisationOutcome MaterialisationImplementation(ir::abstract::VirtualRegister owner,
                                                               std::span<const std::byte> data);

          PhysicalRegisterAllocator _registerAllocator{};
     };
} // namespace ecpps::abi::encoders::x8664
