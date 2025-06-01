#include "PseudoAssembly.h"
#include <stdexcept>
#include "../Execution/ControlFlow.h"
#include "../Execution/Expressions.h"
#include "../Execution/Procedural.h"

using ecpps::codegen::Instruction;
using ecpps::codegen::Routine;

namespace ir = ecpps::ir;

static ecpps::codegen::Operand ParseExpression(const ecpps::Expression& expression)
{
     const auto& value = expression->Value();
     if (const auto integer = dynamic_cast<ir::IntegralNode*>(value.get()); integer != nullptr)
     {
          const auto size = sizeof(int); // TODO: Wait for the type system for the size...
          return ecpps::codegen::IntegerOperand{integer->Value(), size};
     }

     throw std::logic_error("Invalid expression");
}

static void CompileReturn(std::vector<Instruction>& code, const ecpps::ir::ReturnNode& node)
{
     if (node.HasValue())
     {
          // TODO: Mapping function
          // TODO: Adjust size
          // TODO: ABI...
          code.push_back(ecpps::codegen::MovInstruction{
              ParseExpression(node.Value()),
              ecpps::codegen::RegisterOperand{ecpps::codegen::Register::Rax, sizeof(int)},
              ecpps::codegen::OperandSize::Dword});
     }

     code.push_back(ecpps::codegen::ReturnInstruction{});
}

static Routine CompileRoutine(const ir::ProcedureNode& node)
{
     std::vector<Instruction> instructions{};
     for (const auto& line : node.Body())
     {
          if (const auto returnNode = dynamic_cast<ir::ReturnNode*>(line.get()); returnNode != nullptr)
               CompileReturn(instructions, *returnNode);
     }
     return Routine::Branchless(std::move(instructions), node.Name());
}

void ecpps::codegen::Compile(SourceFile& source, const std::vector<ir::NodePointer>& intermediateRepresentation)
{
     for (const auto& node : intermediateRepresentation)
     {
          if (const auto procedureNode = dynamic_cast<ir::ProcedureNode*>(node.get()); procedureNode != nullptr)
               source.compiledRoutines.push_back(CompileRoutine(*procedureNode));
     }
}
