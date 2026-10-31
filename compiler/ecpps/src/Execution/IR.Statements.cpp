#include "IR.h"
#include <RuntimeAssert.h>
#include <TypeSystem/TypeBase.h>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
#include "Constexpr.h"
#include "Context.h"
#include "Expressions.h"
#include "HighLevel.h"
#include "NodeBase.h"
#include "Operations.h"
#include "Parsing/AST.h"
#include "Shared/BumpAllocator.h"
#include "Shared/Diagnostics.h"
#include "Shared/Error.h"

using IRNodePointer = ecpps::ir::NodePointer;
using ASTNodePointer = ecpps::ast::NodePointer;
using ecpps::Expression;

void ecpps::ir::IR::ParseIfStatement(const ast::IfStatementNode& node)
{
     auto condition = ParseCondition(node.Condition());
     if (condition == nullptr) return;

     const auto* conditionReg = LowerExpressionLoaded(std::move(condition), this->_built);
     if (conditionReg == nullptr) return;

     const auto thenLabel = MakeLabelName("if.then");
     const auto endLabel = MakeLabelName("if.end");
     const auto& elseLabel = node.ElseBody().empty() ? endLabel : MakeLabelName("if.else");
     const auto& falseTarget = elseLabel;

     EmitBranch(conditionReg, thenLabel, falseTarget, node.Source());

     EmitLabel(thenLabel, node.Source());
     for (const auto& statement : node.Body()) ParseNode(statement);
     EmitJump(endLabel, node.Source());
     if (elseLabel != endLabel)
     {
          EmitLabel(elseLabel, node.Source());
          for (const auto& statement : node.ElseBody()) ParseNode(statement);
     }

     EmitLabel(endLabel, node.Source());
}

bool ecpps::ir::IR::EmitConditionalBranch(const ast::NodePointer& conditionNode, const std::string& trueLabel,
                                          const std::string& falseLabel, const Location& source)
{
     if (conditionNode == nullptr)
     {
          EmitJump(trueLabel, source);
          return true;
     }

     auto condition = ParseCondition(conditionNode);
     if (condition == nullptr) return false;

     const auto* conditionReg = LowerExpressionLoaded(std::move(condition), this->_built);
     if (conditionReg == nullptr) return false;

     EmitBranch(conditionReg, trueLabel, falseLabel, source);
     return true;
}

void ecpps::ir::IR::ParseWhileStatement(const ast::WhileStatementNode& node)
{
     const auto bodyLabel = MakeLabelName("while.body");
     const auto continueLabel = MakeLabelName("while.cont");
     const auto endLabel = MakeLabelName("while.end");

     if (!EmitConditionalBranch(node.Condition(), bodyLabel, endLabel, node.Source())) return;

     EmitLabel(bodyLabel, node.Source());
     {
          const JumpScope jumpScope{*this, endLabel, continueLabel};
          for (const auto& statement : node.Body()) ParseNode(statement);
     }

     EmitLabel(continueLabel, node.Source());
     if (!EmitConditionalBranch(node.Condition(), bodyLabel, endLabel, node.Source())) return;

     EmitLabel(endLabel, node.Source());
}

void ecpps::ir::IR::ParseForStatement(const ast::ForStatementNode& node)
{
     const auto bodyLabel = MakeLabelName("for.body");
     const auto continueLabel = MakeLabelName("for.cont");
     const auto endLabel = MakeLabelName("for.end");

     ParseNode(node.InitStatement());

     if (!EmitConditionalBranch(node.Condition(), bodyLabel, endLabel, node.Source())) return;

     EmitLabel(bodyLabel, node.Source());
     {
          const JumpScope jumpScope{*this, endLabel, continueLabel};
          for (const auto& statement : node.Body()) ParseNode(statement);
          ParseNode(node.Expression());
     }

     EmitLabel(continueLabel, node.Source());
     if (!EmitConditionalBranch(node.Condition(), bodyLabel, endLabel, node.Source())) return;

     EmitLabel(endLabel, node.Source());
}

void ecpps::ir::IR::ParseDoWhileStatement(const ast::DoWhileStatementNode& node)
{
     const auto bodyLabel = MakeLabelName("do.body");
     const auto continueLabel = MakeLabelName("do.cont");
     const auto endLabel = MakeLabelName("do.end");

     EmitLabel(bodyLabel, node.Source());
     {
          const JumpScope jumpScope{*this, endLabel, continueLabel};
          for (const auto& statement : node.Body()) ParseNode(statement);
     }

     EmitLabel(continueLabel, node.Source());
     if (!EmitConditionalBranch(node.Condition(), bodyLabel, endLabel, node.Source())) return;

     EmitLabel(endLabel, node.Source());
}

void ecpps::ir::IR::ParseLabelNode(const ast::LabelNode& node)
{
     auto& allocator = *this->GetContext().nodeAllocator;

     this->_built.push_back(
          std::unique_ptr<SSALabelNode, IRDeleter>{new (allocator) SSALabelNode(node.Name(), node.Source())});
}

void ecpps::ir::IR::ParseGotoNode(const ast::GotoNode& node)
{
     auto& allocator = *this->GetContext().nodeAllocator;

     this->_built.push_back(
          std::unique_ptr<SSAGotoNode, IRDeleter>{new (allocator) SSAGotoNode(node.Name(), node.Source())});
}

void ecpps::ir::IR::ParseBreakNode(const ast::BreakNode& node)
{
     if (this->_jumpStack.empty())
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                    "A 'break' statement may only appear inside a loop or a switch statement", node.Source()));
          return;
     }

     EmitJump(this->_jumpStack.back().breakLabel, node.Source());
}

void ecpps::ir::IR::ParseContinueNode(const ast::ContinueNode& node)
{
     for (const auto& targets : this->_jumpStack | std::views::reverse)
     {
          if (!targets.continueLabel.has_value()) continue;

          EmitJump(*targets.continueLabel, node.Source());
          return;
     }

     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
               "A 'continue' statement may only appear inside a loop", node.Source()));
}

Expression ecpps::ir::IR::ParseCondition(const ast::NodePointer& conditionNode)
{
     auto condition = ParseExpression(conditionNode);
     if (condition == nullptr) return nullptr;

     if (dynamic_cast<const high::CompareNode*>(condition->Value().get()) != nullptr) return condition;

     if (!IsScalar(condition->Type()))
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Cannot contextually convert " + condition->Type()->Name() + " to bool",
                    condition->Value()->Source()));
          return nullptr;
     }

     const auto source = condition->Value()->Source();
     return MakeNonZeroTest(std::move(condition), source);
}

std::string ecpps::ir::IR::MakeLabelName(const std::string_view hint)
{
     auto* const function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(function != nullptr, "Labels can only be created inside a function");
     if (function == nullptr) throw TracedException("Invalid function");

     return std::format(".L{}.{}.{}", function->GetScope<FunctionScope>().Name().value_or("__unknown"), hint,
                        this->_nextLabelIndex++);
}

void ecpps::ir::IR::EmitLabel(std::string name, const Location& source)
{
     auto& allocator = *this->GetContext().nodeAllocator;
     this->_built.push_back(
          std::unique_ptr<SSALabelNode, IRDeleter>{new (allocator) SSALabelNode(std::move(name), source)});
}

void ecpps::ir::IR::EmitJump(std::string target, const Location& source)
{
     auto& allocator = *this->GetContext().nodeAllocator;
     this->_built.push_back(
          std::unique_ptr<SSAGotoNode, IRDeleter>{new (allocator) SSAGotoNode(std::move(target), source)});
}

void ecpps::ir::IR::EmitBranch(const SingleAssignRegisterNode* condition, std::string trueLabel, std::string falseLabel,
                               const Location& source)
{
     auto& allocator = *this->GetContext().nodeAllocator;
     this->_built.push_back(std::unique_ptr<SSABranchNode, IRDeleter>{
          new (allocator) SSABranchNode(condition, std::move(trueLabel), std::move(falseLabel), source)});
}
