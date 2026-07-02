#pragma once
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include "../Machine/Storage.h"
#include "CodeGeneration/AbstractNodes.h"

namespace ecpps::codegen
{

     enum struct InstructionAlignment : std::uint_fast8_t
     {
          None,
          Aligned,
          Unaligned
     };

     /// <summary>
     /// Instruction to use for the branch jump. Each one of those is documented by a comment
     /// Procedure does not yield any jumps. None generates jmp.
     /// </summary>
     enum struct RoutineCondition : std::uint_fast8_t
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

          std::vector<ir::abstract::Instruction> instructions;
          std::string name;

          static Routine Branchless(std::vector<ir::abstract::Instruction>&& instructions, std::string name = {})
          {
               return Routine{std::move(instructions), RoutineCondition::Procedure, RoutineCondition::Procedure,
                              std::move(name)};
          }

          static Routine WhileLoop(std::vector<ir::abstract::Instruction>&& instructions,
                                   const RoutineCondition condition)
          { return Routine{std::move(instructions), RoutineCondition::Procedure, condition}; }

          static Routine Branch(std::vector<ir::abstract::Instruction>&& instructions, const RoutineCondition condition)
          { return Routine{std::move(instructions), condition, RoutineCondition::Procedure}; }

     private:
          explicit Routine(std::vector<ir::abstract::Instruction> instructions, const RoutineCondition skipCondition,
                           const RoutineCondition loopCondition, std::string name = {})
              : skipCondition(skipCondition), loopCondition(loopCondition), instructions(std::move(instructions)),
                name(std::move(name))
          {
               if (this->name.empty()) this->name = GenerateName();
          }

          static std::string GenerateName(void);
     };
} // namespace ecpps::codegen
