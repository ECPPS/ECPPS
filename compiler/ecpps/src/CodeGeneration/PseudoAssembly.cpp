#include "PseudoAssembly.h"
#include <RuntimeAssert.h>
#include <Shared/Diagnostics.h>
#include <TypeSystem/TypeBase.h>
#include <ranges>
#include <utility>
#include <variant>
#include "../Execution/IR.h"
#include "../Execution/Operations.h"
#include "../Execution/Procedural.h"
#include "../Machine/ABI.h"
#include "AbstractNodes.h"
#include "Execution/NodeBase.h"
#include "Machine/Storage.h"
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

std::uint32_t ecpps::codegen::AssemblyContext::ReserveNextStringEntry(void) noexcept
{
     static std::atomic<std::uint32_t> next = 0;
     return next.fetch_add(1, std::memory_order::relaxed);
}

void ecpps::codegen::ParsingContext::ParseNode(const ir::NodeBase* node)
{
     if (node == nullptr) return;

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
               runtime_assert(intNode != nullptr, "Store node was not a store!");
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
          case ecpps::ir::NodeKind::Load:
          {
               const auto* addNode = dynamic_cast<const ecpps::ir::SSALoadNode*>(node);
               runtime_assert(addNode != nullptr, "Addition node was not an addition!");
               this->ParseLoadNode(*addNode);
          }
          break;
          default:
               this->diagnostics.push_back(std::make_unique<diagnostics::TypeError>("Not implemented", node->Source()));
               break;
          }
     }
     catch (VirtualNotFoundError)
     {
          this->diagnostics.push_back(
               std::make_unique<diagnostics::TypeError>("See earlier diagnostics", node->Source()));
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
     this->DereferenceSSA(virtualTargetIndex);

     ir::abstract::VirtualRegister virtualSource{virtualSourceIndex};
     ir::abstract::VirtualRegister virtualTarget{virtualTargetIndex};

     // TODO: Error check
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
     // TODO: Error check
     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::CopyInteger,
          .operands = {virtualIndex, sourceVirtualised},
     };
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

     auto size = describedLeft.size;
     auto alignment = describedLeft.alignment;

     runtime_assert(size == this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualRightIndex).size,
                    "Sizes don't match while getting a common size");

     runtime_assert(alignment ==
                         this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualRightIndex).alignment,
                    "Alignments don't match while getting a common alignment");

     this->DereferenceSSA(virtualLeftIndex);
     this->DereferenceSSA(virtualRightIndex);

     ir::abstract::VirtualRegister allocatedIndex(this->virtualRegisterAllocationMap.EmplaceAllocate(
          ssaResultIndex, size, alignment, AllocationDescriptor::Type::Temporary));

     ir::abstract::VirtualRegister virtualLeft{virtualLeftIndex};
     ir::abstract::VirtualRegister virtualRight{virtualRightIndex};

     // TODO: Error check
     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::Add,
          .operands = {allocatedIndex, virtualLeft, virtualRight},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseLoadNode(const ir::SSALoadNode& node)
{
     auto ssaSourceIndex = node.Address().Index();
     auto ssaResultIndex = node.Result().Index();

     auto virtualSourceIndex = this->virtualRegisterAllocationMap.FindVirtualBySSA(ssaSourceIndex);
     auto& describedSource = this->virtualRegisterAllocationMap.GetDescriptorFromVirtual(virtualSourceIndex);
     this->DereferenceSSA(ssaSourceIndex);

     auto size = describedSource.size;
     auto alignment = describedSource.alignment;

     ir::abstract::VirtualRegister allocatedIndex(this->virtualRegisterAllocationMap.EmplaceAllocate(
          ssaResultIndex, size, alignment, AllocationDescriptor::Type::Temporary));

     ir::abstract::VirtualRegister virtualSource{virtualSourceIndex};

     // TODO: Error check
     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::Copy,
          .operands = {allocatedIndex, virtualSource},
     };
     this->instructions.push_back(instruction);
}
void ecpps::codegen::ParsingContext::ParseAllocateNode(const ir::AllocationNode& node)
{
     std::ignore = this->virtualRegisterAllocationMap.EmplaceAllocate(
          node.Node().Index(), node.Size(), node.Alignment(), AllocationDescriptor::Type::Allocation);
}
void ecpps::codegen::ParsingContext::ParseIntNode(const ir::SSAImmNode& node)
{
     auto size = node.Result().Width() / CHAR_BIT;
     auto alignment = size;

     auto ssaIndex = node.Result().Index();

     ir::abstract::VirtualRegister virtualIndex{
          this->virtualRegisterAllocationMap.EmplaceAllocate(ssaIndex, size, alignment,
                                                             AllocationDescriptor::Type::Temporary),
     };
     ir::abstract::VirtualRegister sourceVirtualised{node.Value()};
     // TODO: Error check
     ir::abstract::VirtualInstruction instruction{
          .type = ir::abstract::VirtualInstructionType::CopyInteger,
          .operands = {virtualIndex, sourceVirtualised},
     };
     this->instructions.push_back(instruction);
}

void ecpps::codegen::ParsingContext::DereferenceSSA(const std::size_t ssaIndex)
{
     if (this->target->registerMap->DereferenceRegister(ssaIndex) != 0) return;

     this->virtualRegisterAllocationMap.ReleaseSSA(ssaIndex);
}

static Routine CompileRoutine([[maybe_unused]] ecpps::codegen::AssemblyContext& context,
                              const ecpps::ir::ProcedureNode& node, ecpps::abi::api::Target* target)
{
     auto& currentAbi = ecpps::abi::ABI::Current();

     std::vector<const ecpps::ir::FunctionCallNode*> allFunctionCalls;

     for (const auto* call : allFunctionCalls)
     {
          [[maybe_unused]] const auto& function = *call->Function();
     }

     for (const auto& decl : node.Locals())
     {
          // TODO: static & extern
          if (!std::holds_alternative<ecpps::ir::Variable>(decl.local)) continue;
          const auto& variableDecl = std::get<ecpps::ir::Variable>(decl.local);

          [[maybe_unused]] const auto& type = variableDecl.type;
     }

     ecpps::codegen::ParsingContext parseContext(currentAbi);
     parseContext.target = target;

     for (const auto& line : node.Body()) parseContext.ParseNode(line.get());

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
                                                node.NamespacePath()));
}

void ecpps::codegen::Compile(CompilerConfig& config, SourceFile& source,
                             const std::vector<ecpps::ir::NodePointer>& intermediateRepresentation,
                             ecpps::abi::api::Target* target)
{
     AssemblyContext context{config};

     ir::CreateReferenceMap(*target->registerMap, intermediateRepresentation);

     auto& patches = context.Patches();
     for (const auto& node : intermediateRepresentation)
     {
          patches = {};

          if (auto* const procedureNode = dynamic_cast<ecpps::ir::ProcedureNode*>(node.get()); procedureNode != nullptr)
               source.compiledRoutines.push_back(CompileRoutine(context, *procedureNode, target));

          source.stringTranslation = patches;
     }
     config.stringArray = context.GetStringSection();
}
