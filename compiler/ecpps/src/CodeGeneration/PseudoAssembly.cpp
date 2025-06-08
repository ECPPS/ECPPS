#include "PseudoAssembly.h"
#include <ranges>
#include <stdexcept>
#include <utility>
#include "../Execution/ControlFlow.h"
#include "../Execution/Expressions.h"
#include "../Execution/Procedural.h"
#include "../Machine/ABI.h"

using ecpps::codegen::Instruction;
using ecpps::codegen::Routine;

namespace ir = ecpps::ir;

static ecpps::codegen::Operand ParseExpression(const ecpps::Expression& expression)
{
     const auto& value = expression->Value();
     if (const auto integer = dynamic_cast<ir::IntegralNode*>(value.get()); integer != nullptr)
     {
          const auto width = expression->Type()->Size() * ecpps::typeSystem::CharWidth;

          return ecpps::codegen::IntegerOperand{integer->Value(), width};
     }

     throw std::logic_error("Invalid expression");
}

static void CompileReturn(std::vector<Instruction>& code, const ecpps::ir::ReturnNode& node)
{
     if (node.HasValue())
     {
          // TODO: Mapping function
          code.push_back(ecpps::codegen::MovInstruction{
              ParseExpression(node.Value()),
              ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{
                  std::get<ecpps::abi::AllocatedRegister>(ecpps::abi::MicrosoftX64CallingConvention{}
                                                              .ReturnValueStorage(node.Value()->Type()->Size())
                                                              .value)
                      .Ptr()}},
              node.Value()->Type()->Size() * ecpps::typeSystem::CharWidth});
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
     return Routine::Branchless(
         std::move(instructions),
         ecpps::abi::ABI::Current().MangleName(
             node.Linkage(), node.Name(), node.CallingConvention(), node.ReturnType(),
             node.ParameterList() |
                 std::views::transform([](const ecpps::ir::Parameter& parameter) { return parameter.type; }) |
                 std::ranges::to<std::vector>()));
}

void ecpps::codegen::Compile(SourceFile& source, const std::vector<ir::NodePointer>& intermediateRepresentation)
{
     for (const auto& node : intermediateRepresentation)
     {
          if (const auto procedureNode = dynamic_cast<ir::ProcedureNode*>(node.get()); procedureNode != nullptr)
               source.compiledRoutines.push_back(CompileRoutine(*procedureNode));
     }
}
