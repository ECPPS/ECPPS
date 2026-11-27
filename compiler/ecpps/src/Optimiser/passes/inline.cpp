#include "Optimiser/passes/inline.h"
#include <algorithm>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include "Execution/Operations.h"
#include "Execution/Procedural.h"

namespace
{
     using namespace ecpps::ir;

     template <typename TNode, typename... TArgs> NodePointer MakeNode(ecpps::BumpAllocator& allocator, TArgs&&... args)
     {
          return NodePointer{new (allocator) TNode(std::forward<TArgs>(args)...), IRDeleter{}};
     }

     [[nodiscard]] ecpps::Expression CloneExpression(const ecpps::ExpressionBase& original, NodePointer value)
     {
          if (dynamic_cast<const ecpps::LValue*>(&original) != nullptr)
               return std::make_unique<ecpps::LValue>(original.Type(), std::move(value),
                                                      original.IsConstantExpression());
          if (dynamic_cast<const ecpps::XValue*>(&original) != nullptr)
               return std::make_unique<ecpps::XValue>(original.Type(), std::move(value),
                                                      original.IsConstantExpression());
          if (dynamic_cast<const ecpps::PRValue*>(&original) != nullptr)
               return std::make_unique<ecpps::PRValue>(original.Type(), std::move(value),
                                                       original.IsConstantExpression());
          return {};
     }

     [[nodiscard]] SSAPointer MakeRegister(ecpps::BumpAllocator& allocator, const std::size_t index,
                                           RegisterPriorityInfo priority, const std::size_t width,
                                           const std::size_t alignment, const ecpps::Location& source)
     {
          return SSAPointer{new (allocator) SingleAssignRegisterNode(index, priority, width, alignment, source)};
     }

     [[nodiscard]] SSAPointer CloneRegister(ecpps::BumpAllocator& allocator, const SingleAssignRegisterNode& original,
                                            const std::size_t index)
     {
          return MakeRegister(allocator, index, original.PriorityInfo(), original.Width(),
                              original.AlignmentRequirement(), original.Source());
     }

     [[nodiscard]] const SingleAssignRegisterNode* ResultRegister(const NodeBase& node) noexcept
     {
#define ECPPS_RESULT_REGISTER(TNode)                                                                                   \
     if (const auto* operation = dynamic_cast<const TNode*>(&node)) return &operation->Result()
          ECPPS_RESULT_REGISTER(SSALoadNode);
          ECPPS_RESULT_REGISTER(SSACompareNode);
          ECPPS_RESULT_REGISTER(SSAAddNode);
          ECPPS_RESULT_REGISTER(SSASubNode);
          ECPPS_RESULT_REGISTER(SSAMulNode);
          ECPPS_RESULT_REGISTER(SSADivNode);
          ECPPS_RESULT_REGISTER(SSAModNode);
          ECPPS_RESULT_REGISTER(SSALeftShiftNode);
          ECPPS_RESULT_REGISTER(SSARightShiftNode);
          ECPPS_RESULT_REGISTER(SSABinOrNode);
          ECPPS_RESULT_REGISTER(SSABinAndNode);
          ECPPS_RESULT_REGISTER(SSABinXorNode);
          ECPPS_RESULT_REGISTER(SSABitwiseNotNode);
          ECPPS_RESULT_REGISTER(SSAArithmeticNegationNode);
          ECPPS_RESULT_REGISTER(SSAImmNode);
          ECPPS_RESULT_REGISTER(SSAConvertNode);
          ECPPS_RESULT_REGISTER(SSAPointerConvertNode);
          ECPPS_RESULT_REGISTER(SSAPointerConvertFromDecayNode);
          ECPPS_RESULT_REGISTER(SSAAddressOfNode);
          ECPPS_RESULT_REGISTER(SSADerefNode);
#undef ECPPS_RESULT_REGISTER
          if (const auto* allocation = dynamic_cast<const AllocationNode*>(&node)) return &allocation->Node();
          if (const auto* call = dynamic_cast<const SSACallNode*>(&node); call != nullptr && call->HasResult())
               return &call->Result();
          return nullptr;
     }

     [[nodiscard]] std::size_t NextRegisterIndex(const ProcedureNode& procedure) noexcept
     {
          std::size_t nextIndex{};
          for (const auto& node : procedure.Body())
               if (node != nullptr)
                    if (const auto* result = ResultRegister(*node); result != nullptr)
                         nextIndex = std::max(nextIndex, result->Index() + 1);
          return nextIndex;
     }

     struct CloneState
     {
          ecpps::BumpAllocator& allocator;
          std::size_t nextRegisterIndex;
          std::unordered_map<const SingleAssignRegisterNode*, const SingleAssignRegisterNode*> registers{};
          std::unordered_map<std::string, std::string> labels{};
          std::string continuationLabel{};

          [[nodiscard]] const SingleAssignRegisterNode* Map(const SingleAssignRegisterNode& original) const noexcept
          {
               const auto found = this->registers.find(&original);
               return found == this->registers.end() ? &original : found->second;
          }

          [[nodiscard]] SSAPointer NewResult(const SingleAssignRegisterNode& original)
          {
               auto result = CloneRegister(this->allocator, original, this->nextRegisterIndex++);
               this->registers.insert_or_assign(&original, result.get());
               return result;
          }
     };

     template <typename TNode>
     [[nodiscard]] bool CloneBinary(const NodeBase& node, CloneState& state, std::vector<NodePointer>& output)
     {
          if (const auto* operation = dynamic_cast<const TNode*>(&node))
          {
               auto result = state.NewResult(operation->Result());
               output.push_back(MakeNode<TNode>(state.allocator, std::move(result), state.Map(operation->Left()),
                                                state.Map(operation->Right()), operation->Source()));
               return true;
          }
          return false;
     }

     template <typename TNode>
     [[nodiscard]] bool CloneUnary(const NodeBase& node, CloneState& state, std::vector<NodePointer>& output)
     {
          if (const auto* operation = dynamic_cast<const TNode*>(&node))
          {
               auto result = state.NewResult(operation->Result());
               output.push_back(MakeNode<TNode>(state.allocator, std::move(result), state.Map(operation->Operand()),
                                                operation->Source()));
               return true;
          }
          return false;
     }

     [[nodiscard]] bool IsSupportedBodyNode(const NodeBase& node) noexcept
     {
          if (const auto* conversion = dynamic_cast<const SSAPointerConvertFromDecayNode*>(&node))
          {
               const auto& decay = conversion->DecayNode();
               if (const auto* temporary = dynamic_cast<const TemporaryIntegerArrayDecayNode*>(&decay))
                    return temporary->GetOperand() != nullptr && temporary->GetOperand()->Value() != nullptr &&
                           dynamic_cast<const IntegerArrayNode*>(temporary->GetOperand()->Value().get()) != nullptr;
               if (const auto* load = dynamic_cast<const LoadArrayDecayNode*>(&decay))
                    return load->GetAllocReg() != nullptr && load->GetOperand() != nullptr &&
                           load->GetOperand()->Value() != nullptr &&
                           dynamic_cast<const SSARegisterReferenceNode*>(load->GetOperand()->Value().get()) != nullptr;
               return false;
          }
          if (const auto* store = dynamic_cast<const SSAArrayStoreNode*>(&node))
               return dynamic_cast<const IntegerArrayNode*>(&store->ArrayNode()) != nullptr;

          if (dynamic_cast<const AllocationNode*>(&node) != nullptr ||
              dynamic_cast<const ParameterNode*>(&node) != nullptr ||
              dynamic_cast<const SSALoadNode*>(&node) != nullptr ||
              dynamic_cast<const SSALabelNode*>(&node) != nullptr ||
              dynamic_cast<const SSAGotoNode*>(&node) != nullptr ||
              dynamic_cast<const SSABranchNode*>(&node) != nullptr ||
              dynamic_cast<const SSAImmNode*>(&node) != nullptr ||
              dynamic_cast<const SSACompareNode*>(&node) != nullptr ||
              dynamic_cast<const SSAConvertNode*>(&node) != nullptr ||
              dynamic_cast<const SSAPointerConvertNode*>(&node) != nullptr ||
              dynamic_cast<const SSAAddressOfNode*>(&node) != nullptr ||
              dynamic_cast<const SSADerefNode*>(&node) != nullptr ||
              dynamic_cast<const SSAStoreNode*>(&node) != nullptr ||
              dynamic_cast<const SSAIndirectStoreNode*>(&node) != nullptr ||
              dynamic_cast<const SSAStoreIntegerNode*>(&node) != nullptr ||
              dynamic_cast<const SSACallNode*>(&node) != nullptr ||
              dynamic_cast<const SSAUnreachableNode*>(&node) != nullptr)
               return true;

#define ECPPS_SUPPORTED_BINARY(TNode)                                                                                  \
     if (dynamic_cast<const TNode*>(&node) != nullptr) return true
          ECPPS_SUPPORTED_BINARY(SSAAddNode);
          ECPPS_SUPPORTED_BINARY(SSASubNode);
          ECPPS_SUPPORTED_BINARY(SSAMulNode);
          ECPPS_SUPPORTED_BINARY(SSADivNode);
          ECPPS_SUPPORTED_BINARY(SSAModNode);
          ECPPS_SUPPORTED_BINARY(SSALeftShiftNode);
          ECPPS_SUPPORTED_BINARY(SSARightShiftNode);
          ECPPS_SUPPORTED_BINARY(SSABinOrNode);
          ECPPS_SUPPORTED_BINARY(SSABinAndNode);
          ECPPS_SUPPORTED_BINARY(SSABinXorNode);
#undef ECPPS_SUPPORTED_BINARY

          return dynamic_cast<const SSABitwiseNotNode*>(&node) != nullptr ||
                 dynamic_cast<const SSAArithmeticNegationNode*>(&node) != nullptr ||
                 dynamic_cast<const SSAReturnNode*>(&node) != nullptr;
     }

     [[nodiscard]] NodePointer CloneDecayNode(const NodeBase& node, CloneState& state)
     {
          if (const auto* temporary = dynamic_cast<const TemporaryIntegerArrayDecayNode*>(&node))
          {
               const auto* array = dynamic_cast<const IntegerArrayNode*>(temporary->GetOperand()->Value().get());
               if (array == nullptr) return {};
               auto value =
                    MakeNode<IntegerArrayNode>(state.allocator, array->Values(), array->Type(), array->Source());
               auto expression = CloneExpression(*temporary->GetOperand(), std::move(value));
               if (expression == nullptr) return {};
               return MakeNode<TemporaryIntegerArrayDecayNode>(state.allocator, std::move(expression), node.Source());
          }
          if (const auto* load = dynamic_cast<const LoadArrayDecayNode*>(&node))
          {
               if (load->GetAllocReg() == nullptr || load->GetOperand() == nullptr ||
                   load->GetOperand()->Value() == nullptr)
                    return {};
               auto value = MakeNode<SSARegisterReferenceNode>(state.allocator, state.Map(*load->GetAllocReg()),
                                                               load->GetOperand()->Value()->Source());
               auto expression = CloneExpression(*load->GetOperand(), std::move(value));
               if (expression == nullptr) return {};
               return MakeNode<LoadArrayDecayNode>(state.allocator, std::move(expression), node.Source());
          }
          return {};
     }

     [[nodiscard]] bool CanInlineBody(const ProcedureNode& callee, const SSACallNode& call)
     {
          if (callee.Body().empty()) return false;
          const auto& finalNode = callee.Body().back();
          if (dynamic_cast<const SSAReturnNode*>(finalNode.get()) == nullptr &&
              dynamic_cast<const SSAUnreachableNode*>(finalNode.get()) == nullptr &&
              dynamic_cast<const SSAGotoNode*>(finalNode.get()) == nullptr &&
              dynamic_cast<const SSABranchNode*>(finalNode.get()) == nullptr)
               return false;

          std::unordered_set<const SingleAssignRegisterNode*> allocatedRegisters{};
          std::unordered_set<std::string> labels{};
          std::unordered_set<std::string> branchTargets{};
          const bool resultIsUsed = call.HasResult() && call.Result().UseCount() != 0;
          for (std::size_t index = 0; index < callee.Body().size(); ++index)
          {
               const auto& node = callee.Body()[index];
               if (node == nullptr || !IsSupportedBodyNode(*node)) return false;
               if (const auto* allocation = dynamic_cast<const AllocationNode*>(node.get()))
                    allocatedRegisters.insert(&allocation->Node());
               else if (const auto* label = dynamic_cast<const SSALabelNode*>(node.get()))
               {
                    if (!labels.insert(label->Name()).second) return false;
               }
               else if (const auto* jump = dynamic_cast<const SSAGotoNode*>(node.get()))
                    branchTargets.insert(jump->Name());
               else if (const auto* branch = dynamic_cast<const SSABranchNode*>(node.get()))
               {
                    branchTargets.insert(branch->TrueLabel());
                    branchTargets.insert(branch->FalseLabel());
               }
               else if (const auto* returnNode = dynamic_cast<const SSAReturnNode*>(node.get());
                        returnNode != nullptr && resultIsUsed && !returnNode->HasOperand())
                    return false;
               else if (const auto* parameter = dynamic_cast<const ParameterNode*>(node.get()))
               {
                    if (!allocatedRegisters.contains(parameter->Result()) ||
                        parameter->Index() >= call.Arguments().size())
                         return false;
               }
          }
          return std::ranges::all_of(branchTargets,
                                     [&labels](const std::string& target)
                                     {
                                          return labels.contains(target);
                                     });
     }

     [[nodiscard]] std::size_t ReturnCount(const ProcedureNode& callee) noexcept
     {
          return static_cast<std::size_t>(std::ranges::count_if(callee.Body(),
                                                                [](const NodePointer& node)
                                                                {
                                                                     return dynamic_cast<const SSAReturnNode*>(
                                                                                 node.get()) != nullptr;
                                                                }));
     }

     [[nodiscard]] bool NeedsReturnContinuation(const ProcedureNode& callee) noexcept
     {
          if (ReturnCount(callee) != 1) return true;
          return dynamic_cast<const SSAReturnNode*>(callee.Body().back().get()) == nullptr;
     }

     [[nodiscard]] bool CanInline(const ProcedureNode& caller, const ProcedureNode& callee,
                                  const SSACallNode& call) noexcept
     {
          const auto* callerScope = caller.Scope();
          const auto* calleeScope = callee.Scope();
          if (callerScope == nullptr || calleeScope == nullptr || callerScope == calleeScope ||
              call.AbiCleanupKind() != ABICleanupKind::None || calleeScope->isExtern ||
              calleeScope->isDllImportExport || calleeScope->isDeleted || calleeScope->isVariadic ||
              calleeScope->returnsByPointer || callerScope->callingConvention != calleeScope->callingConvention ||
              call.Arguments().size() != calleeScope->CallingConventionParameterCount())
               return false;

          if (std::ranges::any_of(calleeScope->parameters,
                                  [](const FunctionScope::Parameter& parameter)
                                  {
                                       return parameter.isVariadic || ecpps::typeSystem::IsClass(parameter.type);
                                  }))
               return false;

          return CanInlineBody(callee, call);
     }

     using CallGraph = std::unordered_map<const FunctionScope*, std::vector<const FunctionScope*>>;

     struct CallAnalysis
     {
          std::unordered_map<const FunctionScope*, ProcedureNode*> procedures{};
          CallGraph graph{};
          std::unordered_map<const FunctionScope*, std::size_t> counts{};
     };

     [[nodiscard]] CallAnalysis AnalyzeCalls(std::vector<NodePointer>& input)
     {
          CallAnalysis analysis{};
          for (auto& node : input)
          {
               auto* procedure = dynamic_cast<ProcedureNode*>(node.get());
               if (procedure == nullptr || procedure->Scope() == nullptr) continue;

               const auto* scope = procedure->Scope();
               analysis.procedures.emplace(scope, procedure);
               for (const auto& bodyNode : procedure->Body())
                    if (const auto* call = dynamic_cast<const SSACallNode*>(bodyNode.get()))
                    {
                         analysis.graph[scope].push_back(&call->Function());
                         analysis.counts[&call->Function()]++;
                    }
          }
          return analysis;
     }

     [[nodiscard]] bool Reaches(const FunctionScope* from, const FunctionScope* target, const CallGraph& graph)
     {
          std::vector<const FunctionScope*> pending{from};
          std::unordered_set<const FunctionScope*> visited{};
          while (!pending.empty())
          {
               const auto* function = pending.back();
               pending.pop_back();
               if (!visited.insert(function).second) continue;
               if (function == target) return true;
               const auto found = graph.find(function);
               if (found != graph.end()) pending.insert(pending.end(), found->second.begin(), found->second.end());
          }
          return false;
     }

     [[nodiscard]] bool IsRecursive(const FunctionScope* function, const CallGraph& graph)
     {
          const auto found = graph.find(function);
          if (found == graph.end()) return false;
          return std::ranges::any_of(found->second,
                                     [function, &graph](const FunctionScope* callee)
                                     {
                                          return callee == function || Reaches(callee, function, graph);
                                     });
     }

     struct Candidate
     {
          ProcedureNode* caller{};
          ProcedureNode* callee{};
          SSACallNode* call{};
          std::size_t growth{};
          std::size_t savedCallOverhead{};
          bool singleCall{};
     };

     [[nodiscard]] std::size_t EstimatedGrowth(const ProcedureNode& callee, const SSACallNode& call) noexcept
     {
          const bool resultIsUsed = call.HasResult() && call.Result().UseCount() != 0;
          if (NeedsReturnContinuation(callee))
               return resultIsUsed ? callee.Body().size() + ReturnCount(callee) + 2 : callee.Body().size();
          if (resultIsUsed) return callee.Body().size() + 1;
          return callee.Body().size() > 2 ? callee.Body().size() - 2 : 0;
     }

     [[nodiscard]] bool CandidateBefore(const Candidate& left, const Candidate& right) noexcept
     {
          const auto leftScore = static_cast<long double>(left.savedCallOverhead) /
                                 static_cast<long double>(std::max<std::size_t>(1, left.growth));
          const auto rightScore = static_cast<long double>(right.savedCallOverhead) /
                                  static_cast<long double>(std::max<std::size_t>(1, right.growth));
          if (leftScore > rightScore) return true;
          if (leftScore < rightScore) return false;
          if (left.singleCall != right.singleCall) return left.singleCall;
          if (left.growth != right.growth) return left.growth < right.growth;
          return left.callee->Body().size() < right.callee->Body().size();
     }

     [[nodiscard]] std::vector<Candidate> FindCandidates(std::vector<NodePointer>& input, const std::size_t growthLimit)
     {
          const auto analysis = AnalyzeCalls(input);
          std::vector<Candidate> candidates{};
          for (auto& node : input)
          {
               auto* caller = dynamic_cast<ProcedureNode*>(node.get());
               if (caller == nullptr || caller->Scope() == nullptr) continue;
               for (const auto& bodyNode : caller->MutableBody())
               {
                    auto* call = dynamic_cast<SSACallNode*>(bodyNode.get());
                    if (call == nullptr) continue;
                    const auto found = analysis.procedures.find(&call->Function());
                    if (found == analysis.procedures.end()) continue;
                    auto* callee = found->second;
                    const auto* calleeScope = callee->Scope();
                    if (!CanInline(*caller, *callee, *call) || IsRecursive(calleeScope, analysis.graph) ||
                        Reaches(calleeScope, caller->Scope(), analysis.graph))
                         continue;

                    const auto usage = analysis.counts.find(calleeScope);
                    const auto growth = EstimatedGrowth(*callee, *call);
                    if (growth > growthLimit) continue;
                    candidates.push_back({
                         .caller = caller,
                         .callee = callee,
                         .call = call,
                         .growth = growth,
                         .savedCallOverhead =
                              call->Arguments().size() + 1uz + static_cast<std::size_t>(call->HasResult()),
                         .singleCall = usage != analysis.counts.end() && usage->second == 1,
                    });
               }
          }
          return candidates;
     }

     [[nodiscard]] bool CloneNode(const NodeBase& node, CloneState& state, const SSACallNode& call,
                                  const SingleAssignRegisterNode* returnSlot, std::vector<NodePointer>& output)
     {
          if (const auto* label = dynamic_cast<const SSALabelNode*>(&node))
          {
               output.push_back(MakeNode<SSALabelNode>(state.allocator, state.labels.at(label->Name()), node.Source()));
               return true;
          }
          if (const auto* jump = dynamic_cast<const SSAGotoNode*>(&node))
          {
               output.push_back(MakeNode<SSAGotoNode>(state.allocator, state.labels.at(jump->Name()), node.Source()));
               return true;
          }
          if (const auto* branch = dynamic_cast<const SSABranchNode*>(&node))
          {
               output.push_back(MakeNode<SSABranchNode>(state.allocator, state.Map(branch->Condition()),
                                                        state.labels.at(branch->TrueLabel()),
                                                        state.labels.at(branch->FalseLabel()), node.Source()));
               return true;
          }
          if (dynamic_cast<const SSAUnreachableNode*>(&node) != nullptr)
          {
               output.push_back(MakeNode<SSAUnreachableNode>(state.allocator, node.Source()));
               return true;
          }
          if (const auto* allocation = dynamic_cast<const AllocationNode*>(&node))
          {
               auto result = state.NewResult(allocation->Node());
               output.push_back(MakeNode<AllocationNode>(state.allocator, std::move(result), node.Source()));
               return true;
          }
          if (const auto* parameter = dynamic_cast<const ParameterNode*>(&node))
          {
               const auto index = static_cast<std::size_t>(parameter->Index());
               const auto found = state.registers.find(parameter->Result());
               if (found == state.registers.end() || index >= call.Arguments().size()) return false;
               MemoryAccess access{};
               access.alignment = static_cast<std::uint32_t>(found->second->AlignmentRequirement());
               output.push_back(MakeNode<SSAStoreNode>(state.allocator, found->second, call.Arguments()[index], access,
                                                       node.Source()));
               return true;
          }
          if (const auto* load = dynamic_cast<const SSALoadNode*>(&node))
          {
               auto result = state.NewResult(load->Result());
               output.push_back(MakeNode<SSALoadNode>(state.allocator, std::move(result), state.Map(load->Address()),
                                                      load->Access(), node.Source()));
               return true;
          }
          if (const auto* immediate = dynamic_cast<const SSAImmNode*>(&node))
          {
               auto result = state.NewResult(immediate->Result());
               output.push_back(
                    MakeNode<SSAImmNode>(state.allocator, std::move(result), immediate->Value(), node.Source()));
               return true;
          }
          if (const auto* compare = dynamic_cast<const SSACompareNode*>(&node))
          {
               auto result = state.NewResult(compare->Result());
               output.push_back(MakeNode<SSACompareNode>(state.allocator, std::move(result), state.Map(compare->Left()),
                                                         state.Map(compare->Right()), compare->Predicate(),
                                                         node.Source()));
               return true;
          }
          if (const auto* conversion = dynamic_cast<const SSAConvertNode*>(&node))
          {
               auto result = state.NewResult(conversion->Result());
               output.push_back(MakeNode<SSAConvertNode>(state.allocator, std::move(result),
                                                         state.Map(conversion->Src()), conversion->Type(),
                                                         node.Source()));
               return true;
          }
          if (const auto* conversion = dynamic_cast<const SSAPointerConvertNode*>(&node))
          {
               auto result = state.NewResult(conversion->Result());
               output.push_back(MakeNode<SSAPointerConvertNode>(state.allocator, std::move(result),
                                                                state.Map(conversion->Src()), conversion->TargetType(),
                                                                node.Source()));
               return true;
          }
          if (const auto* address = dynamic_cast<const SSAAddressOfNode*>(&node))
          {
               auto result = state.NewResult(address->Result());
               output.push_back(MakeNode<SSAAddressOfNode>(state.allocator, std::move(result),
                                                           state.Map(address->Operand()), node.Source()));
               return true;
          }
          if (const auto* conversion = dynamic_cast<const SSAPointerConvertFromDecayNode*>(&node))
          {
               auto decay = CloneDecayNode(conversion->DecayNode(), state);
               if (decay == nullptr) return false;
               auto result = state.NewResult(conversion->Result());
               output.push_back(MakeNode<SSAPointerConvertFromDecayNode>(
                    state.allocator, std::move(result), std::move(decay), conversion->TargetType(), node.Source()));
               return true;
          }
          if (const auto* deref = dynamic_cast<const SSADerefNode*>(&node))
          {
               auto result = state.NewResult(deref->Result());
               output.push_back(MakeNode<SSADerefNode>(state.allocator, std::move(result), state.Map(deref->Ptr()),
                                                       deref->Access(), node.Source()));
               return true;
          }
          if (const auto* store = dynamic_cast<const SSAStoreNode*>(&node))
          {
               output.push_back(MakeNode<SSAStoreNode>(state.allocator, state.Map(store->Target()),
                                                       state.Map(store->Src()), store->Access(), node.Source()));
               return true;
          }
          if (const auto* store = dynamic_cast<const SSAIndirectStoreNode*>(&node))
          {
               output.push_back(MakeNode<SSAIndirectStoreNode>(state.allocator, state.Map(store->Target()),
                                                               state.Map(store->Src()), store->Access(),
                                                               node.Source()));
               return true;
          }
          if (const auto* store = dynamic_cast<const SSAStoreIntegerNode*>(&node))
          {
               output.push_back(MakeNode<SSAStoreIntegerNode>(state.allocator, state.Map(store->Target()), store->Src(),
                                                              store->Access(), node.Source()));
               return true;
          }
          if (const auto* store = dynamic_cast<const SSAArrayStoreNode*>(&node))
          {
               const auto* array = dynamic_cast<const IntegerArrayNode*>(&store->ArrayNode());
               if (array == nullptr) return false;
               auto arrayCopy =
                    MakeNode<IntegerArrayNode>(state.allocator, array->Values(), array->Type(), array->Source());
               output.push_back(MakeNode<SSAArrayStoreNode>(state.allocator, state.Map(store->Target()),
                                                            std::move(arrayCopy), node.Source()));
               return true;
          }
          if (const auto* callNode = dynamic_cast<const SSACallNode*>(&node))
          {
               std::optional<SSAPointer> result{};
               if (callNode->HasResult()) result.emplace(state.NewResult(callNode->Result()));
               std::vector<const SingleAssignRegisterNode*> arguments{};
               arguments.reserve(callNode->Arguments().size());
               for (const auto* argument : callNode->Arguments()) arguments.push_back(state.Map(*argument));
               output.push_back(MakeNode<SSACallNode>(state.allocator, std::move(result), &callNode->Function(),
                                                      std::move(arguments), node.Source(), callNode->AbiCleanupKind()));
               return true;
          }

          if (CloneBinary<SSAAddNode>(node, state, output) || CloneBinary<SSASubNode>(node, state, output) ||
              CloneBinary<SSAMulNode>(node, state, output) || CloneBinary<SSADivNode>(node, state, output) ||
              CloneBinary<SSAModNode>(node, state, output) || CloneBinary<SSALeftShiftNode>(node, state, output) ||
              CloneBinary<SSARightShiftNode>(node, state, output) || CloneBinary<SSABinOrNode>(node, state, output) ||
              CloneBinary<SSABinAndNode>(node, state, output) || CloneBinary<SSABinXorNode>(node, state, output) ||
              CloneUnary<SSABitwiseNotNode>(node, state, output) ||
              CloneUnary<SSAArithmeticNegationNode>(node, state, output))
               return true;

          if (const auto* returnNode = dynamic_cast<const SSAReturnNode*>(&node))
          {
               const bool resultIsUsed = call.HasResult() && call.Result().UseCount() != 0;
               if (resultIsUsed)
               {
                    if (!returnNode->HasOperand() || returnSlot == nullptr) return false;
                    MemoryAccess access{};
                    access.alignment = static_cast<std::uint32_t>(returnSlot->AlignmentRequirement());
                    output.push_back(MakeNode<SSAStoreNode>(state.allocator, returnSlot,
                                                            state.Map(*returnNode->Operand()), access, node.Source()));
               }
               if (!state.continuationLabel.empty())
                    output.push_back(MakeNode<SSAGotoNode>(state.allocator, state.continuationLabel, node.Source()));
               return true;
          }

          return false;
     }

     [[nodiscard]] bool InlineCall(Candidate candidate, ecpps::BumpAllocator& allocator)
     {
          auto& body = candidate.caller->MutableBody();
          const auto callPosition = std::ranges::find_if(body,
                                                         [target = candidate.call](const NodePointer& node)
                                                         {
                                                              return node.get() == target;
                                                         });
          if (callPosition == body.end()) return false;

          auto& call = *candidate.call;
          const bool resultIsUsed = call.HasResult() && call.Result().UseCount() != 0;
          CloneState state{.allocator = allocator, .nextRegisterIndex = NextRegisterIndex(*candidate.caller)};
          std::vector<NodePointer> replacement{};
          std::string labelPrefix = "__inline_" + std::to_string(state.nextRegisterIndex) + "_";
          while (std::ranges::any_of(body,
                                     [&labelPrefix](const NodePointer& node)
                                     {
                                          const auto* label = dynamic_cast<const SSALabelNode*>(node.get());
                                          return label != nullptr && label->Name().starts_with(labelPrefix);
                                     }))
               labelPrefix += "_";
          for (const auto& node : candidate.callee->Body())
               if (const auto* label = dynamic_cast<const SSALabelNode*>(node.get()))
                    state.labels.emplace(label->Name(), labelPrefix + std::to_string(state.labels.size()));
          if (NeedsReturnContinuation(*candidate.callee)) state.continuationLabel = labelPrefix + "return";

          const SingleAssignRegisterNode* returnSlotPointer{};
          if (resultIsUsed)
          {
               const auto& callResult = call.Result();
               auto slot = MakeRegister(allocator, state.nextRegisterIndex++,
                                        RegisterPriorityInfo{.regClass = RegisterClass::LocalVariable},
                                        callResult.Width(), callResult.AlignmentRequirement(), call.Source());
               returnSlotPointer = slot.get();
               replacement.push_back(MakeNode<AllocationNode>(allocator, std::move(slot), call.Source()));
          }

          for (const auto& node : candidate.callee->Body())
               if (!CloneNode(*node, state, call, returnSlotPointer, replacement)) return false;

          if (!state.continuationLabel.empty())
               replacement.push_back(MakeNode<SSALabelNode>(allocator, state.continuationLabel, call.Source()));

          if (resultIsUsed)
          {
               auto result = call.TakeResult();
               if (!result.has_value()) return false;
               MemoryAccess access{};
               access.alignment = static_cast<std::uint32_t>(returnSlotPointer->AlignmentRequirement());
               replacement.push_back(
                    MakeNode<SSALoadNode>(allocator, std::move(*result), returnSlotPointer, access, call.Source()));
          }

          for (const auto* argument : call.Arguments()) argument->Unuse();
          auto position = body.erase(callPosition);
          body.insert(position, std::make_move_iterator(replacement.begin()),
                      std::make_move_iterator(replacement.end()));
          return true;
     }

     [[nodiscard]] std::size_t ModuleSize(const std::vector<NodePointer>& input) noexcept
     {
          std::size_t size{};
          for (const auto& node : input)
               if (const auto* procedure = dynamic_cast<const ProcedureNode*>(node.get()))
                    size += procedure->Body().size();
          return size;
     }
} // namespace

ecpps::optimisations::PassInput ecpps::optimisations::InlinePass::Execute(PassInput input) const
{
     runtime_assert(this->_allocator != nullptr, "Inlining pass requires a live IR allocator");
     if (this->_allocator == nullptr) return input;

     const auto moduleSize = ModuleSize(input);
     // Limit growth to about 20% of the original module, with a small-module allowance.
     const auto growthLimit = std::max<std::size_t>(8, (moduleSize + 4) / 5);
     std::size_t growth{};
     while (growth < growthLimit)
     {
          auto candidates = FindCandidates(input, growthLimit - growth);
          bool inlined{};
          while (!candidates.empty())
          {
               const auto selected = std::ranges::min_element(candidates, CandidateBefore);
               const auto candidate = *selected;
               if (!InlineCall(candidate, *this->_allocator))
               {
                    candidates.erase(selected);
                    continue;
               }
               growth += candidate.growth;
               inlined = true;
               break;
          }
          if (!inlined) break;
     }
     return input;
}
