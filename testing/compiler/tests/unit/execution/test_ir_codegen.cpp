#define CATCH_CONFIG_MAIN
#include <IRTestHelpers.h>
#include <catch_amalgamated.hpp>

using namespace IRTestHelpers;

TEST_CASE("IR - Addition lowers to SSA and returns the result", "[unit][ir][codegen]")
{
     Fixture fixture{};
     auto addition = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("lhs"), ecpps::ast::Operator::Plus,
          fixture.MakeIdentifier("rhs"), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(addition), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(
          fixture.MakeFunctionSignature("int", "sum", {{"int", "lhs"}, {"int", "rhs"}}), std::move(body)));
     auto nodes = fixture.Parse(std::move(ast));

     const auto* procedure = FindProcedure(nodes, "sum");
     REQUIRE(procedure != nullptr);
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());

     const auto* lhsParameter = FindNode<ecpps::ir::ParameterNode>(procedure->Body());
     REQUIRE(lhsParameter != nullptr);
     const ecpps::ir::ParameterNode* rhsParameter = nullptr;
     for (const auto& node : procedure->Body())
          if (const auto* parameter = dynamic_cast<const ecpps::ir::ParameterNode*>(node.get());
              parameter != nullptr && parameter->Index() == 1)
               rhsParameter = parameter;
     REQUIRE(lhsParameter->Index() == 0);
     REQUIRE(rhsParameter != nullptr);

     const ecpps::ir::SSALoadNode* lhsLoad = nullptr;
     const ecpps::ir::SSALoadNode* rhsLoad = nullptr;
     for (const auto& node : procedure->Body())
          if (const auto* load = dynamic_cast<const ecpps::ir::SSALoadNode*>(node.get()))
          {
               if (&load->Address() == lhsParameter->Result()) lhsLoad = load;
               if (&load->Address() == rhsParameter->Result()) rhsLoad = load;
          }
     REQUIRE(lhsLoad != nullptr);
     REQUIRE(rhsLoad != nullptr);

     const auto* additionNode = FindNode<ecpps::ir::SSAAddNode>(procedure->Body());
     REQUIRE(additionNode != nullptr);
     REQUIRE(&additionNode->Left() == &lhsLoad->Result());
     REQUIRE(&additionNode->Right() == &rhsLoad->Result());

     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
     REQUIRE(returnNode != nullptr);
     REQUIRE(returnNode->Operand() == &additionNode->Result());
}
