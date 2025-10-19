#include "PseudoAssembly.h"
#include <Assert.h>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <variant>
#include "../Execution/ControlFlow.h"
#include "../Execution/Expressions.h"
#include "../Execution/Operations.h"
#include "../Execution/Procedural.h"
#include "../Machine/ABI.h"
#include "../TypeSystem/ArithmeticTypes.h"

using ecpps::codegen::Instruction;
using ecpps::codegen::Routine;

namespace ir = ecpps::ir;

std::unordered_set<std::string> ecpps::codegen::g_functionImports{};

static ecpps::codegen::Operand ParseExpression(std::vector<Instruction>& code, const ecpps::Expression& expression)
{
     if (expression == nullptr) return std::monostate{};

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

          const auto& type = expression->Type();
          std::size_t sizeInBytes = 0;
          if (ecpps::typeSystem::IsIntegral(type))
          {
               const auto integralType = std::dynamic_pointer_cast<ecpps::typeSystem::IntegralType>(type);
               sizeInBytes = integralType->Size();
          }
          else
          {
               // TODO: Assert IsFloatingPoint & add float support
          }

          auto destinationStorage =
              std::holds_alternative<ecpps::codegen::RegisterOperand>(left)
                  ? ecpps::abi::ABI::Current().AllocateRegister(std::get<ecpps::codegen::RegisterOperand>(left).Index(),
                                                                ecpps::abi::RegisterAllocation::Priority)
                  : ecpps::abi::ABI::Current().AllocateRegister(ecpps::typeSystem::CharWidth * sizeInBytes);

          const auto right = ParseExpression(code, addition->Right());

          if (std::holds_alternative<ecpps::codegen::ErrorOperand>(left) ||
              std::holds_alternative<ecpps::codegen::ErrorOperand>(right))
               return ecpps::codegen::ErrorOperand{};

          if (!std::holds_alternative<ecpps::codegen::RegisterOperand>(left))
               code.emplace_back(ecpps::codegen::MovInstruction{
                   left, ecpps::codegen::RegisterOperand{destinationStorage.Ptr()}, destinationStorage->width});
          code.emplace_back(ecpps::codegen::AddInstruction{
              right, ecpps::codegen::RegisterOperand{destinationStorage.Ptr()}, destinationStorage->width});

          return ecpps::codegen::RegisterOperand{destinationStorage.Ptr()};
     }
     if (const auto subtraction = dynamic_cast<ir::SubtractionNode*>(value.get()); subtraction != nullptr)
     {
          if (subtraction->Left() == nullptr || subtraction->Right() == nullptr) return ecpps::codegen::ErrorOperand{};

          const auto left = ParseExpression(code, subtraction->Left());

          const auto& type = expression->Type();
          std::size_t sizeInBytes = 0;
          if (ecpps::typeSystem::IsIntegral(type))
          {
               const auto integralType = std::dynamic_pointer_cast<ecpps::typeSystem::IntegralType>(type);
               sizeInBytes = integralType->Size();
          }
          else
          {
               // TODO: Assert IsFloatingPoint & add float support
          }

          auto destinationStorage =
              std::holds_alternative<ecpps::codegen::RegisterOperand>(left)
                  ? ecpps::abi::ABI::Current().AllocateRegister(std::get<ecpps::codegen::RegisterOperand>(left).Index(),
                                                                ecpps::abi::RegisterAllocation::Priority)
                  : ecpps::abi::ABI::Current().AllocateRegister(ecpps::typeSystem::CharWidth * sizeInBytes);

          const auto right = ParseExpression(code, subtraction->Right());

          if (std::holds_alternative<ecpps::codegen::ErrorOperand>(left) ||
              std::holds_alternative<ecpps::codegen::ErrorOperand>(right))
               return ecpps::codegen::ErrorOperand{};

          if (!std::holds_alternative<ecpps::codegen::RegisterOperand>(left))
               code.emplace_back(ecpps::codegen::MovInstruction{
                   left, ecpps::codegen::RegisterOperand{destinationStorage.Ptr()}, destinationStorage->width});
          code.emplace_back(ecpps::codegen::SubInstruction{
              right, ecpps::codegen::RegisterOperand{destinationStorage.Ptr()}, destinationStorage->width});

          return ecpps::codegen::RegisterOperand{destinationStorage.Ptr()};
     }

     if (const auto multiplication = dynamic_cast<ir::MultiplicationNode*>(value.get()); multiplication != nullptr)
     {
          if (multiplication->Left() == nullptr || multiplication->Right() == nullptr)
               return ecpps::codegen::ErrorOperand{};

          const auto& type = expression->Type();
          std::size_t sizeInBytes = 0;
          bool isSigned = false;
          if (ecpps::typeSystem::IsIntegral(type))
          {
               const auto integralType = std::dynamic_pointer_cast<ecpps::typeSystem::IntegralType>(type);
               sizeInBytes = integralType->Size();
               isSigned = integralType->Sign() == ecpps::typeSystem::Signedness::Signed;
          }
          else
          {
               // TODO: Assert IsFloatingPoint & add float support
          }

          const auto left = ParseExpression(code, multiplication->Left());

          auto storage =
              std::holds_alternative<ecpps::codegen::RegisterOperand>(left)
                  ? ecpps::abi::ABI::Current().AllocateRegister(std::get<ecpps::codegen::RegisterOperand>(left).Index(),
                                                                ecpps::abi::RegisterAllocation::Priority)
                  : ecpps::abi::ABI::Current().AllocateRegister(ecpps::typeSystem::CharWidth * sizeInBytes);

          const auto right = ParseExpression(code, multiplication->Right());

          if (std::holds_alternative<ecpps::codegen::ErrorOperand>(left) ||
              std::holds_alternative<ecpps::codegen::ErrorOperand>(right))
               return ecpps::codegen::ErrorOperand{};

          if (!std::holds_alternative<ecpps::codegen::RegisterOperand>(left))
               code.emplace_back(ecpps::codegen::MovInstruction{left, ecpps::codegen::RegisterOperand{storage.Ptr()},
                                                                storage->width});
          code.emplace_back(ecpps::codegen::MulInstruction{right, ecpps::codegen::RegisterOperand{storage.Ptr()},
                                                           storage->width, isSigned});

          return ecpps::codegen::RegisterOperand{storage.Ptr()};
     }

     if (const auto div = dynamic_cast<ir::DivideNode*>(value.get()); div != nullptr)
     {
          if (div->Left() == nullptr || div->Right() == nullptr) return ecpps::codegen::ErrorOperand{};

          const auto& type = expression->Type();
          std::size_t sizeInBytes = 0;
          bool isSigned = false;
          if (ecpps::typeSystem::IsIntegral(type))
          {
               const auto integralType = std::dynamic_pointer_cast<ecpps::typeSystem::IntegralType>(type);
               sizeInBytes = integralType->Size();
               isSigned = integralType->Sign() == ecpps::typeSystem::Signedness::Signed;
          }
          else
          {
               // TODO: Assert IsFloatingPoint & add float support
          }

          const auto left = ParseExpression(code, div->Left());

          auto storage =
              std::holds_alternative<ecpps::codegen::RegisterOperand>(left)
                  ? ecpps::abi::ABI::Current().AllocateRegister(std::get<ecpps::codegen::RegisterOperand>(left).Index(),
                                                                ecpps::abi::RegisterAllocation::Priority)
                  : ecpps::abi::ABI::Current().AllocateRegister(ecpps::typeSystem::CharWidth * sizeInBytes);

          const auto right = ParseExpression(code, div->Right());

          if (std::holds_alternative<ecpps::codegen::ErrorOperand>(left) ||
              std::holds_alternative<ecpps::codegen::ErrorOperand>(right))
               return ecpps::codegen::ErrorOperand{};

          if (!std::holds_alternative<ecpps::codegen::RegisterOperand>(left))
               code.emplace_back(ecpps::codegen::MovInstruction{left, ecpps::codegen::RegisterOperand{storage.Ptr()},
                                                                storage->width});
          code.emplace_back(ecpps::codegen::DivInstruction{right, ecpps::codegen::RegisterOperand{storage.Ptr()},
                                                           storage->width, isSigned});

          return ecpps::codegen::RegisterOperand{storage.Ptr()};
     }
     if (const auto call = dynamic_cast<ir::FunctionCallNode*>(value.get()); call != nullptr)
     {
          const auto& function = *call->Function();

          auto& currentAbi = ecpps::abi::ABI::Current();
          const std::string functionName = currentAbi.MangleName(
              function.linkage, function.name, function.callingConvention, function.returnType,
              function.parameters |
                  std::views::transform([](const ir::FunctionScope::Parameter& parameter) { return parameter.type; }) |
                  std::ranges::to<std::vector>());
          const auto& callingConvention = currentAbi.CallingConventionFromName(function.callingConvention);

          runtime_assert(function.parameters.size() == call->Arguments().size(),
                         "Overload resolution failed to pick a function with equal amount of arguments");

          const auto returnTypeSize = callingConvention.GetRequirementsForType(function.returnType);
          const auto parameterSizes =
              function.parameters |
              std::views::transform([&callingConvention](const ir::FunctionScope::Parameter& parameter)
                                    { return callingConvention.GetRequirementsForType(parameter.type); }) |
              std::ranges::to<std::vector>();

          const auto argumentStorage = callingConvention.LocateParameters(returnTypeSize, parameterSizes);
          auto argumentIterator = call->Arguments().begin();
          auto parameterIterator = function.parameters.begin();
          runtime_assert(argumentStorage.size() == call->Arguments().size(),
                         "Failed to acquire storage for the arguments");
          for (const auto& storage : argumentStorage)
          {
               const auto& argument = *argumentIterator++;
               const auto& parameter = *parameterIterator++;

               const auto operand = ParseExpression(code, argument);
               code.emplace_back(
                   ecpps::codegen::MovInstruction{operand,
                                                  ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{
                                                      std::get<ecpps::abi::AllocatedRegister>(storage.value).Ptr()}},
                                                  parameter.type->Size() * CHAR_BIT});
          }

          if (function.isDllImportExport) ecpps::codegen::g_functionImports.emplace(functionName);
          code.emplace_back(ecpps::codegen::CallInstruction{functionName});

          const auto returnStorage = callingConvention.ReturnValueStorage(returnTypeSize);
          if (std::holds_alternative<std::monostate>(returnStorage.value))
               return ecpps::codegen::Operand{std::monostate{}};
          return ecpps::codegen::Operand{
              ecpps::codegen::RegisterOperand{std::get<ecpps::abi::AllocatedRegister>(returnStorage.value).Ptr()}};
     }

     throw std::logic_error("Invalid expression");
}

static void CompileReturn(std::vector<Instruction>& code, const ecpps::ir::ReturnNode& node)
{
     if (node.HasValue())
     {
          // TODO: Mapping function
          ecpps::abi::MicrosoftX64CallingConvention callingConvention{};
          const auto returnStorage =
              callingConvention.ReturnValueStorage(callingConvention.GetRequirementsForType(node.Value()->Type()));
          code.push_back(
              ecpps::codegen::MovInstruction{ParseExpression(code, node.Value()),
                                             ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{
                                                 std::get<ecpps::abi::AllocatedRegister>(returnStorage.value).Ptr()}},
                                             node.Value()->Type()->Size() * ecpps::typeSystem::CharWidth});
     }

     code.push_back(ecpps::codegen::ReturnInstruction{});
}

static Routine CompileRoutine(const ir::ProcedureNode& node)
{
     std::vector<Instruction> instructions{};
     auto& currentAbi = ecpps::abi::ABI::Current();
     const auto& parentCallingConvention = currentAbi.CallingConventionFromName(node.CallingConvention());

     auto stackManager = parentCallingConvention.BeginStack(instructions);

     for (const auto& line : node.Body())
     {
          if (const auto returnNode = dynamic_cast<ir::ReturnNode*>(line.get()); returnNode != nullptr)
               CompileReturn(instructions, *returnNode);
          else if (const auto call = dynamic_cast<ir::FunctionCallNode*>(line.get()); call != nullptr)
          {
               const auto& function = *call->Function();

               const std::string functionName = currentAbi.MangleName(
                   function.linkage, function.name, function.callingConvention, function.returnType,
                   function.parameters |
                       std::views::transform([](const ir::FunctionScope::Parameter& parameter)
                                             { return parameter.type; }) |
                       std::ranges::to<std::vector>());
               const auto& callingConvention = currentAbi.CallingConventionFromName(function.callingConvention);
               const auto returnTypeSize = callingConvention.GetRequirementsForType(function.returnType);
               const auto parameterSizes =
                   function.parameters |
                   std::views::transform([&callingConvention](const ir::FunctionScope::Parameter& parameter)
                                         { return callingConvention.GetRequirementsForType(parameter.type); }) |
                   std::ranges::to<std::vector>();

               const auto argumentStorage = callingConvention.LocateParameters(returnTypeSize, parameterSizes);
               auto argumentIterator = call->Arguments().begin();
               auto parameterIterator = function.parameters.begin();
               runtime_assert(argumentStorage.size() == call->Arguments().size(),
                              "Failed to acquire storage for the arguments");
               for (const auto& storage : argumentStorage)
               {
                    const auto& argument = *argumentIterator++;
                    const auto& parameter = *parameterIterator++;

                    const auto operand = ParseExpression(instructions, argument);
                    instructions.emplace_back(ecpps::codegen::MovInstruction{
                        operand,
                        ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{
                            std::get<ecpps::abi::AllocatedRegister>(storage.value).Ptr()}},
                        parameter.type->Size() * CHAR_BIT});
               }

               if (function.isDllImportExport) ecpps::codegen::g_functionImports.emplace(functionName);
               instructions.emplace_back(ecpps::codegen::CallInstruction{functionName});
          }
     }

     stackManager->Finish();

     return Routine::Branchless(std::move(instructions),
                                ecpps::abi::ABI::Current().MangleName(
                                    node.Linkage(), node.Name(), node.CallingConvention(), node.ReturnType(),
                                    node.ParameterList() |
                                        std::views::transform([](const ecpps::ir::FunctionScope::Parameter& parameter)
                                                              { return parameter.type; }) |
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
