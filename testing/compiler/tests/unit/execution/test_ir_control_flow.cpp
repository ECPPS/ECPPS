#define CATCH_CONFIG_MAIN
#include <IRTestHelpers.h>
#include <catch_amalgamated.hpp>
#include <algorithm>

using namespace IRTestHelpers;

namespace
{
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
     auto nodes = fixture.Parse(std::move(ast));

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
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

     const auto* procedure = FindProcedure(nodes, "invalid");
     REQUIRE((procedure != nullptr));
     REQUIRE((CountNodes<ecpps::ir::SSAStoreNode>(procedure->Body()) == 0));
     REQUIRE((CountNodes<ecpps::ir::SSAAddNode>(procedure->Body()) == 0));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "Assignment expressions require modifiable lvalues"));
}

TEST_CASE("IR - Compound assignment diagnoses unsupported indirect targets", "[unit][ir][assignment]")
{
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
     auto nodes = fixture.Parse(std::move(ast));

     const auto* procedure = FindProcedure(nodes, "update");
     REQUIRE((procedure != nullptr));
     REQUIRE((CountNodes<ecpps::ir::SSAStoreNode>(procedure->Body()) == 0));
     REQUIRE((CountNodes<ecpps::ir::SSAAddNode>(procedure->Body()) == 0));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "Modifying an object through a pointer is not supported yet"));
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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
     auto nodes = fixture.Parse(std::move(ast));

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
