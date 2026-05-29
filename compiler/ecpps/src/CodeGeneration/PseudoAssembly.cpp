#include "PseudoAssembly.h"
#include <RuntimeAssert.h>
#include <Shared/Diagnostics.h>
#include <TypeSystem/TypeBase.h>
#include <climits>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <variant>
#include "../Execution/ControlFlow.h"
#include "../Execution/Expressions.h"
#include "../Execution/HighLevel.h"
#include "../Execution/Operations.h"
#include "../Execution/Procedural.h"
#include "../Machine/ABI.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "Execution/Context.h"
#include "Execution/NodeBase.h"
#include "Machine/Storage.h"
#include "Nodes.h"
#include "Shared/Error.h"

using ecpps::codegen::Instruction;
using ecpps::codegen::Routine;

#ifdef __clang__
[[clang::no_sanitize("address")]]
#endif
std::unordered_map<std::string, std::string> ecpps::codegen::g_functionImports{};

constexpr bool IsAligned(const std::size_t value, const std::size_t alignment)
{
     return (value & (alignment - 1)) == 0;
}

// static void CopyRangeOperandIntoMemory(const ecpps::codegen::IntegerRangeOperand& range,
//                                        const ecpps::codegen::MemoryLocationOperand& operand,
//                                        std::vector<Instruction>& code)
// {
//      const auto AppendInteger =
//          [&code]<std::size_t NBytes>(const std::shared_ptr<ecpps::abi::VirtualRegister>& relativeTo,
//                                      std::size_t location, const std::size_t integer)
//      {
//           constexpr auto width = NBytes * ecpps::typeSystem::CharWidth;

//           code.emplace_back(ecpps::codegen::MovInstruction{
//               ecpps::codegen::IntegerOperand{integer, width},
//               ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{relativeTo}, location, width},
//               width});
//      };

//      auto& abi = ecpps::abi::ABI::Current();
//      if (operand.Register().Index() == abi.StackPointerRegister())
//      {
//           const auto& reg = operand.Register().Index();
//           auto offset = operand.Displacement();
//           auto rangeIterator = range.Values().begin();
//           while (!IsAligned(offset, 2) && std::distance(rangeIterator, range.Values().end()) >= 1)
//           {
//                const auto value =
//                    abi.ConvertEndian<1, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                AppendInteger.template operator()<1>(reg, offset, value);
//                ++rangeIterator;
//                offset++;
//           }
//           while (!IsAligned(offset, 4) && std::distance(rangeIterator, range.Values().end()) >= 2)
//           {
//                const auto value =
//                    abi.ConvertEndian<2, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                AppendInteger.template operator()<2>(reg, offset, value);
//                rangeIterator += 2;
//                offset += 2;
//           }
//           while (!IsAligned(offset, 8) && std::distance(rangeIterator, range.Values().end()) >= 4)
//           {
//                const auto value =
//                    abi.ConvertEndian<4, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                AppendInteger.template operator()<4>(reg, offset, value);
//                rangeIterator += 4;
//                offset += 4;
//           }

//           while (std::distance(rangeIterator, range.Values().end()) >= 8)
//           {
//                const auto value =
//                    abi.ConvertEndian<8, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                AppendInteger.template operator()<8>(reg, offset, value);
//                rangeIterator += 8;
//                offset += 8;
//           }

//           while (std::distance(rangeIterator, range.Values().end()) >= 4)
//           {
//                const auto value =
//                    abi.ConvertEndian<4, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                AppendInteger.template operator()<4>(reg, offset, value);
//                rangeIterator += 4;
//                offset += 4;
//           }

//           while (std::distance(rangeIterator, range.Values().end()) >= 2)
//           {
//                const auto value =
//                    abi.ConvertEndian<2, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                AppendInteger.template operator()<2>(reg, offset, value);
//                rangeIterator += 2;
//                offset += 2;
//           }

//           while (std::distance(rangeIterator, range.Values().end()) >= 1)
//           {
//                const auto value =
//                    abi.ConvertEndian<1, unsigned char[]>(rangeIterator); // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                AppendInteger.template operator()<1>(reg, offset, value);
//                ++rangeIterator;
//                offset++;
//           }

//           return;
//      }
// }

// static std::vector<unsigned char> SerialiseByteArray(const std::vector<std::uint32_t>& originalArray,
//                                                      std::size_t elementSize)
// {
//      auto& abi = ecpps::abi::ABI::Current();

//      std::vector<unsigned char> bytes{};
//      bytes.reserve(originalArray.size() * elementSize);

//      for (const auto value : originalArray)
//      {

//           switch (elementSize)
//           {
//           case 1:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 1>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 2:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 2>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 3:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 3>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 4:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 4>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 5:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 5>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 6:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 6>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 7:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 7>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 8:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 8>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 9:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 9>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                          // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 10:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 10>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                           // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 11:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 11>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                           // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 12:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 12>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                           // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 13:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 13>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                           // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 14:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 14>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                           // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 15:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 15>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                           // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 16:
//                bytes.append_range(abi.ConvertEndian<unsigned char[], 16>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                           // modernize-avoid-c-arrays)
//                    value));
//                break;
//           default: throw TracedException("Invalid size");
//           }
//      }
//      return bytes;
// }

// static std::vector<char8_t> SerialiseByteArrayChar(const std::vector<std::uint32_t>& originalArray,
//                                                    std::size_t elementSize)
// {
//      auto& abi = ecpps::abi::ABI::Current();

//      std::vector<char8_t> bytes{};
//      bytes.reserve(originalArray.size() * elementSize);

//      for (const auto value : originalArray)
//      {

//           switch (elementSize)
//           {
//           case 1:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 1>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                    // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 2:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 2>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                    // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 3:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 3>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                    // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 4:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 4>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                    // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 5:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 5>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                    // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 6:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 6>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                    // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 7:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 7>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                    // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 8:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 8>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                    // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 9:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 9>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                    // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 10:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 10>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                     // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 11:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 11>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                     // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 12:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 12>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                     // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 13:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 13>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                     // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 14:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 14>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                     // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 15:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 15>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                     // modernize-avoid-c-arrays)
//                    value));
//                break;
//           case 16:
//                bytes.append_range(abi.ConvertEndian<char8_t[], 16>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
//                                                                     // modernize-avoid-c-arrays)
//                    value));
//                break;
//           default: throw TracedException("Invalid size");
//           }
//      }
//      return bytes;
// }

// static void ScanNodeForFunctionCalls(const ecpps::ir::NodeBase* node,
//                                      std::vector<const ecpps::ir::FunctionCallNode*>& foundCalls)
// {
//      if (node == nullptr) return;

//      if (const auto* call = dynamic_cast<const ecpps::ir::FunctionCallNode*>(node); call != nullptr)
//      {
//           foundCalls.push_back(call);

//           for (const auto& arg : call->Arguments())
//           {
//                if (arg != nullptr) ScanNodeForFunctionCalls(arg->Value().get(), foundCalls);
//           }
//      }
//      else if (const auto* returnNode = dynamic_cast<const ecpps::ir::ReturnNode*>(node); returnNode != nullptr)
//      {
//           if (returnNode->HasValue()) ScanNodeForFunctionCalls(returnNode->Value()->Value().get(), foundCalls);
//      }
//      // else if (const auto* store = dynamic_cast<const ecpps::ir::high::StoreNode*>(node); store != nullptr)
//      // {
//      //      ScanNodeForFunctionCalls(store->Value()->Value().get(), foundCalls);
//      // }
//      else if (const auto* addition = dynamic_cast<const ecpps::ir::AdditionNode*>(node); addition != nullptr)
//      {
//           ScanNodeForFunctionCalls(addition->Left()->Value().get(), foundCalls);
//           ScanNodeForFunctionCalls(addition->Right()->Value().get(), foundCalls);
//      }
//      else if (const auto* subtraction = dynamic_cast<const ecpps::ir::SubtractionNode*>(node); subtraction !=
//      nullptr)
//      {
//           ScanNodeForFunctionCalls(subtraction->Left()->Value().get(), foundCalls);
//           ScanNodeForFunctionCalls(subtraction->Right()->Value().get(), foundCalls);
//      }
//      else if (const auto* multiplication = dynamic_cast<const ecpps::ir::MultiplicationNode*>(node);
//               multiplication != nullptr)
//      {
//           ScanNodeForFunctionCalls(multiplication->Left()->Value().get(), foundCalls);
//           ScanNodeForFunctionCalls(multiplication->Right()->Value().get(), foundCalls);
//      }
//      else if (const auto* division = dynamic_cast<const ecpps::ir::DivideNode*>(node); division != nullptr)
//      {
//           ScanNodeForFunctionCalls(division->Left()->Value().get(), foundCalls);
//           ScanNodeForFunctionCalls(division->Right()->Value().get(), foundCalls);
//      }
//      else if (const auto* addressOf = dynamic_cast<const ecpps::ir::AddressOfNode*>(node); addressOf != nullptr)
//      {
//           ScanNodeForFunctionCalls(addressOf->Operand()->Value().get(), foundCalls);
//      }
//      else if (const auto* dereference = dynamic_cast<const ecpps::ir::DereferenceNode*>(node); dereference !=
//      nullptr)
//      {
//           ScanNodeForFunctionCalls(dereference->Operand()->Value().get(), foundCalls);
//      }
//      else if (const auto* convert = dynamic_cast<const ecpps::ir::ConvertNode*>(node); convert != nullptr)
//      {
//           ScanNodeForFunctionCalls(convert->Operand()->Value().get(), foundCalls);
//      }
//      else if (const auto* additionAssign = dynamic_cast<const ecpps::ir::AdditionAssignNode*>(node);
//               additionAssign != nullptr)
//      {
//           if (additionAssign->Left() != nullptr)
//                ScanNodeForFunctionCalls(additionAssign->Left()->Value().get(), foundCalls);
//           if (additionAssign->Right() != nullptr)
//                ScanNodeForFunctionCalls(additionAssign->Right()->Value().get(), foundCalls);
//      }
//      else if (const auto* subtractionAssign = dynamic_cast<const ecpps::ir::SubtractionAssignNode*>(node);
//               subtractionAssign != nullptr)
//      {
//           if (subtractionAssign->Left() != nullptr)
//                ScanNodeForFunctionCalls(subtractionAssign->Left()->Value().get(), foundCalls);
//           if (subtractionAssign->Right() != nullptr)
//                ScanNodeForFunctionCalls(subtractionAssign->Right()->Value().get(), foundCalls);
//      }
//      else if (const auto* postIncrement = dynamic_cast<const ecpps::ir::PostIncrementNode*>(node);
//               postIncrement != nullptr)
//      {
//           if (postIncrement->Operand() != nullptr)
//                ScanNodeForFunctionCalls(postIncrement->Operand()->Value().get(), foundCalls);
//      }
//      else if (const auto* postDecrement = dynamic_cast<const ecpps::ir::PostDecrementNode*>(node);
//               postDecrement != nullptr)
//      {
//           if (postDecrement->Operand() != nullptr)
//                ScanNodeForFunctionCalls(postDecrement->Operand()->Value().get(), foundCalls);
//      }
//      else if (const auto* arrayDecay = dynamic_cast<const ecpps::ir::LoadArrayDecayNode*>(node); arrayDecay !=
//      nullptr)
//      {
//           ScanNodeForFunctionCalls(arrayDecay->GetOperand()->Value().get(), foundCalls);
//      }
//      else if (const auto* parameterNode = dynamic_cast<const ecpps::ir::ParameterNode*>(node); parameterNode !=
//      nullptr)
//      {
//           // No need to scan parameters
//      }
// }

std::uint32_t ecpps::codegen::AssemblyContext::ReserveNextStringEntry(void) noexcept
{
     static std::atomic<std::uint32_t> next = 0;
     return next.fetch_add(1, std::memory_order::relaxed);
}

void ecpps::codegen::ParsingContext::ParseNode(const Expression& expression)
{
     if (expression == nullptr) return;

     const ecpps::ir::NodePointer& node = expression->Value();
     switch (node->Kind())
     {
     case ecpps::ir::NodeKind::Allocate:
     {
          const auto* allocationNode = dynamic_cast<const ecpps::ir::AllocationNode*>(node.get());
          runtime_assert(allocationNode != nullptr, "Allocate node was not an allocation!");
          this->ParseAllocateNode(expression, *allocationNode);
     }
     break;
     case ecpps::ir::NodeKind::Return:
     {
          const auto* returnNode = dynamic_cast<const ecpps::ir::SSAReturnNode*>(node.get());
          runtime_assert(returnNode != nullptr, "Return node was not a return!");
          this->ParseReturnNode(expression, *returnNode);
     }
     break;
     case ecpps::ir::NodeKind::Store:
     {
          if (const auto* storeIntNode = dynamic_cast<const ecpps::ir::SSAStoreIntegerNode*>(node.get());
              storeIntNode != nullptr)
          {
               this->ParseStoreIntNode(expression, *storeIntNode);
               return;
          }
          const auto* storeNode = dynamic_cast<const ecpps::ir::SSAStoreNode*>(node.get());
          runtime_assert(storeNode != nullptr, "Store node was not a store!");
          this->ParseStoreNode(expression, *storeNode);
     }
     break;
     case ecpps::ir::NodeKind::Addition:
     {
          const auto* addNode = dynamic_cast<const ecpps::ir::SSAAddNode*>(node.get());
          runtime_assert(addNode != nullptr, "Addition node was not an addition!");
          this->ParseAddNode(expression, *addNode);
     }
     break;
     default:
          this->diagnostics.push_back(std::make_unique<diagnostics::TypeError>("Not implemented", node->Source()));
          break;
     }
}
void ecpps::codegen::ParsingContext::ParseReturnNode([[maybe_unused]] const Expression& expression,
                                                     const ir::SSAReturnNode& node)
{
     if (!node.HasOperand())
     {
          this->instructions->emplace_back(ReturnInstruction{});
          return;
     }

     // TODO: Implement storage retrieval & emplacing the result into it

     this->instructions->emplace_back(ReturnInstruction{});
}
void ecpps::codegen::ParsingContext::ParseStoreNode([[maybe_unused]] const Expression& expression,
                                                    const ir::SSAStoreNode& node)
{
     const auto source = this->GetVirtualFromUnbounded(node.Src());
     const auto destination = this->GetVirtualFromUnbounded(node.Target());
     this->instructions->emplace_back(MovInstruction{source, destination, source.Size()});
}
void ecpps::codegen::ParsingContext::ParseStoreIntNode([[maybe_unused]] const Expression& expression,
                                                       const ir::SSAStoreIntegerNode& node)
{
     const auto source = node.Src();
     const auto destination = this->GetVirtualFromUnbounded(node.Target());
     this->instructions->emplace_back(
         MovInstruction{IntegerOperand{source, destination.Size()}, destination, destination.Size()});
}
void ecpps::codegen::ParsingContext::ParseAddNode([[maybe_unused]] const Expression& expression,
                                                  [[maybe_unused]] const ir::SSAAddNode& node)
{
}

void ecpps::codegen::ParsingContext::ParseAllocateNode([[maybe_unused]] const Expression& expression,
                                                       const ir::AllocationNode& node)
{
     this->RegisterUnboundedToVirtual(node.Node(), node.Size() * typeSystem::CharWidth);
}

static Routine CompileRoutine(ecpps::codegen::AssemblyContext& context, const ecpps::ir::ProcedureNode& node)
{
     std::vector<Instruction> instructions{};
     auto& currentAbi = ecpps::abi::ABI::Current();
     const auto& parentCallingConvention = currentAbi.CallingConventionFromName(node.CallingConvention());

     auto stackManager = parentCallingConvention.BeginStack(instructions);

     std::vector<const ecpps::ir::FunctionCallNode*> allFunctionCalls;
     // for (const auto& line : node.Body()) { ScanNodeForFunctionCalls(line.get(), allFunctionCalls); }

     std::size_t maxArgumentStackSpace = 0;
     for (const auto* call : allFunctionCalls)
     {
          const auto& function = *call->Function();
          const auto& callingConvention = currentAbi.CallingConventionFromName(function.callingConvention);
          const auto returnTypeSize = callingConvention.GetRequirementsForType(function.returnType);
          const auto parameterSizes =
              function.parameters |
              std::views::transform([&callingConvention](const ecpps::ir::FunctionScope::Parameter& parameter)
                                    { return callingConvention.GetRequirementsForType(parameter.type); }) |
              std::ranges::to<std::vector>();

          const std::size_t callStackSpace =
              callingConvention.CalculateArgumentStackSpace(returnTypeSize, parameterSizes);
          maxArgumentStackSpace = std::max(maxArgumentStackSpace, callStackSpace);
     }
     stackManager->ReserveCallArgumentSpace(maxArgumentStackSpace);

     std::unordered_map<std::string, std::pair<ecpps::abi::StorageRef, ecpps::abi::StorageRequirement>>& symbolTable =
         context.symbolTables.emplace();

     symbolTable.reserve(node.Locals().size());
     for (const auto& decl : node.Locals())
     {
          // TODO: static & extern
          if (!std::holds_alternative<ecpps::ir::Variable>(decl.local)) continue;
          const auto& variableDecl = std::get<ecpps::ir::Variable>(decl.local);

          const auto& type = variableDecl.type;
          ecpps::abi::StorageRequirement request{type->Size(), type->Alignment(),
                                                 IsIntegral(type) ? ecpps::abi::RequiredStorageKind::Integer
                                                 : IsFloatingPoint(type)
                                                     ? ecpps::abi::RequiredStorageKind::FloatingPoint
                                                 : IsPointer(type) ? ecpps::abi::RequiredStorageKind::Pointer
                                                                   : ecpps::abi::RequiredStorageKind::Aggregate};

          auto storage = stackManager->ReserveStorage(request);
          symbolTable.emplace(
              variableDecl.Name().value_or("__unknown_local_variable"),
              std::pair<ecpps::abi::StorageRef, ecpps::abi::StorageRequirement>{std::move(storage), request});
     }

     context.stackFrameAdjustment = stackManager->GetParameterAdjustment();

     context.functionParameters = parentCallingConvention.LocateParameters(
         ecpps::abi::StorageRequirement{node.ReturnType()->Size(), node.ReturnType()->Alignment(),
                                        ecpps::abi::RequiredStorageKind::Integer},
         node.ParameterList() |
             std::views::transform(
                 [](const ecpps::ir::FunctionScope::Parameter& parameter)
                 {
                      return ecpps::abi::StorageRequirement{parameter.type->Size(), parameter.type->Alignment(),
                                                            ecpps::abi::RequiredStorageKind::Integer};
                 }) |
             std::ranges::to<std::vector>());

     ecpps::codegen::ParsingContext parseContext(instructions, currentAbi);
     // for (const auto& line : node.Body()) parseContext.ParseNode(nullptr);

     stackManager->Finish();
     context.symbolTables.pop();

     return Routine::Branchless(
         std::move(instructions),
         ecpps::abi::ABI::MangleName(node.Linkage(), node.Name(), node.CallingConvention(), node.ReturnType(),
                                     node.ParameterList() |
                                         std::views::transform([](const ecpps::ir::FunctionScope::Parameter& parameter)
                                                               { return parameter.type; }) |
                                         std::ranges::to<std::vector>(),
                                     node.NamespacePath()));
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
