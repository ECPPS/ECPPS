#include "PseudoAssembly.h"
#include <RuntimeAssert.h>
#include <Shared/Diagnostics.h>
#include <TypeSystem/TypeBase.h>
#include <atomic>
#include <format>
#include <ranges>
#include <utility>
#include <variant>
#include "../Execution/IR.h"
#include "../Execution/Operations.h"
#include "../Execution/Procedural.h"
#include "../Machine/ABI.h"
#include "AbstractNodes.h"
#include "Execution/NodeBase.h"
#include "Nodes.h"
#include "Shared/Error.h"

using ecpps::codegen::Routine;

#ifdef __clang__
[[clang::no_sanitize("address")]]
#endif
std::unordered_map<std::string, std::string> ecpps::codegen::g_functionImports{};

constexpr bool IsAligned(const std::size_t value, const std::size_t alignment)
{
     return (value & (alignment - 1)) == 0;
}

static ecpps::ir::abstract::ConditionCode ToConditionCode(const ecpps::ir::ComparisonPredicate predicate)
{
     using enum ecpps::ir::ComparisonPredicate;
     using ecpps::ir::abstract::ConditionCode;
     switch (predicate)
     {
     case Equal: return ConditionCode::Equal;
     case NotEqual: return ConditionCode::NotEqual;
     case SignedLess: return ConditionCode::Less;
     case SignedLessEqual: return ConditionCode::LessEqual;
     case SignedGreater: return ConditionCode::Greater;
     case SignedGreaterEqual: return ConditionCode::GreaterEqual;
     case UnsignedLess: return ConditionCode::Below;
     case UnsignedLessEqual: return ConditionCode::BelowEqual;
     case UnsignedGreater: return ConditionCode::Above;
     case UnsignedGreaterEqual: return ConditionCode::AboveEqual;
     }
     throw TracedException("Unmapped comparison predicate");
}

std::uint32_t ecpps::codegen::AssemblyContext::ReserveNextStringEntry(void) noexcept
{
     static std::atomic<std::uint32_t> next = 0;
     return next.fetch_add(1, std::memory_order::relaxed);
}

std::size_t ecpps::codegen::ParsingContext::CallFunctionIndex(const ir::FunctionScope* contextPointer)
{
     std::size_t foundIndex{};
     for (const auto* pointer : this->functionUsageTable)
     {
          if (pointer == contextPointer) return foundIndex;
          foundIndex++;
     }
     this->functionUsageTable.push_back(contextPointer);
     return foundIndex;
}

void ecpps::codegen::ParsingContext::ParseNode(const ir::NodeBase* node)
{
     if (node == nullptr) return;
     if (this->pendingCompare.has_value() && node->Kind() != ecpps::ir::NodeKind::Branch)
     {
          this->diagnostics.push_back(std::make_unique<diagnostics::TypeError>(
               "Materialising a comparison result outside of a branch is not supported yet", node->Source()));
          this->pendingCompare.reset();
     }

     try
     {
          switch (node->Kind())
          {
          case ecpps::ir::NodeKind::Allocate:
          {
               const auto* allocationNode = dynamic_cast<const ecpps::ir::AllocationNode*>(node);
               runtime_assert(allocationNode != nullptr, "Allocate node was not an allocation!");
               this->ParseAllocateNode(*allocationNode);
          }
          break;
          case ecpps::ir::NodeKind::Return:
          {
               const auto* returnNode = dynamic_cast<const ecpps::ir::SSAReturnNode*>(node);
               runtime_assert(returnNode != nullptr, "Return node was not a return!");
               this->ParseReturnNode(*returnNode);
          }
          break;
          case ecpps::ir::NodeKind::Store:
          {
               if (const auto* storeIntNode = dynamic_cast<const ecpps::ir::SSAStoreIntegerNode*>(node);
                   storeIntNode != nullptr)
               {
                    this->ParseStoreIntNode(*storeIntNode);
                    return;
               }
               const auto* storeNode = dynamic_cast<const ecpps::ir::SSAStoreNode*>(node);
               runtime_assert(storeNode != nullptr, "Store node was not a store!");
               this->ParseStoreNode(*storeNode);
          }
          break;
          case ecpps::ir::NodeKind::Integer:
          {
               const auto* intNode = dynamic_cast<const ecpps::ir::SSAImmNode*>(node);
               runtime_assert(intNode != nullptr, "Integer node was not an immediate!");
               this->ParseIntNode(*intNode);
          }
          break;
          case ecpps::ir::NodeKind::Addition:
          {
               const auto* addNode = dynamic_cast<const ecpps::ir::SSAAddNode*>(node);
               runtime_assert(addNode != nullptr, "Addition node was not an addition!");
               this->ParseAddNode(*addNode);
          }
          break;
          case ecpps::ir::NodeKind::Subtraction:
          {
               const auto* subNode = dynamic_cast<const ecpps::ir::SSASubNode*>(node);
               runtime_assert(subNode != nullptr, "Subtraction node was not a subtraction!");
               this->ParseSubNode(*subNode);
          }
          break;
          case ecpps::ir::NodeKind::LeftBitShift:
          {
               const auto* leftShiftNode = dynamic_cast<const ecpps::ir::SSALeftShiftNode*>(node);
               runtime_assert(leftShiftNode != nullptr, "Addition node was not a left-shift!");
               this->ParseLeftShiftNode(*leftShiftNode);
          }
          break;
          case ecpps::ir::NodeKind::RightBitShift:
          {
               const auto* rightShiftNode = dynamic_cast<const ecpps::ir::SSARightShiftNode*>(node);
               runtime_assert(rightShiftNode != nullptr, "Addition node was not a right-shift!");
               this->ParseRightShiftNode(*rightShiftNode);
          }
          break;
          case ecpps::ir::NodeKind::Load:
          {
               const auto* loadNode = dynamic_cast<const ecpps::ir::SSALoadNode*>(node);
               runtime_assert(loadNode != nullptr, "Load node was not a load!");
               this->ParseLoadNode(*loadNode);
          }
          break;
          case ecpps::ir::NodeKind::Or:
          {
               const auto* orNode = dynamic_cast<const ecpps::ir::SSABinOrNode*>(node);
               runtime_assert(orNode != nullptr, "Or node was not an or!");
               this->ParseBinOrNode(*orNode);
          }
          break;
          case ecpps::ir::NodeKind::And:
          {
               const auto* andNode = dynamic_cast<const ecpps::ir::SSABinAndNode*>(node);
               runtime_assert(andNode != nullptr, "And node was not an and!");
               this->ParseBinAndNode(*andNode);
          }
          break;
          case ecpps::ir::NodeKind::Xor:
          {
               const auto* xorNode = dynamic_cast<const ecpps::ir::SSABinXorNode*>(node);
               runtime_assert(xorNode != nullptr, "Xor node was not a xor!");
               this->ParseBinXorNode(*xorNode);
          }
          break;
          case ecpps::ir::NodeKind::BitwiseNot:
          {
               const auto* complementNode = dynamic_cast<const ecpps::ir::SSABitwiseNotNode*>(node);
               runtime_assert(complementNode != nullptr, "Complement node was not a complement!");
               this->ParseBitwiseNotNode(*complementNode);
          }
          break;
          case ecpps::ir::NodeKind::ArithmeticNegation:
          {
               const auto* negNode = dynamic_cast<const ecpps::ir::SSAArithmeticNegationNode*>(node);
               runtime_assert(negNode != nullptr, "Arithmetic negation node was not an arithmetic negation!");
               this->ParseArithmeticNegationNode(*negNode);
          }
          break;
          case ecpps::ir::NodeKind::Convert:
          {
               const auto* convertNode = dynamic_cast<const ecpps::ir::SSAConvertNode*>(node);
               runtime_assert(convertNode != nullptr, "Integral conversion was not a conversion!");
               this->ParseConvertNode(*convertNode);
          }
          break;
          case ecpps::ir::NodeKind::Call:
          {
               const auto* callNode = dynamic_cast<const ecpps::ir::SSACallNode*>(node);
               runtime_assert(callNode != nullptr, "Call node was not a call!");
               this->ParseCallNode(*callNode);
          }
          break;
          case ecpps::ir::NodeKind::IncomingParameter:
          {
               const auto* paramNode = dynamic_cast<const ecpps::ir::ParameterNode*>(node);
               runtime_assert(paramNode != nullptr, "Invalid node type!");
               this->ParseParameterStoreNode(*paramNode);
          }
          break;
          case ecpps::ir::NodeKind::PointerConversion:
          {
               const auto* convertNode = dynamic_cast<const ecpps::ir::SSAPointerConvertFromDecayNode*>(node);
               runtime_assert(convertNode != nullptr, "Pointer conversion was not a conversion!");
               this->ParsePointerConvertNode(*convertNode);
          }
          break;
          case ecpps::ir::NodeKind::AddressOf:
          {
               const auto* addressOfNode = dynamic_cast<const ecpps::ir::SSAAddressOfNode*>(node);
               runtime_assert(addressOfNode != nullptr, "Address-of was not an address-of!");
               this->ParseAddressOfNode(*addressOfNode);
          }
          break;
          case ecpps::ir::NodeKind::Label:
          {
               if (const auto* gotoNode = dynamic_cast<const ecpps::ir::SSAGotoNode*>(node))
               {
                    this->ParseGotoNode(*gotoNode);
                    break;
               }
               const auto* labelNode = dynamic_cast<const ecpps::ir::SSALabelNode*>(node);
               runtime_assert(labelNode != nullptr, "Not a label!");
               this->ParseLabelNode(*labelNode);
          }
          break;
          case ecpps::ir::NodeKind::Jump:
          {
               const auto* gotoNode = dynamic_cast<const ecpps::ir::SSAGotoNode*>(node);
               runtime_assert(gotoNode != nullptr, "Jump node was not a jump!");
               this->ParseGotoNode(*gotoNode);
          }
          break;
          case ecpps::ir::NodeKind::Compare:
          {
               const auto* compareNode = dynamic_cast<const ecpps::ir::SSACompareNode*>(node);
               runtime_assert(compareNode != nullptr, "Compare node was not a compare!");
               this->ParseCompareNode(*compareNode);
          }
          break;
          case ecpps::ir::NodeKind::Branch:
          {
               const auto* branchNode = dynamic_cast<const ecpps::ir::SSABranchNode*>(node);
               runtime_assert(branchNode != nullptr, "Branch node was not a branch!");
               this->ParseBranchNode(*branchNode);
          }
          break;
          default:
               this->diagnostics.push_back(std::make_unique<diagnostics::TypeError>(
                    std::format("Not implemented: {}", std::to_underlying(node->Kind())), node->Source()));
               break;
          }
     }
     catch (const VirtualNotFoundError& error)
     {
          this->diagnostics.push_back(std::make_unique<diagnostics::TypeError>(
               std::format("See earlier diagnostics [{}]", error.what()), node->Source()));
     }
}
void ecpps::codegen::ParsingContext::ParseReturnNode([[maybe_unused]] const ir::SSAReturnNode& node)
{
     if (!node.HasOperand())
     {
          ir::abstract::VirtualInstruction instruction{
               .type = ir::abstract::VirtualInstructionType::Return,
               .operands = {},
          };
          this->instructions.push_back(instruction);
          return;
     }
     auto ssaSourceIndex = node.Operand()->Index();
     auto virtualSourceIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaSourceIndex);
     this->DereferenceSSA(ssaSourceIndex);
     ir::abstract::VirtualRegister virtualSource{virtualSourceIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::Return,
          .operands = {virtualSource},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseStoreNode(const ir::SSAStoreNode& node)
{
     auto ssaSourceIndex = node.Src().Index();
     auto ssaTargetIndex = node.Target().Index();

     auto virtualSourceIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaSourceIndex);
     auto virtualTargetIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaTargetIndex);
     this->DereferenceSSA(ssaSourceIndex);
     this->DereferenceSSA(ssaTargetIndex);

     ir::abstract::VirtualRegister virtualSource{virtualSourceIndex};
     ir::abstract::VirtualRegister virtualTarget{virtualTargetIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::Copy,
          .operands = {virtualTarget, virtualSource},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseStoreIntNode(const ir::SSAStoreIntegerNode& node)
{
     auto ssaIndex = node.Target().Index();
     ir::abstract::VirtualRegister virtualIndex{this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaIndex)};
     ir::abstract::VirtualRegister sourceVirtualised{node.Src()};
     ir::abstract::VirtualInstruction instruction{.type = ir::abstract::VirtualInstructionType::CopyInteger,
                                                  .operands = {virtualIndex, sourceVirtualised}};
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseAddNode(const ir::SSAAddNode& node)
{
     auto ssaLeftIndex = node.Left().Index();
     auto ssaRightIndex = node.Right().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualLeftIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaLeftIndex);
     auto virtualRightIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaRightIndex);
     auto& describedLeft = this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualLeftIndex);

     auto width = describedLeft.width;

     runtime_assert(width == this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualRightIndex).width,
                    "Widths don't match while getting a common width");

     ir::abstract::VirtualRegister allocatedIndex(
          this->AllocateVirtual(ssaResultIndex, width, AllocationDescriptor::Type::Temporary));

     this->DereferenceSSA(ssaLeftIndex);
     this->DereferenceSSA(ssaRightIndex);

     ir::abstract::VirtualRegister virtualLeft{virtualLeftIndex};
     ir::abstract::VirtualRegister virtualRight{virtualRightIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::Add,
          .operands = {allocatedIndex, virtualLeft, virtualRight},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseSubNode(const ir::SSASubNode& node)
{
     auto ssaLeftIndex = node.Left().Index();
     auto ssaRightIndex = node.Right().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualLeftIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaLeftIndex);
     auto virtualRightIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaRightIndex);
     auto& describedLeft = this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualLeftIndex);

     auto width = describedLeft.width;

     runtime_assert(width == this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualRightIndex).width,
                    "Widths don't match while getting a common width");

     ir::abstract::VirtualRegister allocatedIndex(
          this->AllocateVirtual(ssaResultIndex, width, AllocationDescriptor::Type::Temporary));

     this->DereferenceSSA(ssaLeftIndex);
     this->DereferenceSSA(ssaRightIndex);

     ir::abstract::VirtualRegister virtualLeft{virtualLeftIndex};
     ir::abstract::VirtualRegister virtualRight{virtualRightIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::Sub,
          .operands = {allocatedIndex, virtualLeft, virtualRight},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseLeftShiftNode(const ir::SSALeftShiftNode& node)
{
     auto ssaLeftIndex = node.Left().Index();
     auto ssaRightIndex = node.Right().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualLeftIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaLeftIndex);
     auto virtualRightIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaRightIndex);
     auto& describedLeft = this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualLeftIndex);

     auto width = describedLeft.width;

     runtime_assert(width == this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualRightIndex).width,
                    "Widths don't match while getting a common width");

     ir::abstract::VirtualRegister allocatedIndex(
          this->AllocateVirtual(ssaResultIndex, width, AllocationDescriptor::Type::Temporary));

     this->DereferenceSSA(ssaLeftIndex);
     this->DereferenceSSA(ssaRightIndex);

     ir::abstract::VirtualRegister virtualLeft{virtualLeftIndex};
     ir::abstract::VirtualRegister virtualRight{virtualRightIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::LeftShift,
          .operands = {allocatedIndex, virtualLeft, virtualRight},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseRightShiftNode(const ir::SSARightShiftNode& node)
{
     auto ssaLeftIndex = node.Left().Index();
     auto ssaRightIndex = node.Right().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualLeftIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaLeftIndex);
     auto virtualRightIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaRightIndex);
     auto& describedLeft = this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualLeftIndex);

     auto width = describedLeft.width;

     runtime_assert(width == this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualRightIndex).width,
                    "Widths don't match while getting a common width");

     ir::abstract::VirtualRegister allocatedIndex(
          this->AllocateVirtual(ssaResultIndex, width, AllocationDescriptor::Type::Temporary));

     this->DereferenceSSA(ssaLeftIndex);
     this->DereferenceSSA(ssaRightIndex);

     ir::abstract::VirtualRegister virtualLeft{virtualLeftIndex};
     ir::abstract::VirtualRegister virtualRight{virtualRightIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::RightShift,
          .operands = {allocatedIndex, virtualLeft, virtualRight},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseBinOrNode(const ir::SSABinOrNode& node)
{
     auto ssaLeftIndex = node.Left().Index();
     auto ssaRightIndex = node.Right().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualLeftIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaLeftIndex);
     auto virtualRightIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaRightIndex);
     auto& describedLeft = this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualLeftIndex);

     auto width = describedLeft.width;

     runtime_assert(width == this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualRightIndex).width,
                    "Widths don't match while getting a common width");

     ir::abstract::VirtualRegister allocatedIndex(
          this->AllocateVirtual(ssaResultIndex, width, AllocationDescriptor::Type::Temporary));

     this->DereferenceSSA(ssaLeftIndex);
     this->DereferenceSSA(ssaRightIndex);

     ir::abstract::VirtualRegister virtualLeft{virtualLeftIndex};
     ir::abstract::VirtualRegister virtualRight{virtualRightIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::BinaryOr,
          .operands = {allocatedIndex, virtualLeft, virtualRight},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseBinAndNode(const ir::SSABinAndNode& node)
{
     auto ssaLeftIndex = node.Left().Index();
     auto ssaRightIndex = node.Right().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualLeftIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaLeftIndex);
     auto virtualRightIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaRightIndex);
     auto& describedLeft = this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualLeftIndex);

     auto width = describedLeft.width;

     runtime_assert(width == this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualRightIndex).width,
                    "Widths don't match while getting a common width");

     ir::abstract::VirtualRegister allocatedIndex(
          this->AllocateVirtual(ssaResultIndex, width, AllocationDescriptor::Type::Temporary));

     this->DereferenceSSA(ssaLeftIndex);
     this->DereferenceSSA(ssaRightIndex);

     ir::abstract::VirtualRegister virtualLeft{virtualLeftIndex};
     ir::abstract::VirtualRegister virtualRight{virtualRightIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::BinaryAnd,
          .operands = {allocatedIndex, virtualLeft, virtualRight},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseBinXorNode(const ir::SSABinXorNode& node)
{

     auto ssaLeftIndex = node.Left().Index();
     auto ssaRightIndex = node.Right().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualLeftIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaLeftIndex);
     auto virtualRightIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaRightIndex);
     auto& describedLeft = this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualLeftIndex);

     auto width = describedLeft.width;

     runtime_assert(width == this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualRightIndex).width,
                    "Widths don't match while getting a common width");

     ir::abstract::VirtualRegister allocatedIndex(
          this->AllocateVirtual(ssaResultIndex, width, AllocationDescriptor::Type::Temporary));

     this->DereferenceSSA(ssaLeftIndex);
     this->DereferenceSSA(ssaRightIndex);

     ir::abstract::VirtualRegister virtualLeft{virtualLeftIndex};
     ir::abstract::VirtualRegister virtualRight{virtualRightIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::BinaryXor,
          .operands = {allocatedIndex, virtualLeft, virtualRight},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseBitwiseNotNode(const ir::SSABitwiseNotNode& node)
{

     auto ssaOperandIndex = node.Operand().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualOperandIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaOperandIndex);
     auto& describedLeft = this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualOperandIndex);

     auto width = describedLeft.width;

     ir::abstract::VirtualRegister allocatedIndex(
          this->AllocateVirtual(ssaResultIndex, width, AllocationDescriptor::Type::Temporary));

     this->DereferenceSSA(ssaOperandIndex);

     ir::abstract::VirtualRegister virtualOperand{virtualOperandIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::BitwiseNot,
          .operands = {allocatedIndex, virtualOperand},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseArithmeticNegationNode(const ir::SSAArithmeticNegationNode& node)
{
     auto ssaOperandIndex = node.Operand().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualOperandIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaOperandIndex);
     auto& describedLeft = this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualOperandIndex);

     auto width = describedLeft.width;

     ir::abstract::VirtualRegister allocatedIndex(
          this->AllocateVirtual(ssaResultIndex, width, AllocationDescriptor::Type::Temporary));

     this->DereferenceSSA(ssaOperandIndex);

     ir::abstract::VirtualRegister virtualOperand{virtualOperandIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::ArithmeticNegate,
          .operands = {allocatedIndex, virtualOperand},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseConvertNode(const ir::SSAConvertNode& node)
{
     auto ssaSourceIndex = node.Src().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualSourceIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaSourceIndex);
     auto& describedSource = this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualSourceIndex);

     [[maybe_unused]] auto width = describedSource.width;

     const auto targetSize = node.Result().Width();

     runtime_assert(width == this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualSourceIndex).width,
                    "Sizes don't match while getting a common size");

     ir::abstract::VirtualRegister allocatedIndex(
          this->AllocateVirtual(ssaResultIndex, targetSize, AllocationDescriptor::Type::Temporary));

     this->DereferenceSSA(ssaSourceIndex);

     ir::abstract::VirtualRegister virtualOperand{virtualSourceIndex};
     ir::abstract::VirtualInstructionType type = ir::abstract::VirtualInstructionType::Copy;

     switch (node.Type())
     {
     case ecpps::ir::ConversionType::Reinterpret: type = ir::abstract::VirtualInstructionType::Reinterpret; break;
     case ecpps::ir::ConversionType::SignExtendAndReinterpret:
          type = ir::abstract::VirtualInstructionType::SignExtendAndReinterpret;
          break;
     case ecpps::ir::ConversionType::ZeroExtendAndReinterpret:
          type = ir::abstract::VirtualInstructionType::ZeroExtendAndReinterpret;
          break;
     case ecpps::ir::ConversionType::SignExtension: type = ir::abstract::VirtualInstructionType::SignExtension; break;
     case ecpps::ir::ConversionType::ZeroExtension: type = ir::abstract::VirtualInstructionType::ZeroExtension; break;
     case ecpps::ir::ConversionType::Truncate: type = ir::abstract::VirtualInstructionType::Truncate; break;
     }

     ir::abstract::VirtualInstruction instruction{
          .type = type,
          .operands = {allocatedIndex, virtualOperand},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseLoadNode(const ir::SSALoadNode& node)
{
     auto ssaSourceIndex = node.Address().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualSourceIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaSourceIndex);

     const auto width = node.Result().Width();

     ir::abstract::VirtualRegister allocatedIndex(
          this->AllocateVirtual(ssaResultIndex, width, AllocationDescriptor::Type::Temporary));

     this->DereferenceSSA(ssaSourceIndex);

     ir::abstract::VirtualRegister virtualSource{virtualSourceIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::Copy,
          .operands = {allocatedIndex, virtualSource},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseAllocateNode(const ir::AllocationNode& node)
{
     const auto index =
          this->AllocateVirtual(node.Node().Index(), node.Node().Width(), AllocationDescriptor::Type::Allocation);
     this->target->registerMap->SetAlignment(index, node.Node().AlignmentRequirement());
}
void ecpps::codegen::ParsingContext::ParseIntNode(const ir::SSAImmNode& node)
{
     auto width = node.Result().Width();

     auto ssaIndex = node.Result().Index();

     ir::abstract::VirtualRegister virtualIndex{
          this->AllocateVirtual(ssaIndex, width, AllocationDescriptor::Type::Temporary),
     };
     ir::abstract::VirtualRegister sourceVirtualised{node.Value()};
     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::CopyInteger,
          .operands = {virtualIndex, sourceVirtualised},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseCallNode(const ir::SSACallNode& node)
{
     const auto callIndex = CallFunctionIndex(&node.Function());

     std::size_t nextIndex{};
     for (const auto& argument : node.Arguments())
     {
          const auto argumentIndex = nextIndex++;
          auto ssaTargetIndex = argument->Index();

          auto virtualSourceIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaTargetIndex);

          ir::abstract::VirtualRegister virtualTarget{virtualSourceIndex};
          ir::abstract::VirtualRegister virtualSource{argumentIndex};

          ir::abstract::VirtualInstruction instruction{
               .type = ir::abstract::VirtualInstructionType::PassArgument,
               .operands = {ir::abstract::VirtualRegister{callIndex}, virtualTarget, virtualSource},
          };
          this->instructions.push_back(instruction);
     }

     if (node.HasResult())
     {
          auto width = node.Result().Width();
          auto ssaIndex = node.Result().Index();

          ir::abstract::VirtualRegister virtualIndex{
               this->AllocateVirtual(ssaIndex, width, AllocationDescriptor::Type::Temporary),
          };
          ir::abstract::VirtualRegister sourceVirtualised{callIndex};
          ir::abstract::VirtualInstruction instruction{
               .type = ir::abstract::VirtualInstructionType::CallWithResult,
               .operands = {virtualIndex, sourceVirtualised},
          };
          this->instructions.push_back(instruction);
          return;
     }

     ir::abstract::VirtualRegister sourceVirtualised{callIndex};
     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::Call,
          .operands = {sourceVirtualised},
     };
     this->instructions.push_back(instruction);
}

void ecpps::codegen::ParsingContext::ParseParameterStoreNode(const ir::ParameterNode& node)
{
     auto ssaTargetIndex = node.Result()->Index();
     auto paramtIndex = node.Index();

     auto virtualSourceIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaTargetIndex);

     ir::abstract::VirtualRegister virtualTarget{virtualSourceIndex};
     ir::abstract::VirtualRegister virtualSource{paramtIndex};
     const auto functionIndex = this->CallFunctionIndex(node.GetFunctionScope());

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::CopyParameter,
          .operands = {ir::abstract::VirtualRegister{functionIndex}, virtualTarget, virtualSource},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParsePointerConvertNode(const ir::SSAPointerConvertFromDecayNode& node)
{
     const auto ssaResultIndex = node.Result().Index();
     const auto resultWidth = node.Result().Width();

     if (const auto* loadDecayNode = dynamic_cast<const ir::LoadArrayDecayNode*>(&node.DecayNode());
         loadDecayNode != nullptr)
     {
          const auto* allocationRegister = loadDecayNode->GetAllocReg();
          if (allocationRegister == nullptr) throw TracedException("Array decay has no allocation register");

          const auto ssaSourceIndex = allocationRegister->Index();
          const auto virtualSourceIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaSourceIndex);

          ir::abstract::VirtualRegister allocatedIndex(
               this->AllocateVirtual(ssaResultIndex, resultWidth, AllocationDescriptor::Type::Temporary));

          this->DereferenceSSA(ssaSourceIndex);

          ir::abstract::VirtualRegister virtualSource{virtualSourceIndex};

          ir::abstract::VirtualInstruction instruction{
               .type = ir::abstract::VirtualInstructionType::AddressOf,
               .operands = {allocatedIndex, virtualSource},
          };
          this->instructions.push_back(instruction);
          return;
     }

     if (const auto* temporaryDecayNode = dynamic_cast<const ir::TemporaryIntegerArrayDecayNode*>(&node.DecayNode());
         temporaryDecayNode != nullptr)
     {
          runtime_assert(this->assembly != nullptr, "String pool is unavailable while lowering array decay");

          const auto& arrayNode = temporaryDecayNode;
          const auto elementSize = arrayNode->Type()->Size();

          std::vector<Byte> bytes{};
          bytes.reserve(arrayNode->Values().size() * elementSize);
          for (const auto value : arrayNode->Values())
          {
               for (std::size_t byteIndex = 0; byteIndex < elementSize; byteIndex++)
                    bytes.push_back(static_cast<Byte>((value >> (byteIndex * 8)) & 0xFFu));
          }

          const auto stringIndex = this->assembly->AddString(bytes);

          ir::abstract::VirtualRegister allocatedIndex(
               this->AllocateVirtual(ssaResultIndex, resultWidth, AllocationDescriptor::Type::Temporary));
          ir::abstract::VirtualRegister tableIndex{static_cast<std::size_t>(stringIndex.indexInTable)};
          ir::abstract::VirtualRegister tableOffset{static_cast<std::size_t>(stringIndex.offset)};

          ir::abstract::VirtualInstruction instruction{
               .type = ir::abstract::VirtualInstructionType::LoadStringAddress,
               .operands = {allocatedIndex, tableIndex, tableOffset},
          };
          this->instructions.push_back(instruction);
          return;
     }

     this->diagnostics.push_back(
          std::make_unique<diagnostics::TypeError>("Unsupported array-to-pointer conversion source", node.Source()));
}
void ecpps::codegen::ParsingContext::ParseAddressOfNode(const ir::SSAAddressOfNode& node)
{
     const auto* resultRegister = &node.Result();
     const auto* operandRegister = &node.Operand();

     const auto virtualResultIndex =
          AllocateVirtual(resultRegister->Index(), resultRegister->Width(), ir::abstract::AllocationClass::Temporary);
     const auto virtualSourceIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(operandRegister->Index());

     ir::abstract::VirtualRegister virtualResult{virtualResultIndex};
     ir::abstract::VirtualRegister virtualSource{virtualSourceIndex};

     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::AddressOf,
          .operands = {virtualResult, virtualSource},
     };
     this->instructions.push_back(instruction);
}

std::size_t ecpps::codegen::ParsingContext::LabelId(const std::string& name)
{
     const auto [it, inserted] = this->labelIds.try_emplace(name, this->nextLabelId);
     if (inserted) ++this->nextLabelId;
     return it->second;
}
void ecpps::codegen::ParsingContext::PlaceLabel(const std::size_t id)
{
     this->instructions.push_back(
          {.type = ir::abstract::VirtualInstructionType::Label, .operands = {ir::abstract::VirtualRegister{id}}});
}
void ecpps::codegen::ParsingContext::EmitJump(const std::size_t id)
{
     this->instructions.push_back({.type = ir::abstract::VirtualInstructionType::UnconditionalJump,
                                   .operands = {ir::abstract::VirtualRegister{id}}});
}
void ecpps::codegen::ParsingContext::EmitCompareAndJump(const ir::abstract::ConditionCode cc,
                                                        const ir::abstract::VirtualRegister lhs,
                                                        const ir::abstract::VirtualRegister rhs, const std::size_t id)
{
     this->instructions.push_back({.type = ir::abstract::VirtualInstructionType::CompareAndJump,
                                   .operands = {lhs, rhs, ir::abstract::VirtualRegister{static_cast<std::size_t>(cc)},
                                                ir::abstract::VirtualRegister{id}}});
}

void ecpps::codegen::ParsingContext::ParseLabelNode(const ir::SSALabelNode& node)
{
     const auto id = this->LabelId(std::string{node.Name()});
     if (!this->definedLabels.insert(id).second)
     {
          this->diagnostics.push_back(std::make_unique<diagnostics::TypeError>(
               std::format("Duplicate label '{}'", node.Name()), node.Source()));
          return;
     }
     this->PlaceLabel(id);
}
void ecpps::codegen::ParsingContext::ParseGotoNode(const ir::SSAGotoNode& node)
{
     const auto id = this->LabelId(std::string{node.Name()});
     this->gotoReferences.try_emplace(id, &node);
     this->EmitJump(id);
}

void ecpps::codegen::ParsingContext::FinaliseControlFlow(bool optimiseDeadJumps)
{
     using Type = ir::abstract::VirtualInstructionType;
     constexpr auto None = std::numeric_limits<std::size_t>::max();

     for (const auto& [id, node] : this->gotoReferences)
          if (!this->definedLabels.contains(id))
               this->diagnostics.push_back(std::make_unique<diagnostics::TypeError>(
                    std::format("Undefined label '{}'", node->Name()), node->Source()));

     const auto targetOf = [](const ir::abstract::VirtualInstruction& i) -> std::size_t
     {
          if (i.type == Type::UnconditionalJump) return i.operands[0].index;
          if (i.type == Type::CompareAndJump) return i.operands[3].index;
          return None;
     };

     if (!optimiseDeadJumps)
     {
          std::erase_if(this->instructions,
                        [&](const ir::abstract::VirtualInstruction& i)
                        {
                             const auto t = targetOf(i);
                             return t != None && !this->definedLabels.contains(t);
                        });
          return;
     }

     auto& code = this->instructions;
     bool changed = true;
     for (int pass = 0; changed && pass < 8; pass++)
     {
          changed = false;
          const auto count = code.size();

          std::unordered_map<std::size_t, std::size_t> position{};
          for (std::size_t i = 0; i < count; i++)
               if (code[i].type == Type::Label) position[code[i].operands[0].index] = i;

          const auto skipLabels = [&](std::size_t i)
          {
               while (i < count && code[i].type == Type::Label) i++;
               return i;
          };

          std::vector<bool> dead(count, false);

          for (std::size_t i = 0; i < count; i++)
          {
               const auto original = targetOf(code[i]);
               if (original == None) continue;
               if (!position.contains(original))
               {
                    dead[i] = true;
                    continue;
               }
               auto targetIndex = original;
               for (int hops = 0; hops < 32; hops++)
               {
                    const auto next = skipLabels(position[targetIndex]);
                    if (next >= count || code[next].type != Type::UnconditionalJump) break;
                    const auto onward = targetOf(code[next]);
                    if (onward == targetIndex || !position.contains(onward)) break;
                    targetIndex = onward;
               }
               if (targetIndex == original) continue;
               code[i].operands[code[i].type == Type::UnconditionalJump ? 0 : 3] =
                    ir::abstract::VirtualRegister{targetIndex};
               changed = true;
          }

          for (std::size_t i = 0; i < count; i++)
          {
               const auto targetIndex = targetOf(code[i]);
               if (targetIndex == None || dead[i]) continue;
               for (std::size_t j = i + 1; j < count && code[j].type == Type::Label; j++)
                    if (code[j].operands[0].index == targetIndex)
                    {
                         dead[i] = true;
                         break;
                    }
          }

          std::unordered_map<std::size_t, std::size_t> references{};
          for (std::size_t i = 0; i < count; i++)
               if (!dead[i] && targetOf(code[i]) != None) references[targetOf(code[i])]++;

          bool unreachable = false;
          for (std::size_t i = 0; i < count; i++)
          {
               if (code[i].type == Type::Label)
               {
                    if (!references.contains(code[i].operands[0].index)) dead[i] = true;
                    else
                         unreachable = false;
                    continue;
               }
               if (unreachable) dead[i] = true;
               if (dead[i]) continue;
               if (code[i].type == Type::Return || code[i].type == Type::UnconditionalJump) unreachable = true;
          }

          std::size_t write = 0;
          for (std::size_t read = 0; read < count; read++)
          {
               if (dead[read])
               {
                    changed = true;
                    continue;
               }
               if (write != read) code[write] = std::move(code[read]);
               write++;
          }
          code.erase(code.begin() + static_cast<std::ptrdiff_t>(write), code.end());
     }
}
std::size_t ecpps::codegen::ParsingContext::AllocateVirtual(const std::size_t ssaIndex, const std::size_t width,
                                                            AllocationDescriptor::Type type)
{
     const auto virtualIndex = this->virtualRegisterAllocationMap.EmplaceAllocate(ssaIndex, width, type);
     this->target->registerMap->Describe(virtualIndex, width, type);
     return virtualIndex;
}

void ecpps::codegen::ParsingContext::DereferenceSSA(const std::size_t ssaIndex)
{
     if (this->target->registerMap->DereferenceRegister(ssaIndex) != 0) return;

     this->virtualRegisterAllocationMap.ReleaseSSA(ssaIndex);
}

static Routine CompileRoutine(ecpps::codegen::AssemblyContext& context, const ecpps::ir::ProcedureNode& node,
                              ecpps::abi::api::Target* target,
                              std::vector<ecpps::diagnostics::DiagnosticsMessage>& diagnostics, bool optimiseJumps)
{
     auto& currentAbi = ecpps::abi::ABI::Current();

     std::vector<const ecpps::ir::FunctionCallNode*> allFunctionCalls;

     for (const auto* call : allFunctionCalls)
     {
          [[maybe_unused]] const auto& function = *call->Function();
     }

     for (const auto& decl : node.Locals())
     {
          if (!std::holds_alternative<ecpps::ir::Variable>(decl.local)) continue;
          const auto& variableDecl = std::get<ecpps::ir::Variable>(decl.local);

          [[maybe_unused]] const auto& type = variableDecl.type;
     }

     ecpps::codegen::ParsingContext parseContext(currentAbi);
     parseContext.target = target;
     parseContext.assembly = &context;

     for (const auto& line : node.Body())
     {
          parseContext.ParseNode(line.get());
     }
     parseContext.FinaliseControlFlow(optimiseJumps);

     diagnostics.append_range(parseContext.diagnostics | std::views::as_rvalue);

     return Routine(std::move(parseContext.instructions),
                    ecpps::abi::ABI::MangleName(node.Linkage(), node.Name(), node.CallingConvention(),
                                                node.ReturnType(),
                                                node.ParameterList() |
                                                     std::views::transform(
                                                          [](const ecpps::ir::FunctionScope::Parameter& parameter)
                                                          {
                                                               return parameter.type;
                                                          }) |
                                                     std::ranges::to<std::vector>(),
                                                node.NamespacePath()),
                    std::vector<ecpps::ir::abstract::Instruction>{}, parseContext.functionUsageTable, node.Scope(),
                    parseContext.functionUsageTable);
}

void ecpps::codegen::Compile(AssemblyContext& context, CompilerConfig& config, SourceFile& source,
                             const std::vector<ecpps::ir::NodePointer>& intermediateRepresentation,
                             ecpps::abi::api::Target* target)
{
     ir::CreateReferenceMap(*target->registerMap, intermediateRepresentation);

     for (const auto& node : intermediateRepresentation)
     {
          auto* proc = dynamic_cast<ecpps::ir::ProcedureNode*>(node.get());
          if (!proc) continue;

          target->registerMap->Reset();
          ir::CreateReferenceMap(*target->registerMap, proc->Body()); // per routine, not global

          auto routine = CompileRoutine(context, *proc, target, source.diagnostics.diagnosticsList,
                                        config.optimisations.IsEnabled(Optimisation::OptimiseDeadJumps));
          routine.registerState = *target->registerMap;
          source.compiledRoutines.push_back(std::move(routine));
     }
     config.stringArray = context.GetStringSection();
}
void ecpps::codegen::ParsingContext::ParseCompareNode(const ir::SSACompareNode& node)
{
     const auto lhsSsa = node.Left().Index();
     const auto rhsSsa = node.Right().Index();

     const auto lhsVirtual = this->virtualRegisterAllocationMap.FindVirtualBySSA(lhsSsa);
     const auto rhsVirtual = this->virtualRegisterAllocationMap.FindVirtualBySSA(rhsSsa);

     runtime_assert(this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(lhsVirtual).width ==
                         this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(rhsVirtual).width,
                    "Widths don't match in a comparison");

     this->pendingCompare = PendingCompare{.resultSsa = node.Result().Index(),
                                           .lhsSsa = lhsSsa,
                                           .rhsSsa = rhsSsa,
                                           .lhsVirtual = lhsVirtual,
                                           .rhsVirtual = rhsVirtual,
                                           .cc = ToConditionCode(node.Predicate())};
}

void ecpps::codegen::ParsingContext::ParseBranchNode(const ir::SSABranchNode& node)
{
     if (!this->pendingCompare.has_value() || this->pendingCompare->resultSsa != node.Condition().Index())
     {
          this->diagnostics.push_back(std::make_unique<diagnostics::TypeError>(
               "A branch condition must be produced by the comparison immediately preceding it", node.Source()));
          this->pendingCompare.reset();
          return;
     }

     const auto pending = *std::exchange(this->pendingCompare, std::nullopt);

     const auto trueId = this->LabelId(node.TrueLabel());
     const auto falseId = this->LabelId(node.FalseLabel());

     this->DereferenceSSA(pending.lhsSsa);
     this->DereferenceSSA(pending.rhsSsa);

     this->EmitCompareAndJump(ir::abstract::Invert(pending.cc), ir::abstract::VirtualRegister{pending.lhsVirtual},
                              ir::abstract::VirtualRegister{pending.rhsVirtual}, falseId);
     this->EmitJump(trueId);
}
