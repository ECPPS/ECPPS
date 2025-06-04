#include "PseudoAssembly.h"
#include "../Execution/ControlFlow.h"
#include "../Execution/Expressions.h"
#include "../Execution/Procedural.h"
#include <stdexcept>

using ecpps::codegen::Instruction;
using ecpps::codegen::Routine;

namespace ir = ecpps::ir;

static ecpps::codegen::Operand ParseExpression(const ecpps::Expression& expression)
{
     const auto& value = expression->Value();
     if (const auto integer = dynamic_cast<ir::IntegralNode*>(value.get()); integer != nullptr)
     {
          const auto width = sizeof(int); // TODO: Wait for the type system for the width...
          return ecpps::codegen::IntegerOperand{integer->Value(), width};
     }

     throw std::logic_error("Invalid expression");
}

static void CompileReturn(std::vector<Instruction>& code, const ecpps::ir::ReturnNode& node)
{
     if (node.HasValue())
     {
          // TODO: Mapping function
          // TODO: Adjust width
          // TODO: ABI...
          code.push_back(ecpps::codegen::MovInstruction{
              ParseExpression(node.Value()),
              ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{
                  std::get<ecpps::abi::AllocatedRegister>(
                      ecpps::abi::MicrosoftX64CallingConvention{}.ReturnValueStorage(4).value)
                      .Ptr()}},
              32});
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
