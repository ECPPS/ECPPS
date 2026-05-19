#include "PseudoAssembly.h"
#include <RuntimeAssert.h>
#include <Shared/Diagnostics.h>
#include <TypeSystem/TypeBase.h>
#include <ranges>
#include <utility>
#include <variant>
#include "../Execution/Operations.h"
#include "../Execution/Procedural.h"
#include "../Machine/ABI.h"
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
     case ecpps::ir::NodeKind::Addition:
     {
          const auto* addNode = dynamic_cast<const ecpps::ir::SSAAddNode*>(node);
          runtime_assert(addNode != nullptr, "Addition node was not an addition!");
          this->ParseAddNode(*addNode);
     }
     break;
     default:
          this->diagnostics.push_back(std::make_unique<diagnostics::TypeError>("Not implemented", node->Source()));
          break;
     }
}
void ecpps::codegen::ParsingContext::ParseReturnNode([[maybe_unused]] const ir::SSAReturnNode& node)
{
}
void ecpps::codegen::ParsingContext::ParseStoreNode([[maybe_unused]] const ir::SSAStoreNode& node)
{
}
void ecpps::codegen::ParsingContext::ParseStoreIntNode([[maybe_unused]] const ir::SSAStoreIntegerNode& node)
{
}
void ecpps::codegen::ParsingContext::ParseAddNode([[maybe_unused]] const ir::SSAAddNode& node)
{
}
void ecpps::codegen::ParsingContext::ParseAllocateNode([[maybe_unused]] const ir::AllocationNode& node)
{
}

static Routine CompileRoutine([[maybe_unused]] ecpps::codegen::AssemblyContext& context,
                              const ecpps::ir::ProcedureNode& node)
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
     for (const auto& line : node.Body()) parseContext.ParseNode(line.get());

     return Routine::Branchless(
          std::move(parseContext.instructions),
          ecpps::abi::ABI::MangleName(node.Linkage(), node.Name(), node.CallingConvention(), node.ReturnType(),
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
