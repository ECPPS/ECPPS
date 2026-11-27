#include "Optimiser/passes/deadCode.h"
#include <cstddef>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "CodeGeneration/AbstractNodes.h"
#include "Execution/Operations.h"
#include "Execution/Procedural.h"

namespace
{
     template <typename... TOperations> struct OperationSet
     {
     };

     using PureOperations =
          OperationSet<ecpps::ir::SSACompareNode, ecpps::ir::SSAAddNode, ecpps::ir::SSASubNode, ecpps::ir::SSAMulNode,
                       ecpps::ir::SSADivNode, ecpps::ir::SSAModNode, ecpps::ir::SSALeftShiftNode,
                       ecpps::ir::SSARightShiftNode, ecpps::ir::SSABinOrNode, ecpps::ir::SSABinAndNode,
                       ecpps::ir::SSABinXorNode, ecpps::ir::SSABitwiseNotNode, ecpps::ir::SSAArithmeticNegationNode,
                       ecpps::ir::SSAImmNode, ecpps::ir::SSAConvertNode, ecpps::ir::SSAPointerConvertNode,
                       ecpps::ir::SSAPointerConvertFromDecayNode, ecpps::ir::SSAAddressOfNode>;

     using ecpps::ir::abstract::VirtualInstruction;
     using ecpps::ir::abstract::VirtualInstructionType;
     using ecpps::ir::abstract::VirtualRegister;

     [[nodiscard]] bool IsPureVirtualProducer(const VirtualInstructionType type) noexcept
     {
          using enum VirtualInstructionType;
          switch (type)
          {
          case Copy:
          case CopyInteger:
          case Add:
          case Sub:
          case LeftShift:
          case RightShift:
          case BinaryOr:
          case BinaryAnd:
          case BinaryXor:
          case BitwiseNot:
          case ArithmeticNegate:
          case Multiplication:
          case Reinterpret:
          case ZeroExtension:
          case SignExtension:
          case SignExtendAndReinterpret:
          case ZeroExtendAndReinterpret:
          case Truncate:
          case CopyParameter:
          case AddressOf:
          case LoadStringAddress: return true;
          default: return false;
          }
     }

     [[nodiscard]] bool IsImmutable(const VirtualRegister reg, const ecpps::ir::abstract::VirtualRegisterMap& registers)
     {
          return registers.GetAllocationClass(reg) < ecpps::ir::abstract::AllocationClass::Allocation;
     }

     [[nodiscard]] bool IsElidableAccess(const ecpps::ir::MemoryAccess& access) noexcept
     {
          return !access.isVolatile && !access.atomicOrdering && !access.isAggregate && access.addressSpace == 0;
     }

     [[nodiscard]] std::unordered_map<std::size_t, std::size_t> CountVirtualUses(
          const std::vector<VirtualInstruction>& instructions)
     {
          std::unordered_map<std::size_t, std::size_t> useCounts{};
          for (const auto& instruction : instructions)
               ecpps::ir::abstract::ForEachSourceOperand(instruction,
                                                         [&useCounts](const VirtualRegister& source)
                                                         {
                                                              useCounts[source.index]++;
                                                         });
          return useCounts;
     }

     [[nodiscard]] bool IsUnused(const ecpps::ir::SingleAssignRegisterNode& result) noexcept
     {
          return result.UseCount() == 0;
     }

     [[nodiscard]] bool IsRemovableRead(const ecpps::ir::SingleAssignRegisterNode& result,
                                        const ecpps::ir::MemoryAccess& access) noexcept
     {
          return IsUnused(result) && !access.isVolatile && !access.atomicOrdering && access.addressSpace == 0;
     }

     template <typename... TOperations>
     [[nodiscard]] bool HasUnusedResult(const ecpps::ir::NodeBase& node,
                                        [[maybe_unused]] OperationSet<TOperations...> operationSet) noexcept
     {
          return (
               [&]
               {
                    const auto* operation = dynamic_cast<const TOperations*>(&node);
                    return operation != nullptr && IsUnused(operation->Result());
               }() ||
               ...);
     }

     [[nodiscard]] bool IsRemovable(const ecpps::ir::NodeBase& node) noexcept
     {
          using namespace ecpps::ir;

          if (const auto* allocation = dynamic_cast<const AllocationNode*>(&node)) return IsUnused(allocation->Node());
          if (const auto* parameter = dynamic_cast<const ParameterNode*>(&node)) return IsUnused(*parameter->Result());
          if (const auto* load = dynamic_cast<const SSALoadNode*>(&node))
               return IsRemovableRead(load->Result(), load->Access());
          if (const auto* deref = dynamic_cast<const SSADerefNode*>(&node))
               return IsRemovableRead(deref->Result(), deref->Access());

          return HasUnusedResult(node, PureOperations{});
     }

     template <typename TOperation> bool UnuseIfPresent(const ecpps::ir::NodeBase& node) noexcept
     {
          if (const auto* operation = dynamic_cast<const TOperation*>(&node))
          {
               if constexpr (requires {
                                  operation->Left().Unuse();
                                  operation->Right().Unuse();
                             })
               {
                    operation->Left().Unuse();
                    operation->Right().Unuse();
               }
               else if constexpr (requires { operation->Src().Unuse(); })
                    operation->Src().Unuse();
               else if constexpr (requires { operation->Operand().Unuse(); })
                    operation->Operand().Unuse();
               else if constexpr (requires { operation->DecayNode(); })
               {
                    const auto* decay = dynamic_cast<const ecpps::ir::LoadArrayDecayNode*>(&operation->DecayNode());
                    if (decay != nullptr && decay->GetAllocReg() != nullptr) decay->GetAllocReg()->Unuse();
               }
               else
                    static_assert(std::is_same_v<TOperation, ecpps::ir::SSAImmNode>,
                                  "Add operand unuse handling for this pure operation");
               return true;
          }
          return false;
     }

     template <typename... TOperations>
     bool UnusePureOperands(const ecpps::ir::NodeBase& node,
                            [[maybe_unused]] OperationSet<TOperations...> operationSet) noexcept
     {
          return (UnuseIfPresent<TOperations>(node) || ...);
     }

     void UnuseOperands(const ecpps::ir::NodeBase& node) noexcept
     {
          using namespace ecpps::ir;

          if (UnusePureOperands(node, PureOperations{})) return;
          if (const auto* load = dynamic_cast<const SSALoadNode*>(&node)) load->Address().Unuse();
          else if (const auto* deref = dynamic_cast<const SSADerefNode*>(&node))
               deref->Ptr().Unuse();
     }
} // namespace

ecpps::optimisations::PassInput ecpps::optimisations::DeadCodePass::Execute(PassInput input) const
{
     for (auto& node : input)
     {
          auto* procedure = dynamic_cast<ir::ProcedureNode*>(node.get());
          if (procedure == nullptr) continue;

          auto& body = procedure->MutableBody();
          bool removed = true;
          while (removed)
          {
               removed = false;
               // Erase a ParameterNode before its AllocationNode, which owns the referenced register.
               for (std::size_t index = body.size(); index > 0; --index)
               {
                    auto iterator = body.begin() + static_cast<std::ptrdiff_t>(index - 1);
                    if (*iterator == nullptr || !IsRemovable(**iterator))
                    {
                         if (auto* call = dynamic_cast<ir::SSACallNode*>(iterator->get());
                             call != nullptr && call->HasResult() && IsUnused(call->Result()))
                              call->DiscardResult();
                         continue;
                    }

                    UnuseOperands(**iterator);
                    body.erase(iterator);
                    removed = true;
               }
          }
     }
     return input;
}

void ecpps::optimisations::OptimiseVirtualInstructions(std::vector<ir::abstract::VirtualInstruction>& instructions,
                                                       const ir::abstract::VirtualRegisterMap& registers,
                                                       const bool removeRedundantMoves, const bool eliminateDeadCode)
{
     using namespace ir::abstract;
     if (!removeRedundantMoves && !eliminateDeadCode) return;

     if (removeRedundantMoves)
     {
          std::unordered_map<std::size_t, std::size_t> definitionCounts{};
          for (const auto& instruction : instructions)
               if (const auto destination = DestinationOperandIndex(instruction))
                    definitionCounts[instruction.operands[*destination].index]++;

          std::unordered_map<std::size_t, VirtualRegister> replacements{};
          std::vector<bool> redundant(instructions.size());
          for (std::size_t index = 0; index < instructions.size(); index++)
          {
               auto& instruction = instructions[index];
               ForEachSourceOperand(instruction,
                                    [&replacements](VirtualRegister& source)
                                    {
                                         if (const auto replacement = replacements.find(source.index);
                                             replacement != replacements.end())
                                              source = replacement->second;
                                    });

               if ((instruction.type != VirtualInstructionType::Copy &&
                    instruction.type != VirtualInstructionType::Reinterpret) ||
                   instruction.memoryEffect.has_value() || instruction.operands.size() < 2 ||
                   definitionCounts[instruction.operands[0].index] != 1)
                    continue;

               const auto destination = instruction.operands[0];
               const auto source = instruction.operands[1];
               if (!IsImmutable(destination, registers) || !IsImmutable(source, registers) ||
                   registers.GetWidth(destination) != registers.GetWidth(source))
                    continue;

               if (destination != source) replacements[destination.index] = source;
               redundant[index] = true;
          }

          std::vector<VirtualInstruction> folded{};
          folded.reserve(instructions.size());
          for (std::size_t index = 0; index < instructions.size(); index++)
               if (!redundant[index]) folded.push_back(std::move(instructions[index]));
          instructions = std::move(folded);
     }

     if (!eliminateDeadCode) return;

     auto useCounts = CountVirtualUses(instructions);
     std::unordered_map<std::size_t, std::size_t> definitionCounts{};
     std::unordered_map<std::size_t, std::vector<std::size_t>> definitionIndices{};
     for (std::size_t index = 0; index < instructions.size(); index++)
          if (const auto destination = DestinationOperandIndex(instructions[index]))
          {
               const auto registerIndex = instructions[index].operands[*destination].index;
               definitionCounts[registerIndex]++;
               definitionIndices[registerIndex].push_back(index);
          }

     const auto canRemove = [&](const std::size_t index)
     {
          const auto& instruction = instructions[index];
          const auto destination = DestinationOperandIndex(instruction);
          if (!destination) return false;
          const VirtualRegister result = instruction.operands[*destination];
          if (instruction.memoryEffect.has_value())
          {
               if (!IsElidableAccess(instruction.memoryEffect->access)) return false;
               if (instruction.memoryEffect->isWrite)
                    return instruction.type == VirtualInstructionType::Copy ||
                           instruction.type == VirtualInstructionType::CopyInteger;
               return definitionCounts[result.index] == 1 && (instruction.type == VirtualInstructionType::Copy ||
                                                              instruction.type == VirtualInstructionType::Indirection);
          }
          return IsPureVirtualProducer(instruction.type) && definitionCounts[result.index] == 1 &&
                 IsImmutable(result, registers);
     };

     std::vector<std::size_t> worklist{};
     std::vector<bool> queued(instructions.size());
     std::vector<bool> removed(instructions.size());
     for (std::size_t index = 0; index < instructions.size(); index++)
          if (canRemove(index))
          {
               const auto destination = DestinationOperandIndex(instructions[index]);
               if (!destination.has_value()) continue;
               if (useCounts[instructions[index].operands[*destination].index] != 0) continue;
               worklist.push_back(index);
               queued[index] = true;
          }

     bool anyRemoved{};
     while (!worklist.empty())
     {
          const auto index = worklist.back();
          worklist.pop_back();
          if (removed[index] || !canRemove(index)) continue;

          const auto destination = DestinationOperandIndex(instructions[index]);
          if (!destination || useCounts[instructions[index].operands[*destination].index] != 0) continue;
          removed[index] = true;
          anyRemoved = true;

          ForEachSourceOperand(instructions[index],
                               [&](const VirtualRegister& source)
                               {
                                    auto& count = useCounts[source.index];
                                    if (count == 0) return;
                                    if (--count != 0) return;
                                    const auto definitions = definitionIndices.find(source.index);
                                    if (definitions == definitionIndices.end()) return;
                                    for (const auto definition : definitions->second)
                                         if (!queued[definition] && canRemove(definition))
                                         {
                                              worklist.push_back(definition);
                                              queued[definition] = true;
                                         }
                               });
     }

     if (anyRemoved)
     {
          std::vector<VirtualInstruction> live{};
          live.reserve(instructions.size());
          for (std::size_t index = 0; index < instructions.size(); index++)
               if (!removed[index]) live.push_back(std::move(instructions[index]));
          instructions = std::move(live);
     }

     useCounts = CountVirtualUses(instructions);
     for (auto& instruction : instructions)
          if (instruction.type == VirtualInstructionType::CallWithResult && instruction.operands.size() == 2 &&
              useCounts[instruction.operands[0].index] == 0)
          {
               instruction.operands.erase(instruction.operands.begin());
               instruction.type = VirtualInstructionType::Call;
          }
}
