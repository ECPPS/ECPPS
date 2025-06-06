#pragma once
#include <array>
#include <concepts>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include "..\Machine\ABI.h"

namespace ecpps::codegen
{
     template <typename TOperand> struct OperandBase
     {
          explicit OperandBase(const std::size_t width) : _size(width) {}
          /* static_assert(requires(const TOperand& operand) {
                { operand.ToString() } -> std::same_as<std::string>;
           });*/
          [[nodiscard]] std::size_t Size(void) const noexcept { return this->_size; }

     protected:
          std::size_t _size;
     };

     struct RegisterOperand : OperandBase<RegisterOperand>
     {
          explicit RegisterOperand(std::shared_ptr<abi::VirtualRegister> index)
              : OperandBase(index->width), _index(std::move(index))
          {
          }

          [[nodiscard]] std::string ToString(void) const noexcept;

     private:
          std::shared_ptr<abi::VirtualRegister> _index;
     };

     struct IntegerOperand : OperandBase<IntegerOperand>
     {
          explicit IntegerOperand(const std::size_t value, const std::size_t width) : OperandBase(width), _value(value)
          {
          }
          [[nodiscard]] std::string ToString(void) const noexcept;

          [[nodiscard]] std::size_t Value(void) const noexcept { return this->_value; }

     private:
          std::size_t _value;
     };

     using Operand = std::variant<RegisterOperand, IntegerOperand>;

     enum struct InstructionAlignment : std::uint_fast8_t
     {
          None,
          Aligned,
          Unaligned
     };

     struct MovInstruction
     {
          Operand source;
          Operand destination;
          std::size_t width;
          InstructionAlignment alignment{};

          explicit MovInstruction(Operand source, Operand destination, const std::size_t width)
              : source(std::move(source)), destination(std::move(destination)), width(width)
          {
          }
     };

     struct ReturnInstruction
     {
     };

     /// <summary>
     /// Instruction to use for the branch jump. Each one of those is documented by a comment
     /// Procedure does not yield any jumps. None generates jmp.
     /// </summary>
     enum struct RoutineCondition
     {
          /// <summary>
          /// je (jump if equal / zero flag set)
          /// </summary>
          Equal,
          /// <summary>
          /// jne (jump if not equal / zero flag not set)
          /// </summary>
          NotEqual,
          /// <summary>
          /// jmp (unconditional jump)
          /// </summary>
          None,
          /// <summary>
          /// (no-op) Represents a procedure, no branch instruction
          /// </summary>
          Procedure,
          /// <summary>
          /// ja (jump if above; unsigned >, CF=0 and ZF=0)
          /// </summary>
          Above,
          /// <summary>
          /// jae (jump if above or equal; unsigned >=, CF=0)
          /// </summary>
          AboveOrEqual,
          /// <summary>
          /// jb (jump if below; unsigned <, CF=1)
          /// </summary>
          Below,
          /// <summary>
          /// jbe (jump if below or equal; unsigned <=, CF=1 or ZF=1)
          /// </summary>
          BelowOrEqual,
          /// <summary>
          /// jl (jump if less; signed <, SF != OF)
          /// </summary>
          Less,
          /// <summary>
          /// jle (jump if less or equal; signed <=, ZF=1 or SF != OF)
          /// </summary>
          LessOrEqual,
          /// <summary>
          /// jg (jump if greater; signed >, ZF=0 and SF == OF)
          /// </summary>
          Greater,
          /// <summary>
          /// jge (jump if greater or equal; signed >=, SF == OF)
          /// </summary>
          GreaterOrEqual,
          /// <summary>
          /// jz (jump if zero flag set)
          /// </summary>
          ZeroFlag,
          /// <summary>
          /// jnz (jump if zero flag not set)
          /// </summary>
          NoZeroFlag,
          /// <summary>
          /// jc (jump if carry flag set)
          /// </summary>
          CarryFlag,
          /// <summary>
          /// jnc (jump if carry flag not set)
          /// </summary>
          NoCarryFlag,
     };

     using Instruction = std::variant<MovInstruction, ReturnInstruction>;
     [[nodiscard]] std::string ToString(const Instruction& instruction);

     struct Routine
     {
          /// <summary>
          /// Instruction generated to SKIP the routine entirely, located at the very beginning
          /// </summary>
          RoutineCondition skipCondition;
          /// <summary>
          /// Instruction generated to loop the routine, placed at the end
          /// </summary>
          RoutineCondition loopCondition;

          std::vector<Instruction> instructions;
          std::string name;

          static Routine Branchless(std::vector<Instruction>&& instructions, std::string name = {})
          {
               return Routine{std::move(instructions), RoutineCondition::Procedure, RoutineCondition::Procedure,
                              std::move(name)};
          }

          static Routine WhileLoop(std::vector<Instruction>&& instructions, const RoutineCondition condition)
          {
               return Routine{std::move(instructions), RoutineCondition::Procedure, condition};
          }

          static Routine Branch(std::vector<Instruction>&& instructions, const RoutineCondition condition)
          {
               return Routine{std::move(instructions), condition, RoutineCondition::Procedure};
          }

     private:
          explicit Routine(std::vector<Instruction> instructions, const RoutineCondition skipCondition,
                           const RoutineCondition loopCondition, std::string name = {})
              : instructions(std::move(instructions)), skipCondition(skipCondition), loopCondition(loopCondition),
                name(std::move(name))
          {
               if (this->name.empty()) this->name = GenerateName();
          }

          static std::string GenerateName(void)
          {
               static std::size_t index{};
               return ".LOC" + std::to_string(++index);
          }
     };
} // namespace ecpps::codegen
