#pragma once
#include <array>
#include <concepts>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ecpps::codegen
{
     template <typename TOperand> struct OperandBase
     {
          explicit OperandBase(const std::size_t size) : _size(size) {}
          static_assert(requires(const TOperand& operand) {
               { operand.ToString() } -> std::same_as<std::string>;
          });
          [[nodiscard]] std::size_t Size(void) const noexcept { return this->_size; }

     protected:
          std::size_t _size;
     };

     enum struct Register : std::uint_fast8_t
     {
          Rax, // rax/eax/ax/al
          Rcx,
          Rdx,
          Rbx,
          Rsp,
          Rbp,
          Rsi,
          Rdi,

          R8, // r8/r8d/r8w/r8b
          R9,
          R10,
          R11,
          R12,
          R13,
          R14,
          R15,

          Mm0, // xmm0/ymm0/zmm0
          Mm1,
          Mm2,
          Mm3,
          Mm4,
          Mm5,
          Mm6,
          Mm7,
          Mm8,
          Mm9,
          Mm10,
          Mm11,
          Mm12,
          Mm13,
          Mm14,
          Mm15,
     };
     enum struct OperandSize : std::uint_least8_t
     {
          Byte = 1,
          Word = 2,
          Dword = 4,
          Qword = 8,
          Mmx = 8,
          Xmm = 16,
          Ymm = 32,
          Zmm = 64
     };

     struct RegisterOperand : OperandBase<RegisterOperand>
     {
          explicit RegisterOperand(const Register index, const std::size_t size) : OperandBase(size), _index(index) {}
          [[nodiscard]] std::string ToString(void) const noexcept;

     private:
          Register _index;
     };

     using Operand = std::variant<RegisterOperand>;

     struct MovInstruction
     {
          Operand source;
          Operand destination;
          OperandSize size;

          explicit MovInstruction(Operand source, Operand destination, const OperandSize size)
               : source(std::move(source)), destination(std::move(destination)), size(size)
          {}
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

     using Instruction = std::variant<MovInstruction>;

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

          static Routine Branchless(std::vector<Instruction>&& instructions)
          {
               return Routine{ std::move(instructions), RoutineCondition::Procedure, RoutineCondition::Procedure };
          }

          static Routine WhileLoop(std::vector<Instruction>&& instructions, const RoutineCondition condition)
          {
               return Routine{ std::move(instructions), RoutineCondition::Procedure, condition };
          }

          static Routine Branch(std::vector<Instruction>&& instructions, const RoutineCondition condition)
          {
               return Routine{ std::move(instructions), condition, RoutineCondition::Procedure };
          }
     private:
          explicit Routine(std::vector<Instruction> instructions, const RoutineCondition skipCondition, const RoutineCondition loopCondition)
               : instructions(std::move(instructions)), skipCondition(skipCondition), loopCondition(loopCondition)
          {}
     };
} // namespace ecpps::codegen

namespace
{
     constexpr std::string ToString(const ecpps::codegen::Register reg, const ecpps::codegen::OperandSize size) noexcept
     {
          using ecpps::codegen::OperandSize;
          using ecpps::codegen::Register;

          static const std::unordered_map<OperandSize, std::map<Register, std::string_view>> registers{
              {OperandSize::Byte,
               {
                   {Register::Rax, "al"},
                   {Register::Rcx, "cl"},
                   {Register::Rdx, "dl"},
                   {Register::Rbx, "bl"},
                   {Register::Rsp, "spl"},
                   {Register::Rbp, "bpl"},
                   {Register::Rsi, "sil"},
                   {Register::Rdi, "dil"},
                   {Register::R8, "r8b"},
                   {Register::R9, "r9b"},
                   {Register::R10, "r10b"},
                   {Register::R11, "r11b"},
                   {Register::R12, "r12b"},
                   {Register::R13, "r13b"},
                   {Register::R14, "r14b"},
                   {Register::R15, "r15b"},
               }},
              {OperandSize::Word,
               {
                   {Register::Rax, "ax"},
                   {Register::Rcx, "cx"},
                   {Register::Rdx, "dx"},
                   {Register::Rbx, "bx"},
                   {Register::Rsp, "sp"},
                   {Register::Rbp, "bp"},
                   {Register::Rsi, "si"},
                   {Register::Rdi, "di"},
                   {Register::R8, "r8w"},
                   {Register::R9, "r9w"},
                   {Register::R10, "r10w"},
                   {Register::R11, "r11w"},
                   {Register::R12, "r12w"},
                   {Register::R13, "r13w"},
                   {Register::R14, "r14w"},
                   {Register::R15, "r15w"},
               }},
              {OperandSize::Dword,
               {
                   {Register::Rax, "eax"},
                   {Register::Rcx, "ecx"},
                   {Register::Rdx, "edx"},
                   {Register::Rbx, "ebx"},
                   {Register::Rsp, "esp"},
                   {Register::Rbp, "ebp"},
                   {Register::Rsi, "esi"},
                   {Register::Rdi, "edi"},
                   {Register::R8, "r8d"},
                   {Register::R9, "r9d"},
                   {Register::R10, "r10d"},
                   {Register::R11, "r11d"},
                   {Register::R12, "r12d"},
                   {Register::R13, "r13d"},
                   {Register::R14, "r14d"},
                   {Register::R15, "r15d"},
               }},
              {OperandSize::Qword,
               {
                   {Register::Rax, "rax"},
                   {Register::Rcx, "rcx"},
                   {Register::Rdx, "rdx"},
                   {Register::Rbx, "rbx"},
                   {Register::Rsp, "rsp"},
                   {Register::Rbp, "rbp"},
                   {Register::Rsi, "rsi"},
                   {Register::Rdi, "rdi"},
                   {Register::R8, "r8"},
                   {Register::R9, "r9"},
                   {Register::R10, "r10"},
                   {Register::R11, "r11"},
                   {Register::R12, "r12"},
                   {Register::R13, "r13"},
                   {Register::R14, "r14"},
                   {Register::R15, "r15"},
               }},
              {OperandSize::Mmx,
               {
                   {Register::Mm0, "mm0"},
                   {Register::Mm1, "mm1"},
                   {Register::Mm2, "mm2"},
                   {Register::Mm3, "mm3"},
                   {Register::Mm4, "mm4"},
                   {Register::Mm5, "mm5"},
                   {Register::Mm6, "mm6"},
                   {Register::Mm7, "mm7"},
                   {Register::Mm8, "mm8"},
                   {Register::Mm9, "mm9"},
                   {Register::Mm10, "mm10"},
                   {Register::Mm11, "mm11"},
                   {Register::Mm12, "mm12"},
                   {Register::Mm13, "mm13"},
                   {Register::Mm14, "mm14"},
                   {Register::Mm15, "mm15"},
               }},
              {OperandSize::Xmm,
               {
                   {Register::Mm0, "xmm0"},
                   {Register::Mm1, "xmm1"},
                   {Register::Mm2, "xmm2"},
                   {Register::Mm3, "xmm3"},
                   {Register::Mm4, "xmm4"},
                   {Register::Mm5, "xmm5"},
                   {Register::Mm6, "xmm6"},
                   {Register::Mm7, "xmm7"},
                   {Register::Mm8, "xmm8"},
                   {Register::Mm9, "xmm9"},
                   {Register::Mm10, "xmm10"},
                   {Register::Mm11, "xmm11"},
                   {Register::Mm12, "xmm12"},
                   {Register::Mm13, "xmm13"},
                   {Register::Mm14, "xmm14"},
                   {Register::Mm15, "xmm15"},
               }},
              {OperandSize::Ymm,
               {
                   {Register::Mm0, "ymm0"},
                   {Register::Mm1, "ymm1"},
                   {Register::Mm2, "ymm2"},
                   {Register::Mm3, "ymm3"},
                   {Register::Mm4, "ymm4"},
                   {Register::Mm5, "ymm5"},
                   {Register::Mm6, "ymm6"},
                   {Register::Mm7, "ymm7"},
                   {Register::Mm8, "ymm8"},
                   {Register::Mm9, "ymm9"},
                   {Register::Mm10, "ymm10"},
                   {Register::Mm11, "ymm11"},
                   {Register::Mm12, "ymm12"},
                   {Register::Mm13, "ymm13"},
                   {Register::Mm14, "ymm14"},
                   {Register::Mm15, "ymm15"},
               }},
              {OperandSize::Zmm,
               {
                   {Register::Mm0, "zmm0"},
                   {Register::Mm1, "zmm1"},
                   {Register::Mm2, "zmm2"},
                   {Register::Mm3, "zmm3"},
                   {Register::Mm4, "zmm4"},
                   {Register::Mm5, "zmm5"},
                   {Register::Mm6, "zmm6"},
                   {Register::Mm7, "zmm7"},
                   {Register::Mm8, "zmm8"},
                   {Register::Mm9, "zmm9"},
                   {Register::Mm10, "zmm10"},
                   {Register::Mm11, "zmm11"},
                   {Register::Mm12, "zmm12"},
                   {Register::Mm13, "zmm13"},
                   {Register::Mm14, "zmm14"},
                   {Register::Mm15, "zmm15"},
               }},
          };

          if (const auto it = registers.find(size); it != registers.end())
          {
               const auto& regMap = it->second;
               if (const auto jt = regMap.find(reg); jt != regMap.end()) return std::string(jt->second);
          }

          return "<invalid-register>";
     }
} // namespace