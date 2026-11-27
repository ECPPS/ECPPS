#define CATCH_CONFIG_MAIN
#include <IRTestHelpers.h>
#include <Optimiser/passes/deadCode.h>
#include <Optimiser/passes/inline.h>
#include <catch_amalgamated.hpp>
#include <algorithm>

using namespace irTestHelpers;

namespace
{
     template <typename TNode, typename... TArgs>
     ecpps::ir::NodePointer MakeIRNode(ecpps::BumpAllocator& allocator, TArgs&&... args)
     {
          return ecpps::ir::NodePointer{new (allocator) TNode(std::forward<TArgs>(args)...), ecpps::ir::IRDeleter{}};
     }

     [[nodiscard]] ecpps::ir::SSAPointer MakeSSA(
          ecpps::BumpAllocator& allocator, const std::size_t index,
          const ecpps::ir::RegisterClass regClass = ecpps::ir::RegisterClass::Temporary)
     {
          return ecpps::ir::SSAPointer{new (allocator) ecpps::ir::SingleAssignRegisterNode(
               index, ecpps::ir::RegisterPriorityInfo{.regClass = regClass}, 8, 8, Source)};
     }

     [[nodiscard]] std::vector<ecpps::ir::NodePointer> MakeProcedure(ecpps::BumpAllocator& allocator,
                                                                     ecpps::ir::FunctionScope& scope,
                                                                     std::vector<ecpps::ir::NodePointer> body)
     {
          auto locals = std::make_shared<std::vector<ecpps::ir::FunctionScope::LocalEntity>>();
          std::vector<ecpps::ir::NodePointer> nodes{};
          nodes.push_back(
               MakeIRNode<ecpps::ir::ProcedureNode>(allocator, &scope, std::move(locals), std::move(body), Source));
          return nodes;
     }

     template <typename TNode> std::size_t CountNodes(const std::vector<ecpps::ir::NodePointer>& nodes)
     {
          return static_cast<std::size_t>(std::ranges::count_if(nodes,
                                                                [](const ecpps::ir::NodePointer& node)
                                                                {
                                                                     return dynamic_cast<const TNode*>(node.get()) !=
                                                                            nullptr;
                                                                }));
     }

     bool HasLabel(const std::vector<ecpps::ir::NodePointer>& nodes, const std::string& name)
     {
          return std::ranges::any_of(nodes,
                                     [&name](const ecpps::ir::NodePointer& node)
                                     {
                                          const auto* label = dynamic_cast<const ecpps::ir::SSALabelNode*>(node.get());
                                          return label != nullptr && label->Name() == name;
                                     });
     }

     template <typename TOperationNode> void VerifyCompoundAssignment(const ecpps::ast::Operator operator_)
     {
          Fixture fixture{};
          auto assignment = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
               fixture.astAllocator, fixture.MakeIdentifier("value"), operator_, fixture.MakeIntegerLiteral(3), Source);
          ecpps::SBOVector<ecpps::ast::NodePointer> body{};
          body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(assignment), Source));

          std::vector<ecpps::ast::NodePointer> ast{};
          ast.push_back(fixture.MakeFunctionDefinition(
               fixture.MakeFunctionSignature("int", "compound", {{"int", "value"}}), std::move(body)));
          auto nodes = fixture.Parse(ast);

          const auto* procedure = FindProcedure(nodes, "compound");
          REQUIRE((procedure != nullptr));
          if (procedure == nullptr) return;

          const auto* operation = FindNode<TOperationNode>(procedure->Body());
          REQUIRE((operation != nullptr));
          if (operation == nullptr) return;

          const auto* load = FindNode<ecpps::ir::SSALoadNode>(procedure->Body());
          REQUIRE((load != nullptr));
          if (load == nullptr) return;
          const auto* immediate = FindNode<ecpps::ir::SSAImmNode>(procedure->Body());
          REQUIRE((immediate != nullptr));
          if (immediate == nullptr) return;
          REQUIRE((&operation->Left() == &load->Result()));
          REQUIRE((&operation->Right() == &immediate->Result()));

          const auto* store = FindNode<ecpps::ir::SSAStoreNode>(procedure->Body());
          REQUIRE((store != nullptr));
          if (store == nullptr) return;
          REQUIRE((&store->Src() == &operation->Result()));

          const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
          REQUIRE((returnNode != nullptr));
          if (returnNode == nullptr) return;
          REQUIRE(returnNode->HasOperand());
          REQUIRE((returnNode->Operand() == &operation->Result()));
          REQUIRE(fixture.diagnostics.diagnosticsList.empty());
     }
} // namespace

TEST_CASE("IR - If/else lowers a signed comparison to branch targets", "[unit][ir][control-flow]")
{
     Fixture fixture{};
     auto condition = MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("lhs"),
                                                                  ecpps::ast::Operator::Less,
                                                                  fixture.MakeIdentifier("rhs"), Source);

     std::vector<ecpps::ast::NodePointer> thenBody{};
     thenBody.push_back(
          MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIntegerLiteral(1), Source));
     std::vector<ecpps::ast::NodePointer> elseBody{};
     elseBody.push_back(
          MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIntegerLiteral(0), Source));
     auto ifStatement = MakeASTNode<ecpps::ast::IfStatementNode>(fixture.astAllocator, std::move(condition),
                                                                 std::move(thenBody), std::move(elseBody), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(std::move(ifStatement));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(
          fixture.MakeFunctionSignature("int", "choose", {{"int", "lhs"}, {"int", "rhs"}}), std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "choose");
     REQUIRE((procedure != nullptr));
     const auto* compare = FindNode<ecpps::ir::SSACompareNode>(procedure->Body());
     REQUIRE((compare != nullptr));
     REQUIRE((compare->Predicate() == ecpps::ir::ComparisonPredicate::SignedLess));

     const auto* branch = FindNode<ecpps::ir::SSABranchNode>(procedure->Body());
     REQUIRE((branch != nullptr));
     REQUIRE((&branch->Condition() == &compare->Result()));
     REQUIRE((branch->TrueLabel() != branch->FalseLabel()));
     REQUIRE(HasLabel(procedure->Body(), branch->TrueLabel()));
     REQUIRE(HasLabel(procedure->Body(), branch->FalseLabel()));
     REQUIRE((CountNodes<ecpps::ir::SSAReturnNode>(procedure->Body()) == 2));
     REQUIRE((CountNodes<ecpps::ir::SSAUnreachableNode>(procedure->Body()) == 0));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Missing non-void return warns and adds an unreachable node", "[unit][ir][control-flow]")
{
     Fixture fixture{};
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "missing_return", {}), std::move(body)));
     const auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "missing_return");
     REQUIRE((procedure != nullptr));
     REQUIRE((CountNodes<ecpps::ir::SSAUnreachableNode>(procedure->Body()) == 1));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 1));
     const auto& diagnostic = *fixture.diagnostics.diagnosticsList.front();
     CHECK((diagnostic.Level() == ecpps::diagnostics::DiagnosticsLevel::Warning));
     CHECK((diagnostic.Message().contains("missing_return")));
     CHECK((diagnostic.Message().contains("without returning a value")));
}

TEST_CASE("IR - Assignment stores and returns the assigned value", "[unit][ir][assignment]")
{
     Fixture fixture{};
     auto assignment = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("value"), ecpps::ast::Operator::Assignment,
          fixture.MakeIntegerLiteral(9), Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(assignment), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "update", {{"int", "value"}}),
                                                  std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "update");
     REQUIRE((procedure != nullptr));
     const auto* parameter = FindNode<ecpps::ir::ParameterNode>(procedure->Body());
     REQUIRE((parameter != nullptr));
     const auto* immediate = FindNode<ecpps::ir::SSAImmNode>(procedure->Body());
     REQUIRE((immediate != nullptr));
     REQUIRE((immediate->Value() == 9));
     const auto* store = FindNode<ecpps::ir::SSAStoreNode>(procedure->Body());
     REQUIRE((store != nullptr));
     REQUIRE((&store->Target() == parameter->Result()));
     REQUIRE((&store->Src() == &immediate->Result()));

     const auto* load = FindNode<ecpps::ir::SSALoadNode>(procedure->Body());
     REQUIRE((load != nullptr));
     REQUIRE((&load->Address() == parameter->Result()));

     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
     REQUIRE((returnNode != nullptr));
     REQUIRE(returnNode->HasOperand());
     REQUIRE((returnNode->Operand() == &load->Result()));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Post-increment stores the increment but returns the previous value", "[unit][ir][increment]")
{
     Fixture fixture{};
     auto increment = MakeASTNode<ecpps::ast::UnaryOperatorNode>(fixture.astAllocator, ecpps::ast::Operator::Increment,
                                                                 fixture.MakeIdentifier("value"),
                                                                 ecpps::ast::UnaryOperatorType::Postfix, Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(increment), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "advance", {{"int", "value"}}),
                                                  std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "advance");
     REQUIRE((procedure != nullptr));
     const auto* parameter = FindNode<ecpps::ir::ParameterNode>(procedure->Body());
     REQUIRE((parameter != nullptr));

     const auto* load = FindNode<ecpps::ir::SSALoadNode>(procedure->Body());
     REQUIRE((load != nullptr));
     REQUIRE((&load->Address() == parameter->Result()));

     const auto* one = FindNode<ecpps::ir::SSAImmNode>(procedure->Body());
     REQUIRE((one != nullptr));
     REQUIRE((one->Value() == 1));

     const auto* addition = FindNode<ecpps::ir::SSAAddNode>(procedure->Body());
     REQUIRE((addition != nullptr));
     REQUIRE((&addition->Left() == &load->Result()));
     REQUIRE((&addition->Right() == &one->Result()));

     const auto* store = FindNode<ecpps::ir::SSAStoreNode>(procedure->Body());
     REQUIRE((store != nullptr));
     REQUIRE((&store->Target() == parameter->Result()));
     REQUIRE((&store->Src() == &addition->Result()));

     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
     REQUIRE((returnNode != nullptr));
     REQUIRE(returnNode->HasOperand());
     REQUIRE((returnNode->Operand() == &load->Result()));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Post-increment rejects const operands", "[unit][ir][increment]")
{
     Fixture fixture{};
     auto increment = MakeASTNode<ecpps::ast::UnaryOperatorNode>(fixture.astAllocator, ecpps::ast::Operator::Increment,
                                                                 fixture.MakeIdentifier("value"),
                                                                 ecpps::ast::UnaryOperatorType::Postfix, Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(increment), Source));

     auto signature = fixture.MakeFunctionSignature("int", "invalid", {{"int", "value"}});
     auto* const parameterType =
          dynamic_cast<ecpps::ast::BasicType*>(signature.parameters.parameters.front().type.get());
     REQUIRE((parameterType != nullptr));
     parameterType->SetConst(true);

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeASTNode<ecpps::ast::FunctionDefinitionNode>(fixture.astAllocator, std::move(signature),
                                                                   std::move(body), Source));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "invalid");
     REQUIRE((procedure != nullptr));
     REQUIRE((CountNodes<ecpps::ir::SSAAddNode>(procedure->Body()) == 0));
     REQUIRE((CountNodes<ecpps::ir::SSAStoreNode>(procedure->Body()) == 0));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "modifiable lvalue"));
}

TEST_CASE("IR - Post-decrement stores the decrement but returns the previous value", "[unit][ir][increment]")
{
     Fixture fixture{};
     auto decrement = MakeASTNode<ecpps::ast::UnaryOperatorNode>(fixture.astAllocator, ecpps::ast::Operator::Decrement,
                                                                 fixture.MakeIdentifier("value"),
                                                                 ecpps::ast::UnaryOperatorType::Postfix, Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(decrement), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "retreat", {{"int", "value"}}),
                                                  std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "retreat");
     REQUIRE((procedure != nullptr));
     const auto* parameter = FindNode<ecpps::ir::ParameterNode>(procedure->Body());
     REQUIRE((parameter != nullptr));

     const auto* load = FindNode<ecpps::ir::SSALoadNode>(procedure->Body());
     REQUIRE((load != nullptr));
     REQUIRE((&load->Address() == parameter->Result()));

     const auto* one = FindNode<ecpps::ir::SSAImmNode>(procedure->Body());
     REQUIRE((one != nullptr));
     REQUIRE((one->Value() == 1));

     const auto* subtraction = FindNode<ecpps::ir::SSASubNode>(procedure->Body());
     REQUIRE((subtraction != nullptr));
     REQUIRE((&subtraction->Left() == &load->Result()));
     REQUIRE((&subtraction->Right() == &one->Result()));

     const auto* store = FindNode<ecpps::ir::SSAStoreNode>(procedure->Body());
     REQUIRE((store != nullptr));
     REQUIRE((&store->Target() == parameter->Result()));
     REQUIRE((&store->Src() == &subtraction->Result()));

     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
     REQUIRE((returnNode != nullptr));
     REQUIRE(returnNode->HasOperand());
     REQUIRE((returnNode->Operand() == &load->Result()));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Prefix decrement stores and returns the updated value", "[unit][ir][increment]")
{
     Fixture fixture{};
     auto decrement = MakeASTNode<ecpps::ast::UnaryOperatorNode>(fixture.astAllocator, ecpps::ast::Operator::Decrement,
                                                                 fixture.MakeIdentifier("value"),
                                                                 ecpps::ast::UnaryOperatorType::Prefix, Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(decrement), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "retreat", {{"int", "value"}}),
                                                  std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "retreat");
     REQUIRE((procedure != nullptr));
     const auto* parameter = FindNode<ecpps::ir::ParameterNode>(procedure->Body());
     REQUIRE((parameter != nullptr));

     const auto* load = FindNode<ecpps::ir::SSALoadNode>(procedure->Body());
     REQUIRE((load != nullptr));
     REQUIRE((&load->Address() == parameter->Result()));

     const auto* one = FindNode<ecpps::ir::SSAImmNode>(procedure->Body());
     REQUIRE((one != nullptr));
     REQUIRE((one->Value() == 1));

     const auto* subtraction = FindNode<ecpps::ir::SSASubNode>(procedure->Body());
     REQUIRE((subtraction != nullptr));
     REQUIRE((&subtraction->Left() == &load->Result()));
     REQUIRE((&subtraction->Right() == &one->Result()));

     const auto* store = FindNode<ecpps::ir::SSAStoreNode>(procedure->Body());
     REQUIRE((store != nullptr));
     REQUIRE((&store->Target() == parameter->Result()));
     REQUIRE((&store->Src() == &subtraction->Result()));

     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
     REQUIRE((returnNode != nullptr));
     REQUIRE(returnNode->HasOperand());
     REQUIRE((returnNode->Operand() == &subtraction->Result()));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Prefix decrement rejects const operands", "[unit][ir][increment]")
{
     Fixture fixture{};
     auto decrement = MakeASTNode<ecpps::ast::UnaryOperatorNode>(fixture.astAllocator, ecpps::ast::Operator::Decrement,
                                                                 fixture.MakeIdentifier("value"),
                                                                 ecpps::ast::UnaryOperatorType::Prefix, Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(decrement), Source));

     auto signature = fixture.MakeFunctionSignature("int", "invalid", {{"int", "value"}});
     auto* const parameterType =
          dynamic_cast<ecpps::ast::BasicType*>(signature.parameters.parameters.front().type.get());
     REQUIRE((parameterType != nullptr));
     parameterType->SetConst(true);

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeASTNode<ecpps::ast::FunctionDefinitionNode>(fixture.astAllocator, std::move(signature),
                                                                   std::move(body), Source));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "invalid");
     REQUIRE((procedure != nullptr));
     REQUIRE((CountNodes<ecpps::ir::SSASubNode>(procedure->Body()) == 0));
     REQUIRE((CountNodes<ecpps::ir::SSAStoreNode>(procedure->Body()) == 0));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "modifiable lvalue"));
}

TEST_CASE("IR - Prefix increment stores and returns the updated value", "[unit][ir][increment]")
{
     Fixture fixture{};
     auto increment = MakeASTNode<ecpps::ast::UnaryOperatorNode>(fixture.astAllocator, ecpps::ast::Operator::Increment,
                                                                 fixture.MakeIdentifier("value"),
                                                                 ecpps::ast::UnaryOperatorType::Prefix, Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(increment), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "advance", {{"int", "value"}}),
                                                  std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "advance");
     REQUIRE((procedure != nullptr));
     const auto* parameter = FindNode<ecpps::ir::ParameterNode>(procedure->Body());
     REQUIRE((parameter != nullptr));

     const auto* load = FindNode<ecpps::ir::SSALoadNode>(procedure->Body());
     REQUIRE((load != nullptr));
     REQUIRE((&load->Address() == parameter->Result()));

     const auto* one = FindNode<ecpps::ir::SSAImmNode>(procedure->Body());
     REQUIRE((one != nullptr));
     REQUIRE((one->Value() == 1));

     const auto* addition = FindNode<ecpps::ir::SSAAddNode>(procedure->Body());
     REQUIRE((addition != nullptr));
     REQUIRE((&addition->Left() == &load->Result()));
     REQUIRE((&addition->Right() == &one->Result()));

     const auto* store = FindNode<ecpps::ir::SSAStoreNode>(procedure->Body());
     REQUIRE((store != nullptr));
     REQUIRE((&store->Target() == parameter->Result()));
     REQUIRE((&store->Src() == &addition->Result()));

     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
     REQUIRE((returnNode != nullptr));
     REQUIRE(returnNode->HasOperand());
     REQUIRE((returnNode->Operand() == &addition->Result()));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Prefix increment rejects const operands", "[unit][ir][increment]")
{
     Fixture fixture{};
     auto increment = MakeASTNode<ecpps::ast::UnaryOperatorNode>(fixture.astAllocator, ecpps::ast::Operator::Increment,
                                                                 fixture.MakeIdentifier("value"),
                                                                 ecpps::ast::UnaryOperatorType::Prefix, Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(increment), Source));

     auto signature = fixture.MakeFunctionSignature("int", "invalid", {{"int", "value"}});
     auto* const parameterType =
          dynamic_cast<ecpps::ast::BasicType*>(signature.parameters.parameters.front().type.get());
     REQUIRE((parameterType != nullptr));
     parameterType->SetConst(true);

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeASTNode<ecpps::ast::FunctionDefinitionNode>(fixture.astAllocator, std::move(signature),
                                                                   std::move(body), Source));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "invalid");
     REQUIRE((procedure != nullptr));
     REQUIRE((CountNodes<ecpps::ir::SSAAddNode>(procedure->Body()) == 0));
     REQUIRE((CountNodes<ecpps::ir::SSAStoreNode>(procedure->Body()) == 0));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "modifiable lvalue"));
}

TEST_CASE("IR - Subtraction assignment stores and returns the updated value", "[unit][ir][assignment]")
{
     Fixture fixture{};
     auto assignment = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("value"), ecpps::ast::Operator::MinusAssign,
          fixture.MakeIntegerLiteral(3), Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(assignment), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "reduce", {{"int", "value"}}),
                                                  std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "reduce");
     REQUIRE((procedure != nullptr));
     const auto* parameter = FindNode<ecpps::ir::ParameterNode>(procedure->Body());
     REQUIRE((parameter != nullptr));

     const auto* load = FindNode<ecpps::ir::SSALoadNode>(procedure->Body());
     REQUIRE((load != nullptr));
     REQUIRE((&load->Address() == parameter->Result()));

     const auto* three = FindNode<ecpps::ir::SSAImmNode>(procedure->Body());
     REQUIRE((three != nullptr));
     REQUIRE((three->Value() == 3));

     const auto* subtraction = FindNode<ecpps::ir::SSASubNode>(procedure->Body());
     REQUIRE((subtraction != nullptr));
     REQUIRE((&subtraction->Left() == &load->Result()));
     REQUIRE((&subtraction->Right() == &three->Result()));

     const auto* store = FindNode<ecpps::ir::SSAStoreNode>(procedure->Body());
     REQUIRE((store != nullptr));
     REQUIRE((&store->Target() == parameter->Result()));
     REQUIRE((&store->Src() == &subtraction->Result()));

     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
     REQUIRE((returnNode != nullptr));
     REQUIRE(returnNode->HasOperand());
     REQUIRE((returnNode->Operand() == &subtraction->Result()));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Addition assignment stores and returns the updated value", "[unit][ir][assignment]")
{
     Fixture fixture{};
     auto assignment = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("value"), ecpps::ast::Operator::PlusAssign,
          fixture.MakeIntegerLiteral(3), Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(assignment), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "increase", {{"int", "value"}}),
                                                  std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "increase");
     REQUIRE((procedure != nullptr));
     const auto* parameter = FindNode<ecpps::ir::ParameterNode>(procedure->Body());
     REQUIRE((parameter != nullptr));

     const auto* load = FindNode<ecpps::ir::SSALoadNode>(procedure->Body());
     REQUIRE((load != nullptr));
     REQUIRE((&load->Address() == parameter->Result()));

     const auto* three = FindNode<ecpps::ir::SSAImmNode>(procedure->Body());
     REQUIRE((three != nullptr));
     REQUIRE((three->Value() == 3));

     const auto* addition = FindNode<ecpps::ir::SSAAddNode>(procedure->Body());
     REQUIRE((addition != nullptr));
     REQUIRE((&addition->Left() == &load->Result()));
     REQUIRE((&addition->Right() == &three->Result()));

     const auto* store = FindNode<ecpps::ir::SSAStoreNode>(procedure->Body());
     REQUIRE((store != nullptr));
     REQUIRE((&store->Target() == parameter->Result()));
     REQUIRE((&store->Src() == &addition->Result()));

     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
     REQUIRE((returnNode != nullptr));
     REQUIRE(returnNode->HasOperand());
     REQUIRE((returnNode->Operand() == &addition->Result()));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Arithmetic and bitwise compound assignments lower", "[unit][ir][assignment]")
{
     VerifyCompoundAssignment<ecpps::ir::SSAAddNode>(ecpps::ast::Operator::PlusAssign);
     VerifyCompoundAssignment<ecpps::ir::SSASubNode>(ecpps::ast::Operator::MinusAssign);
     VerifyCompoundAssignment<ecpps::ir::SSAMulNode>(ecpps::ast::Operator::MultiplyAssign);
     VerifyCompoundAssignment<ecpps::ir::SSADivNode>(ecpps::ast::Operator::DivideAssign);
     VerifyCompoundAssignment<ecpps::ir::SSAModNode>(ecpps::ast::Operator::ModuloAssign);
     VerifyCompoundAssignment<ecpps::ir::SSABinXorNode>(ecpps::ast::Operator::XorAssign);
     VerifyCompoundAssignment<ecpps::ir::SSABinAndNode>(ecpps::ast::Operator::AndAssign);
     VerifyCompoundAssignment<ecpps::ir::SSABinOrNode>(ecpps::ast::Operator::OrAssign);
     VerifyCompoundAssignment<ecpps::ir::SSALeftShiftNode>(ecpps::ast::Operator::LeftShiftAssign);
     VerifyCompoundAssignment<ecpps::ir::SSARightShiftNode>(ecpps::ast::Operator::RightShiftAssign);
}

TEST_CASE("IR - Compound assignment rejects rvalue operands", "[unit][ir][assignment]")
{
     Fixture fixture{};
     auto sum = MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("value"),
                                                            ecpps::ast::Operator::Plus, fixture.MakeIntegerLiteral(1),
                                                            Source);
     auto assignment = MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, std::move(sum),
                                                                   ecpps::ast::Operator::PlusAssign,
                                                                   fixture.MakeIntegerLiteral(3), Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(assignment), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "invalid", {{"int", "value"}}),
                                                  std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "invalid");
     REQUIRE((procedure != nullptr));
     REQUIRE((CountNodes<ecpps::ir::SSAStoreNode>(procedure->Body()) == 0));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "A modifiable lvalue is required for compound assignment"));
}

TEST_CASE("IR - Compound assignment rejects const operands", "[unit][ir][assignment]")
{
     Fixture fixture{};
     auto assignment = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("value"), ecpps::ast::Operator::PlusAssign,
          fixture.MakeIntegerLiteral(3), Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(assignment), Source));

     auto signature = fixture.MakeFunctionSignature("int", "invalid", {{"int", "value"}});
     auto* const parameterType =
          dynamic_cast<ecpps::ast::BasicType*>(signature.parameters.parameters.front().type.get());
     REQUIRE((parameterType != nullptr));
     parameterType->SetConst(true);

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeASTNode<ecpps::ast::FunctionDefinitionNode>(fixture.astAllocator, std::move(signature),
                                                                   std::move(body), Source));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "invalid");
     REQUIRE((procedure != nullptr));
     REQUIRE((CountNodes<ecpps::ir::SSAStoreNode>(procedure->Body()) == 0));
     REQUIRE((CountNodes<ecpps::ir::SSAAddNode>(procedure->Body()) == 0));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "Assignment expressions require modifiable lvalues"));
}

TEST_CASE("IR - Compound assignment supports indirect targets", "[unit][ir][assignment]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));

     Fixture fixture{};
     auto dereference = MakeASTNode<ecpps::ast::UnaryOperatorNode>(fixture.astAllocator, ecpps::ast::Operator::Asterisk,
                                                                   fixture.MakeIdentifier("value"),
                                                                   ecpps::ast::UnaryOperatorType::Prefix, Source);
     auto assignment = MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, std::move(dereference),
                                                                   ecpps::ast::Operator::PlusAssign,
                                                                   fixture.MakeIntegerLiteral(3), Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(assignment), Source));

     auto signature = fixture.MakeFunctionSignature("int", "update", {{"int", "value"}});
     signature.parameters.parameters.front().type =
          MakeASTNode<ecpps::ast::PointerType>(fixture.astAllocator, fixture.MakeBasicType("int"), Source);

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeASTNode<ecpps::ast::FunctionDefinitionNode>(fixture.astAllocator, std::move(signature),
                                                                   std::move(body), Source));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "update");
     REQUIRE((procedure != nullptr));

     const auto* parameter = FindNode<ecpps::ir::ParameterNode>(procedure->Body());
     REQUIRE((parameter != nullptr));
     if (parameter == nullptr) return;

     REQUIRE((CountNodes<ecpps::ir::SSALoadNode>(procedure->Body()) == 1));
     const auto* pointerLoad = FindNode<ecpps::ir::SSALoadNode>(procedure->Body());
     REQUIRE((pointerLoad != nullptr));
     if (pointerLoad == nullptr) return;

     REQUIRE((&pointerLoad->Address() == parameter->Result()));
     const auto* addition = FindNode<ecpps::ir::SSAAddNode>(procedure->Body());
     REQUIRE((addition != nullptr));
     if (addition == nullptr) return;

     const auto* immediate = FindNode<ecpps::ir::SSAImmNode>(procedure->Body());
     REQUIRE((immediate != nullptr));
     if (immediate == nullptr) return;

     REQUIRE((&addition->Right() == &immediate->Result()));

     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());

     REQUIRE((returnNode != nullptr));
     if (returnNode == nullptr) return;
     REQUIRE(returnNode->HasOperand());
     REQUIRE((returnNode->Operand() == &addition->Result()));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - While loop emits entry and repeat condition branches", "[unit][ir][control-flow]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> loopBody{};
     auto loop = MakeASTNode<ecpps::ast::WhileStatementNode>(fixture.astAllocator, fixture.MakeIdentifier("condition"),
                                                             std::move(loopBody), Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(std::move(loop));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(
          fixture.MakeFunctionSignature("void", "spin", {{"bool", "condition"}}), std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "spin");
     REQUIRE((procedure != nullptr));
     REQUIRE((CountNodes<ecpps::ir::SSABranchNode>(procedure->Body()) == 2));

     const auto* firstBranch = FindNode<ecpps::ir::SSABranchNode>(procedure->Body());
     REQUIRE((firstBranch != nullptr));
     REQUIRE((firstBranch->TrueLabel() != firstBranch->FalseLabel()));
     REQUIRE(HasLabel(procedure->Body(), firstBranch->TrueLabel()));
     REQUIRE(HasLabel(procedure->Body(), firstBranch->FalseLabel()));

     const ecpps::ir::SSABranchNode* secondBranch = nullptr;
     bool foundFirstBranch = false;
     for (const auto& node : procedure->Body())
          if (const auto* branch = dynamic_cast<const ecpps::ir::SSABranchNode*>(node.get()); branch != nullptr)
          {
               if (!foundFirstBranch) foundFirstBranch = true;
               else
                    secondBranch = branch;
          }
     REQUIRE((secondBranch != nullptr));
     REQUIRE((secondBranch->TrueLabel() == firstBranch->TrueLabel()));
     REQUIRE((secondBranch->FalseLabel() == firstBranch->FalseLabel()));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Break jumps to the enclosing loop exit", "[unit][ir][control-flow]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> loopBody{};
     loopBody.push_back(MakeASTNode<ecpps::ast::BreakNode>(fixture.astAllocator, Source));
     auto loop = MakeASTNode<ecpps::ast::WhileStatementNode>(fixture.astAllocator, fixture.MakeIdentifier("condition"),
                                                             std::move(loopBody), Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(std::move(loop));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(
          fixture.MakeFunctionSignature("void", "stop", {{"bool", "condition"}}), std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "stop");
     REQUIRE((procedure != nullptr));
     const auto* branch = FindNode<ecpps::ir::SSABranchNode>(procedure->Body());
     REQUIRE((branch != nullptr));
     const auto* jump = FindNode<ecpps::ir::SSAGotoNode>(procedure->Body());
     REQUIRE((jump != nullptr));
     REQUIRE((jump->Name() == branch->FalseLabel()));
     REQUIRE(HasLabel(procedure->Body(), jump->Name()));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Continue jumps to the loop continuation", "[unit][ir][control-flow]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> loopBody{};
     loopBody.push_back(MakeASTNode<ecpps::ast::ContinueNode>(fixture.astAllocator, Source));
     auto loop = MakeASTNode<ecpps::ast::WhileStatementNode>(fixture.astAllocator, fixture.MakeIdentifier("condition"),
                                                             std::move(loopBody), Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(std::move(loop));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(
          fixture.MakeFunctionSignature("void", "repeat", {{"bool", "condition"}}), std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "repeat");
     REQUIRE((procedure != nullptr));
     const auto* jump = FindNode<ecpps::ir::SSAGotoNode>(procedure->Body());
     REQUIRE((jump != nullptr));

     std::size_t jumpIndex = procedure->Body().size();
     std::size_t continuationLabelIndex = procedure->Body().size();
     std::size_t followingBranchIndex = procedure->Body().size();
     for (std::size_t index = 0; index < procedure->Body().size(); index++)
     {
          const auto& node = procedure->Body()[index];
          if (dynamic_cast<const ecpps::ir::SSAGotoNode*>(node.get()) == jump) jumpIndex = index;
          if (const auto* label = dynamic_cast<const ecpps::ir::SSALabelNode*>(node.get());
              label != nullptr && label->Name() == jump->Name())
               continuationLabelIndex = index;
          if (continuationLabelIndex < index && dynamic_cast<const ecpps::ir::SSABranchNode*>(node.get()) != nullptr)
               followingBranchIndex = index;
     }

     REQUIRE((jumpIndex < continuationLabelIndex));
     REQUIRE((continuationLabelIndex < followingBranchIndex));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("DCE removes unused pure register chains", "[unit][optimizer][dce]")
{
     Fixture fixture{};
     ecpps::ir::FunctionScope scope{};
     std::vector<ecpps::ir::NodePointer> body{};

     auto first = MakeSSA(fixture.irAllocator, 0);
     const auto* firstRegister = first.get();
     body.push_back(MakeIRNode<ecpps::ir::SSAImmNode>(fixture.irAllocator, std::move(first), std::uint64_t{7}, Source));

     auto second = MakeSSA(fixture.irAllocator, 1);
     const auto* secondRegister = second.get();
     body.push_back(MakeIRNode<ecpps::ir::SSAAddNode>(fixture.irAllocator, std::move(second), firstRegister,
                                                      firstRegister, Source));

     auto third = MakeSSA(fixture.irAllocator, 2);
     body.push_back(MakeIRNode<ecpps::ir::SSAAddNode>(fixture.irAllocator, std::move(third), secondRegister,
                                                      secondRegister, Source));

     auto nodes =
          ecpps::optimisations::DeadCodePass{}.Execute(MakeProcedure(fixture.irAllocator, scope, std::move(body)));

     auto* procedure = dynamic_cast<ecpps::ir::ProcedureNode*>(nodes.front().get());
     REQUIRE((procedure != nullptr));
     CHECK((procedure->Body().empty()));
}

TEST_CASE("DCE removes unused parameter storage after its last load", "[unit][optimizer][dce]")
{
     Fixture fixture{};
     ecpps::ir::FunctionScope scope{};
     std::vector<ecpps::ir::NodePointer> body{};
     auto parameter = MakeSSA(fixture.irAllocator, 0, ecpps::ir::RegisterClass::LocalVariable);
     const auto* parameterRegister = parameter.get();
     body.push_back(MakeIRNode<ecpps::ir::AllocationNode>(fixture.irAllocator, std::move(parameter), Source));
     body.push_back(MakeIRNode<ecpps::ir::ParameterNode>(fixture.irAllocator, &scope, std::uint64_t{0},
                                                         parameterRegister, Source));
     ecpps::ir::MemoryAccess access{};
     access.alignment = 8;
     body.push_back(MakeIRNode<ecpps::ir::SSALoadNode>(fixture.irAllocator, MakeSSA(fixture.irAllocator, 1),
                                                       parameterRegister, access, Source));
     auto unusedParameter = MakeSSA(fixture.irAllocator, 2, ecpps::ir::RegisterClass::LocalVariable);
     const auto* unusedParameterRegister = unusedParameter.get();
     body.push_back(MakeIRNode<ecpps::ir::AllocationNode>(fixture.irAllocator, std::move(unusedParameter), Source));
     body.push_back(MakeIRNode<ecpps::ir::ParameterNode>(fixture.irAllocator, &scope, std::uint64_t{1},
                                                         unusedParameterRegister, Source));

     auto nodes =
          ecpps::optimisations::DeadCodePass{}.Execute(MakeProcedure(fixture.irAllocator, scope, std::move(body)));

     const auto* procedure = dynamic_cast<const ecpps::ir::ProcedureNode*>(nodes.front().get());
     REQUIRE((procedure != nullptr));
     CHECK((procedure->Body().empty()));
}

TEST_CASE("DCE removes unused plain loads and retains volatile and atomic loads", "[unit][optimizer][dce]")
{
     Fixture fixture{};
     ecpps::ir::FunctionScope scope{};
     std::vector<ecpps::ir::NodePointer> body{};

     auto address = MakeSSA(fixture.irAllocator, 0, ecpps::ir::RegisterClass::LocalVariable);
     const auto* addressRegister = address.get();
     body.push_back(MakeIRNode<ecpps::ir::AllocationNode>(fixture.irAllocator, std::move(address), Source));

     ecpps::ir::MemoryAccess plain{};
     plain.alignment = 8;
     body.push_back(MakeIRNode<ecpps::ir::SSALoadNode>(fixture.irAllocator, MakeSSA(fixture.irAllocator, 1),
                                                       addressRegister, plain, Source));

     auto volatileAccess = plain;
     volatileAccess.isVolatile = true;
     body.push_back(MakeIRNode<ecpps::ir::SSALoadNode>(fixture.irAllocator, MakeSSA(fixture.irAllocator, 2),
                                                       addressRegister, volatileAccess, Source));

     auto atomicAccess = plain;
     atomicAccess.atomicOrdering = ecpps::ir::MemoryOrdering::Relaxed;
     body.push_back(MakeIRNode<ecpps::ir::SSALoadNode>(fixture.irAllocator, MakeSSA(fixture.irAllocator, 3),
                                                       addressRegister, atomicAccess, Source));

     auto otherAddressSpace = plain;
     otherAddressSpace.addressSpace = 1;
     body.push_back(MakeIRNode<ecpps::ir::SSALoadNode>(fixture.irAllocator, MakeSSA(fixture.irAllocator, 4),
                                                       addressRegister, otherAddressSpace, Source));

     auto nodes =
          ecpps::optimisations::DeadCodePass{}.Execute(MakeProcedure(fixture.irAllocator, scope, std::move(body)));

     auto* procedure = dynamic_cast<ecpps::ir::ProcedureNode*>(nodes.front().get());
     REQUIRE((procedure != nullptr));
     REQUIRE((CountNodes<ecpps::ir::SSALoadNode>(procedure->Body()) == 3));
     CHECK((FindNode<ecpps::ir::SSALoadNode>(procedure->Body())->Access().isVolatile));
     CHECK((std::ranges::any_of(procedure->Body(),
                                [](const ecpps::ir::NodePointer& node)
                                {
                                     const auto* load = dynamic_cast<const ecpps::ir::SSALoadNode*>(node.get());
                                     return load != nullptr && load->Access().atomicOrdering.has_value();
                                })));
     CHECK((std::ranges::any_of(procedure->Body(),
                                [](const ecpps::ir::NodePointer& node)
                                {
                                     const auto* load = dynamic_cast<const ecpps::ir::SSALoadNode*>(node.get());
                                     return load != nullptr && load->Access().addressSpace == 1;
                                })));
}

TEST_CASE("DCE keeps an unused effectful call but discards its result register", "[unit][optimizer][dce]")
{
     Fixture fixture{};
     ecpps::ir::FunctionScope caller{};
     ecpps::ir::FunctionScope callee{};
     std::vector<ecpps::ir::NodePointer> body{};

     body.push_back(MakeIRNode<ecpps::ir::SSACallNode>(
          fixture.irAllocator, std::optional<ecpps::ir::SSAPointer>{MakeSSA(fixture.irAllocator, 0)}, &callee,
          std::vector<const ecpps::ir::SingleAssignRegisterNode*>{}, Source));

     auto nodes =
          ecpps::optimisations::DeadCodePass{}.Execute(MakeProcedure(fixture.irAllocator, caller, std::move(body)));

     auto* procedure = dynamic_cast<ecpps::ir::ProcedureNode*>(nodes.front().get());
     REQUIRE((procedure != nullptr));
     const auto* call = FindNode<ecpps::ir::SSACallNode>(procedure->Body());
     REQUIRE((call != nullptr));
     CHECK_FALSE(call->HasResult());
}

TEST_CASE("Inlining replaces a direct call to a local function", "[unit][optimizer][inline]")
{
     Fixture fixture{};
     auto calleeBody = ecpps::SBOVector<ecpps::ast::NodePointer>{};
     auto modulo = MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("value"),
                                                               ecpps::ast::Operator::Percent,
                                                               fixture.MakeIntegerLiteral(4), Source);
     calleeBody.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(modulo), Source));

     auto callArguments = ecpps::SBOVector<ecpps::ast::NodePointer>{};
     callArguments.Push(fixture.MakeIntegerLiteral(7));
     auto call = MakeASTNode<ecpps::ast::CallOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("callee"),
                                                           std::move(callArguments), Source);
     auto callerBody = ecpps::SBOVector<ecpps::ast::NodePointer>{};
     callerBody.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(call), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "callee", {{"int", "value"}}),
                                                  std::move(calleeBody)));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "caller", {}), std::move(callerBody)));
     auto nodes = fixture.Parse(ast);

     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
     const auto* caller = FindProcedure(nodes, "caller");
     REQUIRE((caller != nullptr));
     REQUIRE((FindCall(*caller) != nullptr));

     nodes = ecpps::optimisations::InlinePass{fixture.irAllocator}.Execute(std::move(nodes));

     caller = FindProcedure(nodes, "caller");
     REQUIRE((caller != nullptr));
     CHECK((FindCall(*caller) == nullptr));
     CHECK((CountNodes<ecpps::ir::SSAReturnNode>(caller->Body()) == 1));
     CHECK((CountNodes<ecpps::ir::SSAModNode>(caller->Body()) == 1));
}

TEST_CASE("Inlining preserves volatile integer stores", "[unit][optimizer][inline][dce]")
{
     Fixture fixture{};
     ecpps::ir::FunctionScope callerScope{};
     ecpps::ir::FunctionScope calleeScope{};
     auto storage = MakeSSA(fixture.irAllocator, 0, ecpps::ir::RegisterClass::LocalVariable);
     const auto* storageRegister = storage.get();
     const ecpps::ir::MemoryAccess volatileAccess{.isVolatile = true, .alignment = 4};
     std::vector<ecpps::ir::NodePointer> calleeBody{};
     calleeBody.push_back(MakeIRNode<ecpps::ir::AllocationNode>(fixture.irAllocator, std::move(storage), Source));
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSAStoreIntegerNode>(fixture.irAllocator, storageRegister, 1uz,
                                                                     volatileAccess, Source));
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, nullptr, Source));

     std::vector<ecpps::ir::NodePointer> callerBody{};
     callerBody.push_back(MakeIRNode<ecpps::ir::SSACallNode>(fixture.irAllocator, std::nullopt, &calleeScope,
                                                             std::vector<const ecpps::ir::SingleAssignRegisterNode*>{},
                                                             Source));
     callerBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, nullptr, Source));
     auto nodes = MakeProcedure(fixture.irAllocator, calleeScope, std::move(calleeBody));
     auto caller = MakeProcedure(fixture.irAllocator, callerScope, std::move(callerBody));
     nodes.push_back(std::move(caller.front()));

     nodes = ecpps::optimisations::InlinePass{fixture.irAllocator}.Execute(std::move(nodes));

     const auto* inlinedCaller = dynamic_cast<const ecpps::ir::ProcedureNode*>(nodes.back().get());
     REQUIRE((inlinedCaller != nullptr));
     const auto* store = FindNode<ecpps::ir::SSAStoreIntegerNode>(inlinedCaller->Body());
     REQUIRE((store != nullptr));
     CHECK((store->Access().isVolatile));
}

TEST_CASE("Inlining routes multiple returns to the call continuation", "[unit][optimizer][inline]")
{
     Fixture fixture{};
     ecpps::ir::FunctionScope callerScope{};
     ecpps::ir::FunctionScope calleeScope{};
     std::vector<ecpps::ir::NodePointer> calleeBody{};
     auto value = MakeSSA(fixture.irAllocator, 0);
     const auto* valueRegister = value.get();
     calleeBody.push_back(
          MakeIRNode<ecpps::ir::SSAImmNode>(fixture.irAllocator, std::move(value), std::uint64_t{1}, Source));
     auto condition = MakeSSA(fixture.irAllocator, 1);
     const auto* conditionRegister = condition.get();
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSACompareNode>(fixture.irAllocator, std::move(condition),
                                                                valueRegister, valueRegister,
                                                                ecpps::ir::ComparisonPredicate::NotEqual, Source));
     calleeBody.push_back(
          MakeIRNode<ecpps::ir::SSABranchNode>(fixture.irAllocator, conditionRegister, "left", "right", Source));
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSALabelNode>(fixture.irAllocator, "left", Source));
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, conditionRegister, Source));
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSALabelNode>(fixture.irAllocator, "right", Source));
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, conditionRegister, Source));

     std::vector<ecpps::ir::NodePointer> callerBody{};
     auto callResult = MakeSSA(fixture.irAllocator, 0);
     const auto* callResultRegister = callResult.get();
     callerBody.push_back(MakeIRNode<ecpps::ir::SSACallNode>(
          fixture.irAllocator, std::optional<ecpps::ir::SSAPointer>{std::move(callResult)}, &calleeScope,
          std::vector<const ecpps::ir::SingleAssignRegisterNode*>{}, Source));
     callerBody.push_back(MakeIRNode<ecpps::ir::SSALabelNode>(fixture.irAllocator, "left", Source));
     callerBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, callResultRegister, Source));

     std::vector<ecpps::ir::NodePointer> fillerBody{};
     fillerBody.reserve(41);
     for (std::size_t index = 0; index < 41; index++)
          fillerBody.push_back(MakeIRNode<ecpps::ir::SSAImmNode>(
               fixture.irAllocator, MakeSSA(fixture.irAllocator, index), static_cast<std::uint64_t>(index), Source));
     fillerBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, nullptr, Source));

     auto calleeNodes = MakeProcedure(fixture.irAllocator, calleeScope, std::move(calleeBody));
     auto callerNodes = MakeProcedure(fixture.irAllocator, callerScope, std::move(callerBody));
     ecpps::ir::FunctionScope fillerScope{};
     auto fillerNodes = MakeProcedure(fixture.irAllocator, fillerScope, std::move(fillerBody));
     std::vector<ecpps::ir::NodePointer> nodes{};
     nodes.push_back(std::move(calleeNodes.front()));
     nodes.push_back(std::move(callerNodes.front()));
     nodes.push_back(std::move(fillerNodes.front()));

     nodes = ecpps::optimisations::InlinePass{fixture.irAllocator}.Execute(std::move(nodes));

     const auto* caller = dynamic_cast<const ecpps::ir::ProcedureNode*>(nodes[1].get());
     REQUIRE((caller != nullptr));
     CHECK((FindCall(*caller) == nullptr));
     CHECK((CountNodes<ecpps::ir::SSAGotoNode>(caller->Body()) == 2));
     CHECK((CountNodes<ecpps::ir::SSAStoreNode>(caller->Body()) == 2));
     CHECK((CountNodes<ecpps::ir::SSALoadNode>(caller->Body()) == 1));
     CHECK((CountNodes<ecpps::ir::SSALabelNode>(caller->Body()) == 4));
     const auto* branch = FindNode<ecpps::ir::SSABranchNode>(caller->Body());
     REQUIRE((branch != nullptr));
     CHECK((branch->TrueLabel() != "left"));
     CHECK((branch->FalseLabel() != "right"));
     CHECK((branch->TrueLabel() != branch->FalseLabel()));
     const auto* firstReturnJump = FindNode<ecpps::ir::SSAGotoNode>(caller->Body());
     REQUIRE((firstReturnJump != nullptr));
     CHECK((std::ranges::all_of(caller->Body(),
                                [firstReturnJump](const ecpps::ir::NodePointer& node)
                                {
                                     const auto* jump = dynamic_cast<const ecpps::ir::SSAGotoNode*>(node.get());
                                     return jump == nullptr || jump->Name() == firstReturnJump->Name();
                                })));
}

TEST_CASE("Inlining remaps registers inside array decay nodes", "[unit][optimizer][inline]")
{
     Fixture fixture{};
     ecpps::ir::FunctionScope callerScope{};
     ecpps::ir::FunctionScope calleeScope{};
     ecpps::ir::FunctionScope sinkScope{};
     sinkScope.isExtern = true;
     std::vector<ecpps::ir::NodePointer> calleeBody{};
     auto array = MakeSSA(fixture.irAllocator, 0, ecpps::ir::RegisterClass::LocalVariable);
     const auto* arrayRegister = array.get();
     calleeBody.push_back(MakeIRNode<ecpps::ir::AllocationNode>(fixture.irAllocator, std::move(array), Source));

     auto reference = MakeIRNode<ecpps::ir::SSARegisterReferenceNode>(fixture.irAllocator, arrayRegister, Source);
     ecpps::Expression arrayExpression = std::make_unique<ecpps::LValue>(nullptr, std::move(reference), false);
     auto decay = MakeIRNode<ecpps::ir::LoadArrayDecayNode>(fixture.irAllocator, std::move(arrayExpression), Source);
     auto converted = MakeSSA(fixture.irAllocator, 1);
     const auto* convertedRegister = converted.get();
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSAPointerConvertFromDecayNode>(
          fixture.irAllocator, std::move(converted), std::move(decay), nullptr, Source));
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSACallNode>(
          fixture.irAllocator, std::nullopt, &sinkScope,
          std::vector<const ecpps::ir::SingleAssignRegisterNode*>{convertedRegister}, Source));
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, nullptr, Source));

     std::vector<ecpps::ir::NodePointer> callerBody{};
     callerBody.push_back(MakeIRNode<ecpps::ir::SSACallNode>(fixture.irAllocator, std::nullopt, &calleeScope,
                                                             std::vector<const ecpps::ir::SingleAssignRegisterNode*>{},
                                                             Source));
     callerBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, nullptr, Source));
     auto calleeNodes = MakeProcedure(fixture.irAllocator, calleeScope, std::move(calleeBody));
     auto callerNodes = MakeProcedure(fixture.irAllocator, callerScope, std::move(callerBody));
     std::vector<ecpps::ir::NodePointer> nodes{};
     nodes.push_back(std::move(calleeNodes.front()));
     nodes.push_back(std::move(callerNodes.front()));

     nodes = ecpps::optimisations::InlinePass{fixture.irAllocator}.Execute(std::move(nodes));

     const auto* caller = dynamic_cast<const ecpps::ir::ProcedureNode*>(nodes.back().get());
     REQUIRE((caller != nullptr));
     const auto* nestedCall = FindCall(*caller);
     REQUIRE((nestedCall != nullptr));
     CHECK((&nestedCall->Function() == &sinkScope));
     const auto* conversion = FindNode<ecpps::ir::SSAPointerConvertFromDecayNode>(caller->Body());
     REQUIRE((conversion != nullptr));
     const auto* loadDecay = dynamic_cast<const ecpps::ir::LoadArrayDecayNode*>(&conversion->DecayNode());
     REQUIRE((loadDecay != nullptr));
     const auto* allocation = FindNode<ecpps::ir::AllocationNode>(caller->Body());
     REQUIRE((allocation != nullptr));
     CHECK((loadDecay->GetAllocReg() == &allocation->Node()));
     CHECK((loadDecay->GetAllocReg() != arrayRegister));
}

TEST_CASE("Inlining clones integer array stores and temporary decay", "[unit][optimizer][inline]")
{
     Fixture fixture{};
     ecpps::ir::FunctionScope callerScope{};
     ecpps::ir::FunctionScope calleeScope{};
     ecpps::ir::FunctionScope sinkScope{};
     sinkScope.isExtern = true;
     std::vector<ecpps::ir::NodePointer> calleeBody{};
     auto array = MakeSSA(fixture.irAllocator, 0, ecpps::ir::RegisterClass::LocalVariable);
     const auto* arrayRegister = array.get();
     calleeBody.push_back(MakeIRNode<ecpps::ir::AllocationNode>(fixture.irAllocator, std::move(array), Source));
     auto storedArray = MakeIRNode<ecpps::ir::IntegerArrayNode>(fixture.irAllocator, std::vector<std::uint32_t>{1, 2},
                                                                nullptr, Source);
     const auto* storedArrayPointer = storedArray.get();
     calleeBody.push_back(
          MakeIRNode<ecpps::ir::SSAArrayStoreNode>(fixture.irAllocator, arrayRegister, std::move(storedArray), Source));

     auto temporaryArray = MakeIRNode<ecpps::ir::IntegerArrayNode>(fixture.irAllocator,
                                                                   std::vector<std::uint32_t>{3, 4}, nullptr, Source);
     const auto* temporaryArrayPointer = temporaryArray.get();
     ecpps::Expression temporaryExpression = std::make_unique<ecpps::PRValue>(nullptr, std::move(temporaryArray), true);
     auto temporaryDecay = MakeIRNode<ecpps::ir::TemporaryIntegerArrayDecayNode>(
          fixture.irAllocator, std::move(temporaryExpression), Source);
     auto converted = MakeSSA(fixture.irAllocator, 1);
     const auto* convertedRegister = converted.get();
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSAPointerConvertFromDecayNode>(
          fixture.irAllocator, std::move(converted), std::move(temporaryDecay), nullptr, Source));
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSACallNode>(
          fixture.irAllocator, std::nullopt, &sinkScope,
          std::vector<const ecpps::ir::SingleAssignRegisterNode*>{convertedRegister}, Source));
     calleeBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, nullptr, Source));

     std::vector<ecpps::ir::NodePointer> callerBody{};
     callerBody.push_back(MakeIRNode<ecpps::ir::SSACallNode>(fixture.irAllocator, std::nullopt, &calleeScope,
                                                             std::vector<const ecpps::ir::SingleAssignRegisterNode*>{},
                                                             Source));
     callerBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, nullptr, Source));
     auto calleeNodes = MakeProcedure(fixture.irAllocator, calleeScope, std::move(calleeBody));
     auto callerNodes = MakeProcedure(fixture.irAllocator, callerScope, std::move(callerBody));
     std::vector<ecpps::ir::NodePointer> nodes{};
     nodes.push_back(std::move(calleeNodes.front()));
     nodes.push_back(std::move(callerNodes.front()));

     nodes = ecpps::optimisations::InlinePass{fixture.irAllocator}.Execute(std::move(nodes));

     const auto* caller = dynamic_cast<const ecpps::ir::ProcedureNode*>(nodes.back().get());
     REQUIRE((caller != nullptr));
     const auto* allocation = FindNode<ecpps::ir::AllocationNode>(caller->Body());
     REQUIRE((allocation != nullptr));
     const auto* arrayStore = FindNode<ecpps::ir::SSAArrayStoreNode>(caller->Body());
     REQUIRE((arrayStore != nullptr));
     CHECK((&arrayStore->Target() == &allocation->Node()));
     CHECK((&arrayStore->ArrayNode() != storedArrayPointer));
     const auto* conversion = FindNode<ecpps::ir::SSAPointerConvertFromDecayNode>(caller->Body());
     REQUIRE((conversion != nullptr));
     const auto* decay = dynamic_cast<const ecpps::ir::TemporaryIntegerArrayDecayNode*>(&conversion->DecayNode());
     REQUIRE((decay != nullptr));
     CHECK((decay->Values() == std::vector<std::uint32_t>{3, 4}));
     CHECK((decay->GetOperand()->Value().get() != temporaryArrayPointer));
}

TEST_CASE("Inlining supports a member function's implicit object argument", "[unit][optimizer][inline]")
{
     Fixture fixture{};
     ecpps::typeSystem::ClassInfo classInfo{};
     ecpps::ir::FunctionScope callerScope{};
     ecpps::ir::FunctionScope memberScope{};
     memberScope.memberClassInfo = &classInfo;
     std::vector<ecpps::ir::NodePointer> memberBody{};
     auto thisSlot = MakeSSA(fixture.irAllocator, 0, ecpps::ir::RegisterClass::LocalVariable);
     const auto* thisSlotRegister = thisSlot.get();
     memberBody.push_back(MakeIRNode<ecpps::ir::AllocationNode>(fixture.irAllocator, std::move(thisSlot), Source));
     memberBody.push_back(MakeIRNode<ecpps::ir::ParameterNode>(fixture.irAllocator, &memberScope, std::uint64_t{0},
                                                               thisSlotRegister, Source));
     auto loadedThis = MakeSSA(fixture.irAllocator, 1);
     ecpps::ir::MemoryAccess access{};
     access.alignment = 8;
     memberBody.push_back(MakeIRNode<ecpps::ir::SSALoadNode>(fixture.irAllocator, std::move(loadedThis),
                                                             thisSlotRegister, access, Source));
     memberBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, nullptr, Source));

     std::vector<ecpps::ir::NodePointer> callerBody{};
     auto thisArgument = MakeSSA(fixture.irAllocator, 0);
     const auto* thisArgumentRegister = thisArgument.get();
     callerBody.push_back(
          MakeIRNode<ecpps::ir::SSAImmNode>(fixture.irAllocator, std::move(thisArgument), std::uint64_t{0}, Source));
     callerBody.push_back(MakeIRNode<ecpps::ir::SSACallNode>(
          fixture.irAllocator, std::nullopt, &memberScope,
          std::vector<const ecpps::ir::SingleAssignRegisterNode*>{thisArgumentRegister}, Source));
     callerBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, nullptr, Source));

     auto memberNodes = MakeProcedure(fixture.irAllocator, memberScope, std::move(memberBody));
     auto callerNodes = MakeProcedure(fixture.irAllocator, callerScope, std::move(callerBody));
     std::vector<ecpps::ir::NodePointer> nodes{};
     nodes.push_back(std::move(memberNodes.front()));
     nodes.push_back(std::move(callerNodes.front()));

     nodes = ecpps::optimisations::InlinePass{fixture.irAllocator}.Execute(std::move(nodes));

     const auto* caller = dynamic_cast<const ecpps::ir::ProcedureNode*>(nodes.back().get());
     REQUIRE((caller != nullptr));
     CHECK((FindCall(*caller) == nullptr));
     const auto* allocation = FindNode<ecpps::ir::AllocationNode>(caller->Body());
     REQUIRE((allocation != nullptr));
     const auto* store = FindNode<ecpps::ir::SSAStoreNode>(caller->Body());
     REQUIRE((store != nullptr));
     CHECK((&store->Target() == &allocation->Node()));
     CHECK((&store->Src() == thisArgumentRegister));
}

TEST_CASE("Inlining ranks saved call overhead against IR growth", "[unit][optimizer][inline]")
{
     Fixture fixture{};
     ecpps::ir::FunctionScope callerScope{};
     ecpps::ir::FunctionScope smallCalleeScope{};
     ecpps::ir::FunctionScope expensiveCallScope{};

     std::vector<ecpps::ir::NodePointer> smallCalleeBody{};
     smallCalleeBody.reserve(8);
     for (std::size_t index = 0; index < 7; index++)
          smallCalleeBody.push_back(MakeIRNode<ecpps::ir::SSAImmNode>(
               fixture.irAllocator, MakeSSA(fixture.irAllocator, index), static_cast<std::uint64_t>(index), Source));
     smallCalleeBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, nullptr, Source));

     std::vector<ecpps::ir::NodePointer> expensiveCallBody{};
     const ecpps::ir::SingleAssignRegisterNode* returned{};
     expensiveCallBody.reserve(7);
     for (std::size_t index = 0; index < 6; index++)
     {
          auto value = MakeSSA(fixture.irAllocator, index);
          returned = value.get();
          expensiveCallBody.push_back(MakeIRNode<ecpps::ir::SSAImmNode>(fixture.irAllocator, std::move(value),
                                                                        static_cast<std::uint64_t>(index), Source));
     }
     expensiveCallBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, returned, Source));

     std::vector<ecpps::ir::NodePointer> callerBody{};
     callerBody.push_back(MakeIRNode<ecpps::ir::SSACallNode>(fixture.irAllocator, std::nullopt, &smallCalleeScope,
                                                             std::vector<const ecpps::ir::SingleAssignRegisterNode*>{},
                                                             Source));
     auto callResult = MakeSSA(fixture.irAllocator, 0);
     const auto* callResultRegister = callResult.get();
     callerBody.push_back(MakeIRNode<ecpps::ir::SSACallNode>(
          fixture.irAllocator, std::optional<ecpps::ir::SSAPointer>{std::move(callResult)}, &expensiveCallScope,
          std::vector<const ecpps::ir::SingleAssignRegisterNode*>{}, Source));
     callerBody.push_back(MakeIRNode<ecpps::ir::SSAReturnNode>(fixture.irAllocator, callResultRegister, Source));

     auto smallCalleeNodes = MakeProcedure(fixture.irAllocator, smallCalleeScope, std::move(smallCalleeBody));
     auto expensiveCallNodes = MakeProcedure(fixture.irAllocator, expensiveCallScope, std::move(expensiveCallBody));
     auto callerNodes = MakeProcedure(fixture.irAllocator, callerScope, std::move(callerBody));
     std::vector<ecpps::ir::NodePointer> nodes{};
     nodes.push_back(std::move(smallCalleeNodes.front()));
     nodes.push_back(std::move(expensiveCallNodes.front()));
     nodes.push_back(std::move(callerNodes.front()));

     nodes = ecpps::optimisations::InlinePass{fixture.irAllocator}.Execute(std::move(nodes));

     const auto* caller = dynamic_cast<const ecpps::ir::ProcedureNode*>(nodes.back().get());
     REQUIRE((caller != nullptr));
     const auto* remainingCall = FindCall(*caller);
     REQUIRE((remainingCall != nullptr));
     CHECK((&remainingCall->Function() == &smallCalleeScope));
     CHECK((CountNodes<ecpps::ir::SSACallNode>(caller->Body()) == 1));
}
