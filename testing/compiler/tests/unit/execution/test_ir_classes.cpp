#define CATCH_CONFIG_MAIN
#include <IRTestHelpers.h>
#include <catch_amalgamated.hpp>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace IRTestHelpers;

namespace
{
     struct AggregateFieldStore
     {
          std::uint64_t value;
          std::uint64_t offset;
     };

     std::vector<AggregateFieldStore> GetAggregateFieldStores(const std::vector<ecpps::ir::NodePointer>& body)
     {
          std::vector<AggregateFieldStore> stores{};
          for (const auto& node : body)
          {
               const auto* store = dynamic_cast<const ecpps::ir::SSAIndirectStoreNode*>(node.get());
               if (store == nullptr) continue;

               std::optional<std::uint64_t> value{};
               std::optional<std::uint64_t> offset{};
               for (const auto& valueNode : body)
                    if (const auto* immediate = dynamic_cast<const ecpps::ir::SSAImmNode*>(valueNode.get());
                        immediate != nullptr && &immediate->Result() == &store->Src())
                         value = immediate->Value();

               for (const auto& addressNode : body)
                    if (const auto* address = dynamic_cast<const ecpps::ir::SSAAddNode*>(addressNode.get());
                        address != nullptr && &address->Result() == &store->Target())
                         for (const auto& offsetNode : body)
                              if (const auto* immediate = dynamic_cast<const ecpps::ir::SSAImmNode*>(offsetNode.get());
                                  immediate != nullptr && &address->Right() == &immediate->Result())
                                   offset = immediate->Value();

               if (value.has_value() && offset.has_value()) stores.push_back({*value, *offset});
          }
          return stores;
     }

     bool HasAggregateFieldStore(const std::vector<AggregateFieldStore>& stores, const std::uint64_t value,
                                 const std::uint64_t offset)
     {
          return std::ranges::any_of(stores,
                                     [value, offset](const AggregateFieldStore& store)
                                     {
                                          return store.value == value && store.offset == offset;
                                     });
     }

     void RequireSecondFieldIsReturned(const ecpps::ir::ProcedureNode& procedure)
     {
          const auto* fieldLoad = FindNode<ecpps::ir::SSADerefNode>(procedure.Body());
          const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure.Body());
          REQUIRE((fieldLoad != nullptr));
          REQUIRE((returnNode != nullptr));
          REQUIRE((returnNode->Operand() == &fieldLoad->Result()));
     }
} // namespace

TEST_CASE("IR - Class definition records field layout", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("PairForLayout", {{"int", "number"}, {"char", "marker"}}));

     (void)fixture.Parse(std::move(ast));
     const std::string className{"PairForLayout"};
     const auto& info = FindClassInfo(fixture, className);

     REQUIRE((info.state == ecpps::typeSystem::ClassState::Complete));
     REQUIRE((info.fields.size() == 2));
     REQUIRE((info.fields[0].name == "number"));
     REQUIRE((info.fields[0].offset == 0));
     REQUIRE((info.fields[1].name == "marker"));
     REQUIRE((info.fields[1].offset == 4));
     REQUIRE((info.size == 8));
     REQUIRE((info.alignment == 4));
     REQUIRE(info.triviallyCopyable);
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Aggregate initialization stores the provided field value", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("BoxForInitialization", {{"int", "value"}}));

     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(fixture.MakeIntegerLiteral(7));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("BoxForInitialization", "box", std::move(initialiser)));
     auto memberAccess = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("box"), ecpps::ast::Operator::FullStop,
          fixture.MakeIdentifier("value"), Source);
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(memberAccess), Source));
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "make", {}), std::move(body)));

     auto nodes = fixture.Parse(std::move(ast));
     const auto* procedure = FindProcedure(nodes, "make");
     REQUIRE((procedure != nullptr));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());

     const ecpps::ir::SSAImmNode* seven = nullptr;
     const ecpps::ir::SSAImmNode* zeroOffset = nullptr;
     const ecpps::ir::SSAIndirectStoreNode* store = nullptr;
     const ecpps::ir::SSAAddNode* fieldAddress = nullptr;
     for (const auto& node : procedure->Body())
     {
          if (const auto* immediate = dynamic_cast<const ecpps::ir::SSAImmNode*>(node.get()))
               if (immediate->Value() == 7) seven = immediate;
          if (const auto* indirectStore = dynamic_cast<const ecpps::ir::SSAIndirectStoreNode*>(node.get());
              indirectStore != nullptr && seven != nullptr && &indirectStore->Src() == &seven->Result())
               store = indirectStore;
     }
     for (const auto& node : procedure->Body())
          if (const auto* add = dynamic_cast<const ecpps::ir::SSAAddNode*>(node.get());
              add != nullptr && store != nullptr && &add->Result() == &store->Target())
          {
               fieldAddress = add;
               for (const auto& operandNode : procedure->Body())
                    if (const auto* immediate = dynamic_cast<const ecpps::ir::SSAImmNode*>(operandNode.get());
                        immediate != nullptr && immediate->Value() == 0 && &add->Right() == &immediate->Result())
                         zeroOffset = immediate;
          }

     REQUIRE((seven != nullptr));
     REQUIRE((zeroOffset != nullptr));
     REQUIRE((store != nullptr));
     REQUIRE((fieldAddress != nullptr));

     const auto* fieldLoad = FindNode<ecpps::ir::SSADerefNode>(procedure->Body());
     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
     REQUIRE((fieldLoad != nullptr));
     REQUIRE((returnNode != nullptr));
     REQUIRE((returnNode->Operand() == &fieldLoad->Result()));
}

TEST_CASE("IR - Positional aggregate initialization assigns each field in order", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(
          fixture.MakeStructDefinition("PairForPositionalInitialization", {{"int", "first"}, {"int", "second"}}));

     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(fixture.MakeIntegerLiteral(5));
     initialisers.push_back(fixture.MakeIntegerLiteral(9));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("PairForPositionalInitialization", "pair", std::move(initialiser)));
     auto memberAccess = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("pair"), ecpps::ast::Operator::FullStop,
          fixture.MakeIdentifier("second"), Source);
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(memberAccess), Source));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "makePair", {}), std::move(body)));

     auto nodes = fixture.Parse(std::move(ast));
     const auto* procedure = FindProcedure(nodes, "makePair");
     REQUIRE((procedure != nullptr));
     const auto stores = GetAggregateFieldStores(procedure->Body());
     REQUIRE((stores.size() == 2));
     REQUIRE(HasAggregateFieldStore(stores, 5, 0));
     REQUIRE(HasAggregateFieldStore(stores, 9, 4));
     RequireSecondFieldIsReturned(*procedure);
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Omitted aggregate fields receive zero initialization", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("PairWithOmittedField", {{"int", "first"}, {"int", "second"}}));

     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(fixture.MakeIntegerLiteral(5));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("PairWithOmittedField", "pair", std::move(initialiser)));
     auto memberAccess = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("pair"), ecpps::ast::Operator::FullStop,
          fixture.MakeIdentifier("second"), Source);
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(memberAccess), Source));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "secondOrZero", {}), std::move(body)));

     auto nodes = fixture.Parse(std::move(ast));
     const auto* procedure = FindProcedure(nodes, "secondOrZero");
     REQUIRE((procedure != nullptr));
     const auto stores = GetAggregateFieldStores(procedure->Body());
     REQUIRE((stores.size() == 2));
     REQUIRE(HasAggregateFieldStore(stores, 5, 0));
     REQUIRE(HasAggregateFieldStore(stores, 0, 4));
     RequireSecondFieldIsReturned(*procedure);
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Designated aggregate initialization targets the named field", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(
          fixture.MakeStructDefinition("PairForDesignatedInitialization", {{"int", "first"}, {"int", "second"}}));

     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(fixture.astAllocator, "second",
                                                                               fixture.MakeIntegerLiteral(9), Source));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("PairForDesignatedInitialization", "pair", std::move(initialiser)));
     auto memberAccess = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("pair"), ecpps::ast::Operator::FullStop,
          fixture.MakeIdentifier("second"), Source);
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(memberAccess), Source));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "readSecond", {}), std::move(body)));

     auto nodes = fixture.Parse(std::move(ast));
     const auto* procedure = FindProcedure(nodes, "readSecond");
     REQUIRE((procedure != nullptr));
     const auto stores = GetAggregateFieldStores(procedure->Body());
     REQUIRE((stores.size() == 2));
     REQUIRE(HasAggregateFieldStore(stores, 0, 0));
     REQUIRE(HasAggregateFieldStore(stores, 9, 4));
     RequireSecondFieldIsReturned(*procedure);
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Designated aggregate initializers must follow member order", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(
          fixture.MakeStructDefinition("PairWithOutOfOrderDesignators", {{"int", "first"}, {"int", "second"}}));

     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(fixture.astAllocator, "second",
                                                                               fixture.MakeIntegerLiteral(9), Source));
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(fixture.astAllocator, "first",
                                                                               fixture.MakeIntegerLiteral(5), Source));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("PairWithOutOfOrderDesignators", "pair", std::move(initialiser)));
     auto memberAccess = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("pair"), ecpps::ast::Operator::FullStop,
          fixture.MakeIdentifier("second"), Source);
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(memberAccess), Source));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "invalidOrder", {}), std::move(body)));

     const auto nodes = fixture.Parse(std::move(ast));

     REQUIRE((FindProcedure(nodes, "invalidOrder") != nullptr));
     REQUIRE(
          HasTypeErrorContaining(fixture.diagnostics, "Designated initialisers must follow member declaration order"));
}

TEST_CASE("IR - Unknown designated aggregate members are diagnosed", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("PairWithUnknownDesignator", {{"int", "first"}, {"int", "second"}}));

     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(fixture.astAllocator, "missing",
                                                                               fixture.MakeIntegerLiteral(7), Source));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("PairWithUnknownDesignator", "pair", std::move(initialiser)));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", "unknownField", {}), std::move(body)));

     (void)fixture.Parse(std::move(ast));

     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "'missing' is not a direct data member"));
}

TEST_CASE("IR - Duplicate designated aggregate members are diagnosed", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("PairWithDuplicateDesignator", {{"int", "first"}, {"int", "second"}}));

     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(fixture.astAllocator, "first",
                                                                               fixture.MakeIntegerLiteral(5), Source));
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(fixture.astAllocator, "first",
                                                                               fixture.MakeIntegerLiteral(9), Source));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("PairWithDuplicateDesignator", "pair", std::move(initialiser)));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", "duplicateField", {}), std::move(body)));

     (void)fixture.Parse(std::move(ast));

     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "Member 'first' is initialized more than once"));
}

TEST_CASE("IR - Positional and designated aggregate initializers cannot be mixed", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("PairWithMixedInitializers", {{"int", "first"}, {"int", "second"}}));

     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(fixture.MakeIntegerLiteral(5));
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(fixture.astAllocator, "second",
                                                                               fixture.MakeIntegerLiteral(9), Source));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("PairWithMixedInitializers", "pair", std::move(initialiser)));
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", "mixedInitializers", {}),
                                                  std::move(body)));

     (void)fixture.Parse(std::move(ast));

     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "Positional and designated initialisers cannot be mixed"));
}
