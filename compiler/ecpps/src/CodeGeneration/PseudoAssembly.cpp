#include "PseudoAssembly.h"
#include <Assert.h>
#include <Shared/Diagnostics.h>
#include <TypeSystem/TypeBase.h>
#include <iterator>
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
#include "Nodes.h"

using ecpps::codegen::Instruction;
using ecpps::codegen::Routine;

#ifdef __clang__
[[clang::no_sanitize("address")]]
#endif
std::unordered_set<std::string> ecpps::codegen::g_functionImports{};

constexpr bool IsAligned(const std::size_t value, const std::size_t alignment)
{
     return (value & (alignment - 1)) == 0;
}

static void CopyRangeOperandIntoMemory(const ecpps::codegen::IntegerRangeOperand& range,
                                       const ecpps::codegen::MemoryLocationOperand& operand,
                                       std::vector<Instruction>& code)
{
     const auto AppendInteger =
         [&code]<std::size_t NBytes>(const std::shared_ptr<ecpps::abi::VirtualRegister>& relativeTo,
                                     std::size_t location, const std::size_t integer)
     {
          constexpr auto width = NBytes * ecpps::typeSystem::CharWidth;

          code.emplace_back(ecpps::codegen::MovInstruction{
              ecpps::codegen::IntegerOperand{integer, width},
              ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{relativeTo}, location, width},
              width});
     };

     auto& abi = ecpps::abi::ABI::Current();
     if (operand.Register().Index() == abi.StackPointerRegister())
     {
          const auto& reg = operand.Register().Index();
          auto offset = operand.Displacement();
          auto rangeIterator = range.Values().begin();
          while (!IsAligned(offset, 2) && std::distance(rangeIterator, range.Values().end()) >= 1)
          {
               const auto value =
                   abi.ConvertEndian<1, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
               AppendInteger.template operator()<1>(reg, offset, value);
               ++rangeIterator;
               offset++;
          }
          while (!IsAligned(offset, 4) && std::distance(rangeIterator, range.Values().end()) >= 2)
          {
               const auto value =
                   abi.ConvertEndian<2, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
               AppendInteger.template operator()<2>(reg, offset, value);
               ++rangeIterator;
               ++rangeIterator;
               offset += 2;
          }
          while (!IsAligned(offset, 8) && std::distance(rangeIterator, range.Values().end()) >= 4)
          {
               const auto value =
                   abi.ConvertEndian<4, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
               AppendInteger.template operator()<4>(reg, offset, value);
               ++rangeIterator;
               ++rangeIterator;
               ++rangeIterator;
               ++rangeIterator;
               offset += 4;
          }

          while (std::distance(rangeIterator, range.Values().end()) >= 8)
          {
               const auto value =
                   abi.ConvertEndian<8, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
               AppendInteger.template operator()<8>(reg, offset, value);
               ++rangeIterator;
               ++rangeIterator;
               ++rangeIterator;
               ++rangeIterator;
               ++rangeIterator;
               ++rangeIterator;
               ++rangeIterator;
               ++rangeIterator;
               offset += 8;
          }

          while (std::distance(rangeIterator, range.Values().end()) >= 4)
          {
               const auto value =
                   abi.ConvertEndian<4, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
               AppendInteger.template operator()<4>(reg, offset, value);
               ++rangeIterator;
               ++rangeIterator;
               ++rangeIterator;
               ++rangeIterator;
               offset += 4;
          }

          while (std::distance(rangeIterator, range.Values().end()) >= 2)
          {
               const auto value =
                   abi.ConvertEndian<2, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
               AppendInteger.template operator()<2>(reg, offset, value);
               ++rangeIterator;
               ++rangeIterator;
               offset += 2;
          }

          while (std::distance(rangeIterator, range.Values().end()) >= 1)
          {
               const auto value =
                   abi.ConvertEndian<1, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
               AppendInteger.template operator()<1>(reg, offset, value);
               ++rangeIterator;
               offset++;
          }

          return;
     }
}

static std::vector<unsigned char> SerialiseByteArray(const std::vector<std::uint32_t>& originalArray,
                                                     std::size_t elementSize)
{
     auto& abi = ecpps::abi::ABI::Current();

     std::vector<unsigned char> bytes{};
     bytes.reserve(originalArray.size() * elementSize);

     for (const auto value : originalArray)
     {

          switch (elementSize)
          {
          case 1:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 1>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
                   value));
               break;
          case 2:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 2>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
                   value));
               break;
          case 3:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 3>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
                   value));
               break;
          case 4:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 4>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
                   value));
               break;
          case 5:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 5>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
                   value));
               break;
          case 6:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 6>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
                   value));
               break;
          case 7:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 7>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
                   value));
               break;
          case 8:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 8>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
                   value));
               break;
          case 9:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 9>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                         // modernize-avoid-c-arrays)
                   value));
               break;
          case 10:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 10>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                          // modernize-avoid-c-arrays)
                   value));
               break;
          case 11:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 11>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                          // modernize-avoid-c-arrays)
                   value));
               break;
          case 12:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 12>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                          // modernize-avoid-c-arrays)
                   value));
               break;
          case 13:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 13>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                          // modernize-avoid-c-arrays)
                   value));
               break;
          case 14:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 14>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                          // modernize-avoid-c-arrays)
                   value));
               break;
          case 15:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 15>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                          // modernize-avoid-c-arrays)
                   value));
               break;
          case 16:
               bytes.append_range(abi.ConvertEndian<unsigned char[], 16>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                          // modernize-avoid-c-arrays)
                   value));
               break;
          default: throw TracedException("Invalid size");
          }
     }
     return bytes;
}

static std::vector<char8_t> SerialiseByteArrayChar(const std::vector<std::uint32_t>& originalArray,
                                                   std::size_t elementSize)
{
     auto& abi = ecpps::abi::ABI::Current();

     std::vector<char8_t> bytes{};
     bytes.reserve(originalArray.size() * elementSize);

     for (const auto value : originalArray)
     {

          switch (elementSize)
          {
          case 1:
               bytes.append_range(abi.ConvertEndian<char8_t[], 1>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                   // modernize-avoid-c-arrays)
                   value));
               break;
          case 2:
               bytes.append_range(abi.ConvertEndian<char8_t[], 2>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                   // modernize-avoid-c-arrays)
                   value));
               break;
          case 3:
               bytes.append_range(abi.ConvertEndian<char8_t[], 3>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                   // modernize-avoid-c-arrays)
                   value));
               break;
          case 4:
               bytes.append_range(abi.ConvertEndian<char8_t[], 4>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                   // modernize-avoid-c-arrays)
                   value));
               break;
          case 5:
               bytes.append_range(abi.ConvertEndian<char8_t[], 5>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                   // modernize-avoid-c-arrays)
                   value));
               break;
          case 6:
               bytes.append_range(abi.ConvertEndian<char8_t[], 6>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                   // modernize-avoid-c-arrays)
                   value));
               break;
          case 7:
               bytes.append_range(abi.ConvertEndian<char8_t[], 7>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                   // modernize-avoid-c-arrays)
                   value));
               break;
          case 8:
               bytes.append_range(abi.ConvertEndian<char8_t[], 8>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                   // modernize-avoid-c-arrays)
                   value));
               break;
          case 9:
               bytes.append_range(abi.ConvertEndian<char8_t[], 9>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                   // modernize-avoid-c-arrays)
                   value));
               break;
          case 10:
               bytes.append_range(abi.ConvertEndian<char8_t[], 10>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                    // modernize-avoid-c-arrays)
                   value));
               break;
          case 11:
               bytes.append_range(abi.ConvertEndian<char8_t[], 11>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                    // modernize-avoid-c-arrays)
                   value));
               break;
          case 12:
               bytes.append_range(abi.ConvertEndian<char8_t[], 12>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                    // modernize-avoid-c-arrays)
                   value));
               break;
          case 13:
               bytes.append_range(abi.ConvertEndian<char8_t[], 13>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                    // modernize-avoid-c-arrays)
                   value));
               break;
          case 14:
               bytes.append_range(abi.ConvertEndian<char8_t[], 14>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                    // modernize-avoid-c-arrays)
                   value));
               break;
          case 15:
               bytes.append_range(abi.ConvertEndian<char8_t[], 15>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                    // modernize-avoid-c-arrays)
                   value));
               break;
          case 16:
               bytes.append_range(abi.ConvertEndian<char8_t[], 16>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                    // modernize-avoid-c-arrays)
                   value));
               break;
          default: throw TracedException("Invalid size");
          }
     }
     return bytes;
}

static ecpps::codegen::Operand ParseExpression(ecpps::codegen::AssemblyContext& context, std::vector<Instruction>& code,
                                               const ecpps::Expression& expression)
{
     if (expression == nullptr) return std::monostate{};

     auto& symbolTable = context.symbolTables.top();

     const auto& value = expression->Value();
     if (auto* const integer = dynamic_cast<ecpps::ir::IntegralNode*>(value.get()); integer != nullptr)
     {
          const auto width = expression->Type()->Size() * ecpps::typeSystem::CharWidth;

          return ecpps::codegen::IntegerOperand{integer->Value(), width};
     }
     if (auto* const addition = dynamic_cast<ecpps::ir::AdditionNode*>(value.get()); addition != nullptr)
     {
          if (addition->Left() == nullptr || addition->Right() == nullptr) return ecpps::codegen::ErrorOperand{};

          const auto left = ParseExpression(context, code, addition->Left());

          const auto& type = expression->Type();
          std::size_t sizeInBytes = 0;
          if (ecpps::typeSystem::IsIntegral(type))
          {
               const auto* const integralType = dynamic_cast<const ecpps::typeSystem::IntegralType*>(type);
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

          std::vector<Instruction> codeBuffer{};
          const auto right = ParseExpression(context, codeBuffer, addition->Right());
          if (std::holds_alternative<ecpps::codegen::RegisterOperand>(right) &&
              *std::get<ecpps::codegen::RegisterOperand>(right).Index()->physical ==
                  *destinationStorage.Ptr()->physical)
               destinationStorage =
                   ecpps::abi::ABI::Current().AllocateRegister(ecpps::typeSystem::CharWidth * sizeInBytes);

          if (std::holds_alternative<ecpps::codegen::ErrorOperand>(left) ||
              std::holds_alternative<ecpps::codegen::ErrorOperand>(right))
               return ecpps::codegen::ErrorOperand{};

          if (!std::holds_alternative<ecpps::codegen::RegisterOperand>(left) ||
              *std::get<ecpps::codegen::RegisterOperand>(left).Index()->physical != *destinationStorage->physical)
               code.emplace_back(ecpps::codegen::MovInstruction{
                   left, ecpps::codegen::RegisterOperand{destinationStorage.Ptr()}, destinationStorage->width});

          code.append_range(codeBuffer);

          code.emplace_back(ecpps::codegen::AddInstruction{
              right, ecpps::codegen::RegisterOperand{destinationStorage.Ptr()}, destinationStorage->width});

          return ecpps::codegen::RegisterOperand{destinationStorage.Ptr()};
     }
     if (auto* const addition = dynamic_cast<ecpps::ir::AdditionAssignNode*>(value.get()); addition != nullptr)
     {
          if (addition->Left() == nullptr || addition->Right() == nullptr) return ecpps::codegen::ErrorOperand{};

          const auto left = ParseExpression(context, code, addition->Left());

          auto destinationStorage = left;

          std::vector<Instruction> codeBuffer{};
          const auto right = ParseExpression(context, codeBuffer, addition->Right());

          if (std::holds_alternative<ecpps::codegen::ErrorOperand>(left) ||
              std::holds_alternative<ecpps::codegen::ErrorOperand>(right))
               return ecpps::codegen::ErrorOperand{};

          code.append_range(codeBuffer);

          code.emplace_back(ecpps::codegen::AddInstruction{
              right, destinationStorage, addition->Right()->Type()->Size() * ecpps::typeSystem::CharWidth});

          return destinationStorage; // NOLINT(clang-diagnostic-nrvo)
     }
     if (auto* const subtraction = dynamic_cast<ecpps::ir::SubtractionNode*>(value.get()); subtraction != nullptr)
     {
          if (subtraction->Left() == nullptr || subtraction->Right() == nullptr) return ecpps::codegen::ErrorOperand{};

          const auto left = ParseExpression(context, code, subtraction->Left());

          const auto& type = expression->Type();
          std::size_t sizeInBytes = 0;
          if (ecpps::typeSystem::IsIntegral(type))
          {
               const auto* integralType = type->CastTo<ecpps::typeSystem::IntegralType>();
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

          const auto right = ParseExpression(context, code, subtraction->Right());

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

     if (auto* const multiplication = dynamic_cast<ecpps::ir::MultiplicationNode*>(value.get());
         multiplication != nullptr)
     {
          if (multiplication->Left() == nullptr || multiplication->Right() == nullptr)
               return ecpps::codegen::ErrorOperand{};

          const auto& type = expression->Type();
          std::size_t sizeInBytes = 0;
          bool isSigned = false;
          if (ecpps::typeSystem::IsIntegral(type))
          {
               const auto* integralType = type->CastTo<ecpps::typeSystem::IntegralType>();
               sizeInBytes = integralType->Size();
               isSigned = integralType->Sign() == ecpps::typeSystem::Signedness::Signed;
          }
          else
          {
               // TODO: Assert IsFloatingPoint & add float support
          }

          const auto left = ParseExpression(context, code, multiplication->Left());

          auto storage =
              std::holds_alternative<ecpps::codegen::RegisterOperand>(left)
                  ? ecpps::abi::ABI::Current().AllocateRegister(std::get<ecpps::codegen::RegisterOperand>(left).Index(),
                                                                ecpps::abi::RegisterAllocation::Priority)
                  : ecpps::abi::ABI::Current().AllocateRegister(ecpps::typeSystem::CharWidth * sizeInBytes);

          const auto right = ParseExpression(context, code, multiplication->Right());

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

     if (auto* const div = dynamic_cast<ecpps::ir::DivideNode*>(value.get()); div != nullptr)
     {
          if (div->Left() == nullptr || div->Right() == nullptr) return ecpps::codegen::ErrorOperand{};

          const auto& type = expression->Type();
          std::size_t sizeInBytes = 0;
          bool isSigned = false;
          if (ecpps::typeSystem::IsIntegral(type))
          {
               const auto* integralType = type->CastTo<ecpps::typeSystem::IntegralType>();
               sizeInBytes = integralType->Size();
               isSigned = integralType->Sign() == ecpps::typeSystem::Signedness::Signed;
          }
          else
          {
               // TODO: Assert IsFloatingPoint & add float support
          }

          const auto left = ParseExpression(context, code, div->Left());

          auto storage =
              std::holds_alternative<ecpps::codegen::RegisterOperand>(left)
                  ? ecpps::abi::ABI::Current().AllocateRegister(std::get<ecpps::codegen::RegisterOperand>(left).Index(),
                                                                ecpps::abi::RegisterAllocation::Priority)
                  : ecpps::abi::ABI::Current().AllocateRegister(ecpps::typeSystem::CharWidth * sizeInBytes);

          const auto right = ParseExpression(context, code, div->Right());

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
     if (auto* const call = dynamic_cast<ecpps::ir::FunctionCallNode*>(value.get()); call != nullptr)
     {
          const auto& function = *call->Function();

          auto& currentAbi = ecpps::abi::ABI::Current();
          const std::string functionName = ecpps::abi::ABI::MangleName(
              function.linkage, function.name, function.callingConvention, function.returnType,
              function.parameters |
                  std::views::transform([](const ecpps::ir::FunctionScope::Parameter& parameter)
                                        { return parameter.type; }) |
                  std::ranges::to<std::vector>());
          auto& callingConvention = currentAbi.CallingConventionFromName(function.callingConvention);

          runtime_assert(function.parameters.size() == call->Arguments().size(),
                         "Overload resolution failed to pick a function with equal amount of arguments");

          const auto callAbi = callingConvention.PrepareForCall(code);

          const auto returnTypeSize = callingConvention.GetRequirementsForType(function.returnType);
          const auto parameterSizes =
              function.parameters |
              std::views::transform([&callingConvention](const ecpps::ir::FunctionScope::Parameter& parameter)
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

               const auto operand = ParseExpression(context, code, argument);
               code.emplace_back(
                   ecpps::codegen::MovInstruction{operand,
                                                  ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{
                                                      std::get<ecpps::abi::AllocatedRegister>(storage.value).Ptr()}},
                                                  parameter.type->Size() * CHAR_BIT});
          }

          if (function.isDllImportExport) ecpps::codegen::g_functionImports.emplace(functionName);
          code.emplace_back(ecpps::codegen::CallInstruction{functionName});
          callAbi->Finish(code);

          const auto returnStorage = callingConvention.ReturnValueStorage(returnTypeSize);
          if (std::holds_alternative<std::monostate>(returnStorage.value))
               return ecpps::codegen::Operand{std::monostate{}};
          return ecpps::codegen::Operand{
              ecpps::codegen::RegisterOperand{std::get<ecpps::abi::AllocatedRegister>(returnStorage.value).Ptr()}};
     }
     if (auto* const load = dynamic_cast<ecpps::ir::LoadNode*>(value.get()); load != nullptr)
     {
          const auto it = symbolTable.find(load->Address());
          if (it == symbolTable.end()) return ecpps::codegen::ErrorOperand{};

          const auto& [location, storageRequest] = it->second;
          // TODO: Checks
          const auto& memoryLocation = std::get<ecpps::abi::MemoryLocation>(location.value);

          const auto width = storageRequest.size * ecpps::typeSystem::CharWidth;

          return ecpps::codegen::Operand{ecpps::codegen::MemoryLocationOperand{
              ecpps::codegen::RegisterOperand{memoryLocation.reg}, memoryLocation.offset, width}};
     }
     if (auto* const addressOf = dynamic_cast<ecpps::ir::AddressOfNode*>(value.get()); addressOf != nullptr)
     {
          auto& abi = ecpps::abi::ABI::Current();

          auto operand = ParseExpression(context, code, addressOf->Operand());

          if (std::holds_alternative<ecpps::codegen::RegisterOperand>(operand) &&
              std::get<ecpps::codegen::RegisterOperand>(operand).Size() == abi.PointerSize() * ecpps::abi::byteSize)
               return operand;

          runtime_assert(std::holds_alternative<ecpps::codegen::MemoryLocationOperand>(operand),
                         "Invalid operand for the address-of instruction");

          const auto& mem = std::get<ecpps::codegen::MemoryLocationOperand>(operand);

          const auto resultReg = abi.AllocateRegister(abi.PointerSize() * ecpps::typeSystem::CharWidth);

          code.emplace_back(ecpps::codegen::TakeAddressInstruction{
              ecpps::codegen::MemoryLocationOperand{mem.Register(), mem.Displacement(),
                                                    abi.PointerSize() * ecpps::typeSystem::CharWidth},
              ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{resultReg.Ptr()}}});

          return ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{resultReg.Ptr()}};
     }

     if (auto* const indirection = dynamic_cast<ecpps::ir::DereferenceNode*>(value.get()); indirection != nullptr)
     {
          auto& abi = ecpps::abi::ABI::Current();
          const auto* pointerType = indirection->Operand()->Type()->CastTo<ecpps::typeSystem::PointerType>();

          auto operandToPerformIndirectionOn = ParseExpression(context, code, indirection->Operand());

          if (std::holds_alternative<ecpps::codegen::MemoryLocationOperand>(operandToPerformIndirectionOn))
          {
               const auto tmpReg = abi.AllocateRegister(abi.PointerSize() * ecpps::typeSystem::CharWidth);
               const auto& mem = std::get<ecpps::codegen::MemoryLocationOperand>(operandToPerformIndirectionOn);

               code.emplace_back(ecpps::codegen::MovInstruction{
                   ecpps::codegen::MemoryLocationOperand{mem.Register(), mem.Displacement(),
                                                         abi.PointerSize() * ecpps::typeSystem::CharWidth},
                   ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{tmpReg.Ptr()}},
                   abi.PointerSize() * ecpps::typeSystem::CharWidth});
               return ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{tmpReg.Ptr()}, 0,
                                                            pointerType->BaseType()->Size() *
                                                                ecpps::typeSystem::CharWidth};
          }

          if (std::holds_alternative<ecpps::codegen::RegisterOperand>(operandToPerformIndirectionOn))
          {
               return ecpps::codegen::MemoryLocationOperand{
                   std::get<ecpps::codegen::RegisterOperand>(operandToPerformIndirectionOn), 0,
                   pointerType->BaseType()->Size() * ecpps::typeSystem::CharWidth};
          }

          throw TracedException("Invalid operand for the address-of instruction");
     }

     if (auto* const convert = dynamic_cast<ecpps::ir::ConvertNode*>(value.get()); convert != nullptr)
     {
          const auto inner = ParseExpression(context, code, convert->Operand());

          if (std::holds_alternative<ecpps::codegen::ErrorOperand>(inner)) return ecpps::codegen::ErrorOperand{};

          const auto& targetType = convert->TargetType();

          std::size_t width = targetType->Size() * ecpps::typeSystem::CharWidth;
          [[maybe_unused]] bool isSigned = false; // NOLINT
          if (IsIntegral(targetType))
          {
               isSigned = targetType->CastTo<ecpps::typeSystem::IntegralType>()->Sign() ==
                          ecpps::typeSystem::Signedness::Signed;
          }

          const auto storage = std::holds_alternative<ecpps::codegen::RegisterOperand>(inner)
                                   ? ecpps::abi::ABI::Current().AllocateRegister(
                                         std::get<ecpps::codegen::RegisterOperand>(inner).Index(),
                                         ecpps::abi::RegisterAllocation::Priority)
                                   : ecpps::abi::ABI::Current().AllocateRegister(width);

          if (!std::holds_alternative<ecpps::codegen::RegisterOperand>(inner))
          {
               ecpps::codegen::MovInstruction movInstruction{inner, ecpps::codegen::RegisterOperand{storage.Ptr()},
                                                             convert->Operand()->Type()->Size() *
                                                                 ecpps::typeSystem::CharWidth};
               movInstruction.isConversion = true;
               code.emplace_back(std::move(movInstruction));
          }

          return ecpps::codegen::RegisterOperand{storage.Ptr()};
     }
     if (auto* const postIncrement = dynamic_cast<ecpps::ir::PostIncrementNode*>(value.get()); postIncrement != nullptr)
     {
          if (postIncrement->Operand() == nullptr) return ecpps::codegen::ErrorOperand{};

          const auto left = ParseExpression(context, code, postIncrement->Operand());

          auto destinationStorage = ecpps::abi::ABI::Current().AllocateRegister(
              postIncrement->Operand()->Type()->Size() * ecpps::typeSystem::CharWidth);

          std::vector<Instruction> codeBuffer{};
          const auto right = ecpps::codegen::IntegerOperand{
              postIncrement->IncrementValue(), postIncrement->Operand()->Type()->Size() * ecpps::typeSystem::CharWidth};

          if (std::holds_alternative<ecpps::codegen::ErrorOperand>(left)) return ecpps::codegen::ErrorOperand{};

          code.append_range(codeBuffer);

          code.emplace_back(
              ecpps::codegen::MovInstruction{left, ecpps::codegen::RegisterOperand{destinationStorage.Ptr()},
                                             postIncrement->Operand()->Type()->Size() * ecpps::typeSystem::CharWidth});

          code.emplace_back(ecpps::codegen::AddInstruction{
              right, left, postIncrement->Operand()->Type()->Size() * ecpps::typeSystem::CharWidth});

          return ecpps::codegen::RegisterOperand{destinationStorage.Ptr()};
     }
     if (auto* const intArrayDecay = dynamic_cast<ecpps::ir::TemporaryIntegerArrayDecayNode*>(value.get());
         intArrayDecay != nullptr)
     {
          auto& abi = ecpps::abi::ABI::Current();

          const auto& type = intArrayDecay->Type();
          const auto elementSize = type->Size();
          auto bytes = SerialiseByteArrayChar(intArrayDecay->Values(), elementSize);

          const auto stringIndex = context.AddString(bytes);

          const auto movWidth = abi.PointerSize() * ecpps::typeSystem::CharWidth;
          auto destinationStorage = ecpps::abi::ABI::Current().AllocateRegister(movWidth);

          const auto instructionIndex = code.size();

          code.emplace_back(ecpps::codegen::TakeAddressInstruction{
              ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{abi.StringRegister()}, 0, movWidth},
              ecpps::codegen::RegisterOperand{destinationStorage.Ptr()}});
          context.AddStringPatch(instructionIndex, ecpps::InstructionPatchType::LeaFrom, stringIndex);

          return ecpps::codegen::RegisterOperand{destinationStorage.Ptr()};
     }
     if (auto* const integerArray = dynamic_cast<ecpps::ir::IntegerArrayNode*>(value.get()); integerArray != nullptr)
     {
          const auto& type = integerArray->Type();
          const auto elementSize = type->Size();

          auto bytes = SerialiseByteArray(integerArray->Values(), elementSize);

          return ecpps::codegen::IntegerRangeOperand{std::move(bytes)};
     }
     if (auto* const arrayDecay = dynamic_cast<ecpps::ir::LoadArrayDecayNode*>(value.get()); arrayDecay != nullptr)
     {
          auto& abi = ecpps::abi::ABI::Current();

          const auto& operand = arrayDecay->GetOperand();
          const auto loaded = ParseExpression(context, code, operand);

          const auto movWidth = abi.PointerSize() * ecpps::typeSystem::CharWidth;

          auto destinationStorage = ecpps::abi::ABI::Current().AllocateRegister(movWidth);

          runtime_assert(std::holds_alternative<ecpps::codegen::MemoryLocationOperand>(loaded),
                         "Invalid operand for lea");

          code.emplace_back(
              ecpps::codegen::TakeAddressInstruction{std::get<ecpps::codegen::MemoryLocationOperand>(loaded),
                                                     ecpps::codegen::RegisterOperand{destinationStorage.Ptr()}});

          return ecpps::codegen::RegisterOperand{destinationStorage.Ptr()};
     }

     throw ecpps::TracedException(std::logic_error("Invalid expression"));
}

static void CompileReturn(ecpps::codegen::AssemblyContext& context, std::vector<Instruction>& code,
                          const ecpps::ir::ReturnNode& node)
{
     if (node.HasValue())
     {
          // TODO: Mapping function
          ecpps::abi::MicrosoftX64CallingConvention callingConvention{};
          const auto returnStorage =
              callingConvention.ReturnValueStorage(callingConvention.GetRequirementsForType(node.Value()->Type()));
          code.emplace_back(
              ecpps::codegen::MovInstruction{ParseExpression(context, code, node.Value()),
                                             ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{
                                                 std::get<ecpps::abi::AllocatedRegister>(returnStorage.value).Ptr()}},
                                             node.Value()->Type()->Size() * ecpps::typeSystem::CharWidth});
     }

     code.emplace_back(ecpps::codegen::ReturnInstruction{});
}

static Routine CompileRoutine(ecpps::codegen::AssemblyContext& context, const ecpps::ir::ProcedureNode& node)
{
     std::vector<Instruction> instructions{};
     auto& currentAbi = ecpps::abi::ABI::Current();
     const auto& parentCallingConvention = currentAbi.CallingConventionFromName(node.CallingConvention());

     auto stackManager = parentCallingConvention.BeginStack(instructions);

     std::unordered_map<std::string, std::pair<ecpps::abi::StorageRef, ecpps::abi::StorageRequirement>>& symbolTable =
         context.symbolTables.emplace();

     symbolTable.reserve(node.Locals().size());
     for (const auto& decl : node.Locals())
     {
          // TODO: static & extern
          const auto& type = decl.type;
          ecpps::abi::StorageRequirement request{type->Size(), type->Alignment(),
                                                 IsIntegral(type) ? ecpps::abi::RequiredStorageKind::Integer
                                                 : IsFloatingPoint(type)
                                                     ? ecpps::abi::RequiredStorageKind::FloatingPoint
                                                 : IsPointer(type) ? ecpps::abi::RequiredStorageKind::Pointer
                                                                   : ecpps::abi::RequiredStorageKind::Aggregate};

          auto storage = stackManager->ReserveStorage(request);
          symbolTable.emplace(decl.name, std::pair<ecpps::abi::StorageRef, ecpps::abi::StorageRequirement>{
                                             std::move(storage), request});
     }

     for (const auto& line : node.Body())
     {
          if (auto* const returnNode = dynamic_cast<ecpps::ir::ReturnNode*>(line.get()); returnNode != nullptr)
               CompileReturn(context, instructions, *returnNode);
          else if (auto* const call = dynamic_cast<ecpps::ir::FunctionCallNode*>(line.get()); call != nullptr)
          {
               const auto& function = *call->Function();

               const std::string functionName = ecpps::abi::ABI::MangleName(
                   function.linkage, function.name, function.callingConvention, function.returnType,
                   function.parameters |
                       std::views::transform([](const ecpps::ir::FunctionScope::Parameter& parameter)
                                             { return parameter.type; }) |
                       std::ranges::to<std::vector>());
               const auto& callingConvention = currentAbi.CallingConventionFromName(function.callingConvention);
               const auto returnTypeSize = callingConvention.GetRequirementsForType(function.returnType);
               const auto parameterSizes =
                   function.parameters |
                   std::views::transform([&callingConvention](const ecpps::ir::FunctionScope::Parameter& parameter)
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

                    const auto operand = ParseExpression(context, instructions, argument);
                    instructions.emplace_back(ecpps::codegen::MovInstruction{
                        operand,
                        ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{
                            std::get<ecpps::abi::AllocatedRegister>(storage.value).Ptr()}},
                        parameter.type->Size() * CHAR_BIT});
               }

               if (function.isDllImportExport) ecpps::codegen::g_functionImports.emplace(functionName);
               instructions.emplace_back(ecpps::codegen::CallInstruction{functionName});
          }
          else if (const auto* const store = dynamic_cast<const ecpps::ir::StoreNode*>(line.get()); store != nullptr)
          {
               runtime_assert(symbolTable.contains(store->Address()), "Invalid symbol");

               const auto& [location, storageRequest] = symbolTable.at(store->Address());
               const auto& memoryLocation = std::get<ecpps::abi::MemoryLocation>(location.value);

               const auto initValue = ParseExpression(context, instructions, store->Value());

               const ecpps::codegen::Operand dest{
                   ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{memoryLocation.reg},
                                                         memoryLocation.offset, storageRequest.size * CHAR_BIT}};

               if (std::holds_alternative<ecpps::codegen::IntegerRangeOperand>(initValue))
               {
                    CopyRangeOperandIntoMemory(std::get<ecpps::codegen::IntegerRangeOperand>(initValue),
                                               std::get<ecpps::codegen::MemoryLocationOperand>(dest), instructions);
               }
               else
               {
                    const bool srcIsMem = std::holds_alternative<ecpps::codegen::MemoryLocationOperand>(initValue);
                    const bool dstIsMem = true;

                    if (srcIsMem && dstIsMem)
                    {
                         const auto tempReg = currentAbi.AllocateRegister(storageRequest.size * CHAR_BIT);

                         instructions.emplace_back(ecpps::codegen::MovInstruction{
                             initValue, ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{tempReg.Ptr()}},
                             storageRequest.size * CHAR_BIT});

                         instructions.emplace_back(ecpps::codegen::MovInstruction{
                             ecpps::codegen::Operand{ecpps::codegen::RegisterOperand{tempReg.Ptr()}}, dest,
                             storageRequest.size * CHAR_BIT});
                    }
                    else
                    {
                         instructions.emplace_back(
                             ecpps::codegen::MovInstruction{initValue, dest, storageRequest.size * CHAR_BIT});
                    }
               }
          }
          else if (auto* const addition = dynamic_cast<ecpps::ir::AdditionAssignNode*>(line.get()); addition != nullptr)
          {
               if (addition->Left() == nullptr || addition->Right() == nullptr) continue;

               const auto left = ParseExpression(context, instructions, addition->Left());

               const auto& destinationStorage = left;

               std::vector<Instruction> codeBuffer{};
               const auto right = ParseExpression(context, codeBuffer, addition->Right());

               if (std::holds_alternative<ecpps::codegen::ErrorOperand>(left) ||
                   std::holds_alternative<ecpps::codegen::ErrorOperand>(right))
                    continue;

               instructions.append_range(codeBuffer);

               instructions.emplace_back(ecpps::codegen::AddInstruction{
                   right, destinationStorage, addition->Right()->Type()->Size() * ecpps::typeSystem::CharWidth});
          }
     }

     stackManager->Finish();
     context.symbolTables.pop();

     return Routine::Branchless(
         std::move(instructions),
         ecpps::abi::ABI::MangleName(node.Linkage(), node.Name(), node.CallingConvention(), node.ReturnType(),
                                     node.ParameterList() |
                                         std::views::transform([](const ecpps::ir::FunctionScope::Parameter& parameter)
                                                               { return parameter.type; }) |
                                         std::ranges::to<std::vector>()));
}

void ecpps::codegen::Compile(CompilerConfig& config, SourceFile& source,
                             const std::vector<ecpps::ir::NodePointer>& intermediateRepresentation)
{
     AssemblyContext context{config};
     auto& patches = context.Patches();
     for (const auto& node : intermediateRepresentation)
     {
          patches = {};

          if (auto* const procedureNode = dynamic_cast<ecpps::ir::ProcedureNode*>(node.get()); procedureNode != nullptr)
               source.compiledRoutines.push_back(CompileRoutine(context, *procedureNode));

          source.stringTranslation = patches;
     }
     config.stringArray = context.GetStringSection();
}
