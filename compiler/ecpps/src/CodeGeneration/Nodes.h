#pragma once
#include <array>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include "..\Machine\Storage.h"

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

          [[nodiscard]] const std::shared_ptr<abi::VirtualRegister>& Index(void) const noexcept { return this->_index; }

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

     struct MemoryLocationOperand : OperandBase<MemoryLocationOperand>
     {
          explicit MemoryLocationOperand(RegisterOperand register_, const std::size_t displacement,
                                         const std::size_t width)
              : OperandBase(width), _register(std::move(register_)), _displacement(displacement)
          {
          }
          [[nodiscard]] std::string ToString(void) const noexcept;

          [[nodiscard]] const RegisterOperand& Register(void) const noexcept { return this->_register; }
          [[nodiscard]] std::size_t Displacement(void) const noexcept { return this->_displacement; }

     private:
          RegisterOperand _register;
          std::size_t _displacement;
     };

     struct ErrorOperand
     {
          [[nodiscard]] std::string ToString(void) const noexcept { return ""; }
     };

     using Operand = std::variant<std::monostate, ErrorOperand, RegisterOperand, IntegerOperand, MemoryLocationOperand>;

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

     struct AddInstruction
     {
          Operand from;
          Operand to;
          std::size_t width;
          /// <summary>
          /// For SIMD, unused outside of it in the generic context (might be used for optimisations)
          /// </summary>
          InstructionAlignment alignment{};

          explicit AddInstruction(Operand from, Operand to, const std::size_t width)
              : from(std::move(from)), to(std::move(to)), width(width)
          {
          }
     };

     struct SubInstruction
     {
          Operand from;
          Operand to;
          std::size_t width;
          /// <summary>
          /// For SIMD, unused outside of it in the generic context (might be used for optimisations)
          /// </summary>
          InstructionAlignment alignment{};

          explicit SubInstruction(Operand from, Operand to, const std::size_t width)
              : from(std::move(from)), to(std::move(to)), width(width)
          {
          }
     };

     struct MulInstruction
     {
          Operand from;
          Operand to;
          std::size_t width;
          bool isSigned;
          /// <summary>
          /// For SIMD, unused outside of it in the generic context (might be used for optimisations)
          /// </summary>
          InstructionAlignment alignment{};

          explicit MulInstruction(Operand from, Operand to, const std::size_t width, const bool isSigned)
              : from(std::move(from)), to(std::move(to)), width(width), isSigned(isSigned)
          {
          }
     };

     struct DivInstruction
     {
          Operand from;
          Operand to;
          std::size_t width;
          bool isSigned;
          /// <summary>
          /// For SIMD, unused outside of it in the generic context (might be used for optimisations)
          /// </summary>
          InstructionAlignment alignment{};

          explicit DivInstruction(Operand from, Operand to, const std::size_t width, const bool isSigned)
              : from(std::move(from)), to(std::move(to)), width(width), isSigned(isSigned)
          {
          }
     };

     struct PushInstruction
     {
          Operand source;

          explicit PushInstruction(Operand source) : source(std::move(source)) {}
     };

     struct PopInstruction
     {
          Operand destination;

          explicit PopInstruction(Operand destination) : destination(std::move(destination)) {}
     };

     struct ReturnInstruction
     {
     };

     struct CallInstruction
     {
          std::string functionName;

          explicit CallInstruction(std::string name) : functionName(std::move(name)) {}
     };

     /// <summary>
     /// Custom-defined instruction by architectures. Has no meaning in the generic code generation context
     /// Can be used for architecture-specific optimisations and intrinsics.
     /// </summary>
     struct CustomInstruction
     {
          virtual ~CustomInstruction(void) = default;
          [[nodiscard]] virtual std::string ToString(void) const = 0;

     protected:
          explicit CustomInstruction(void) = default;
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

     using Instruction =
         std::variant<MovInstruction, ReturnInstruction, AddInstruction, MulInstruction, DivInstruction,
                      CallInstruction,
                      SubInstruction /*, PushInstruction, PopInstruction , std::unique_ptr<CustomInstruction>*/>;
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
