#include "PseudoAssembly.h"
#include <ranges>
#include <stdexcept>
#include <utility>
#include <variant>
#include "../Execution/ControlFlow.h"
#include "../Execution/Expressions.h"
#include "../Execution/Operations.h"
#include "../Execution/Procedural.h"
#include "../Machine/ABI.h"

using ecpps::codegen::Instruction;
using ecpps::codegen::Routine;

namespace ir = ecpps::ir;

static ecpps::codegen::Operand ParseExpression(std::vector<Instruction>& code, const ecpps::Expression& expression)
{
     const auto& value = expression->Value();
     if (const auto integer = dynamic_cast<ir::IntegralNode*>(value.get()); integer != nullptr)
     {
          const auto width = expression->Type()->Size() * ecpps::typeSystem::CharWidth;

          return ecpps::codegen::IntegerOperand{integer->Value(), width};
     }
     if (const auto addition = dynamic_cast<ir::AdditionNode*>(value.get()); addition != nullptr)
     {
          if (addition->Left() == nullptr || addition->Right() == nullptr) return ecpps::codegen::ErrorOperand{};

          const auto left = ParseExpression(code, addition->Left());

          auto storage = std::holds_alternative<ecpps::codegen::RegisterOperand>(left)
               ? ecpps::abi::ABI::Current().AllocateRegister(std::get<ecpps::codegen::RegisterOperand>(left).Index(), ecpps::abi::RegisterAllocation::Priority)
               : ecpps::abi::ABI::Current().AllocateRegister(ecpps::typeSystem::CharWidth *
                                                                     addition->Left()->Type()->Size());

          const auto right = ParseExpression(code, addition->Right());

          if (std::holds_alternative<ecpps::codegen::ErrorOperand>(left) ||
              std::holds_alternative<ecpps::codegen::ErrorOperand>(right))
               return ecpps::codegen::ErrorOperand{};

          if (!std::holds_alternative<ecpps::codegen::RegisterOperand>(left))
               code.emplace_back(
                   ecpps::codegen::MovInstruction{left, ecpps::codegen::RegisterOperand{storage.Ptr()}, storage->width});
          code.emplace_back(
              ecpps::codegen::AddInstruction{right, ecpps::codegen::RegisterOperand{storage.Ptr()}, storage->width});

          return ecpps::codegen::RegisterOperand{storage.Ptr()};
     }

     throw std::logic_error("Invalid expression");
}

static void CompileReturn(std::vector<Instruction>& code, const ecpps::ir::ReturnNode& node)
{
     if (node.HasValue())
     {
          // TODO: Mapping function
          code.push_back(ecpps::codegen::MovInstruction{
              ParseExpression(code, node.Value()),
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
