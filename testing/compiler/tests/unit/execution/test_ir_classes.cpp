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

               if (value.has_value() && offset.has_value()) stores.push_back({.value = *value, .offset = *offset});
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

     ecpps::ast::NodePointer MakeConstructorDeclaration(
          Fixture& fixture, const std::string& className, const ecpps::ast::FunctionBodyKind bodyKind,
          const std::vector<std::pair<std::string, std::string>>& parameters = {},
          std::vector<ecpps::ast::NodePointer> body = {})
     {
          ecpps::ast::ParametersAndQualifiers parameterList{};
          for (const auto& [type, name] : parameters)
          {
               ecpps::ast::FunctionParameter parameter{};
               parameter.type = fixture.MakeBasicType(type);
               parameter.name = fixture.MakeIdentifier(name);
               parameterList.parameters.push_back(std::move(parameter));
          }

          ecpps::ast::MemberDeclarator declarator{};
          declarator.declaredIdExpression = fixture.MakeIdentifier(className);
          declarator.suffixes.emplace_back(std::move(parameterList));
          declarator.functionBody = bodyKind;
          declarator.body = std::move(body);
          std::vector<ecpps::ast::MemberDeclarator> declarators{};
          declarators.push_back(std::move(declarator));
          return MakeASTNode<ecpps::ast::MemberDeclarationNode>(
               fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
               ecpps::ast::DeclSpecifierSequence{}, std::move(declarators), Source);
     }

     ecpps::ast::NodePointer MakeConstructorClass(Fixture& fixture, const std::string& className,
                                                  ecpps::ast::NodePointer constructor)
     {
          std::vector<ecpps::ast::NodePointer> members{};
          members.push_back(std::move(constructor));
          ecpps::ast::DeclSpecifierSequence fieldSpecifiers{};
          fieldSpecifiers.definingTypeSpecifier = fixture.MakeBasicType("int");
          ecpps::ast::MemberDeclarator fieldDeclarator{};
          fieldDeclarator.declaredIdExpression = fixture.MakeIdentifier("value");
          fieldDeclarator.initialisationType = ecpps::ast::InitialisationType::EqualInitialiser;
          fieldDeclarator.initialiser = fixture.MakeIntegerLiteral(3);
          std::vector<ecpps::ast::MemberDeclarator> fields{};
          fields.push_back(std::move(fieldDeclarator));
          members.push_back(MakeASTNode<ecpps::ast::MemberDeclarationNode>(
               fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
               std::move(fieldSpecifiers), std::move(fields), Source));
          return MakeASTNode<ecpps::ast::ClassDefNode>(fixture.astAllocator, className, ecpps::ast::ClassKey::Struct,
                                                       std::move(members), false, false, Source);
     }

     ecpps::ast::NodePointer MakeDefaultConstructionCaller(Fixture& fixture, const std::string& className,
                                                           const std::string& functionName,
                                                           std::vector<ecpps::ast::NodePointer> arguments = {})
     {
          ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
          auto initialiser =
               MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(arguments), Source);
          ecpps::SBOVector<ecpps::ast::NodePointer> body{};
          body.Push(fixture.MakeVariableDeclaration(className, "value", std::move(initialiser)));
          body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
          return fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", functionName, {}),
                                                std::move(body));
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

TEST_CASE("IR - Aggregate initialisation stores the provided field value", "[unit][ir][classes]")
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

TEST_CASE("IR - Positional aggregate initialisation assigns each field in order", "[unit][ir][classes]")
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

TEST_CASE("IR - Omitted aggregate fields receive zero initialisation", "[unit][ir][classes]")
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

TEST_CASE("IR - Designated aggregate initialisation targets the named field", "[unit][ir][classes]")
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

TEST_CASE("IR - Designated aggregate initialisers must follow member order", "[unit][ir][classes]")
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

     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "Member 'first' is initialised more than once"));
}

TEST_CASE("IR - Positional and designated aggregate initialisers cannot be mixed", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("PairWithMixedInitialisers", {{"int", "first"}, {"int", "second"}}));

     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(fixture.MakeIntegerLiteral(5));
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(fixture.astAllocator, "second",
                                                                               fixture.MakeIntegerLiteral(9), Source));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("PairWithMixedInitialisers", "pair", std::move(initialiser)));
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", "mixedInitialisers", {}),
                                                  std::move(body)));

     (void)fixture.Parse(std::move(ast));

     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "Positional and designated initialisers cannot be mixed"));
}

TEST_CASE("IR - In-class constructors emit and call regular procedures", "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     std::vector<ecpps::ast::NodePointer> constructorBody{};
     constructorBody.push_back(MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("value"), ecpps::ast::Operator::Assignment,
          fixture.MakeIdentifier("valueParam"), Source));
     ast.push_back(MakeConstructorClass(
          fixture, "InlineCtorForIR",
          MakeConstructorDeclaration(fixture, "InlineCtorForIR", ecpps::ast::FunctionBodyKind::Block,
                                     {{"int", "valueParam"}}, std::move(constructorBody))));
     std::vector<ecpps::ast::NodePointer> arguments{};
     arguments.push_back(fixture.MakeIntegerLiteral(10));
     ast.push_back(MakeDefaultConstructionCaller(fixture, "InlineCtorForIR", "callInlineCtor", std::move(arguments)));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* constructor = FindProcedure(nodes, "InlineCtorForIR");
     const auto* caller = FindProcedure(nodes, "callInlineCtor");
     REQUIRE((constructor != nullptr));
     REQUIRE((caller != nullptr));
     const auto* call = FindCall(*caller);
     REQUIRE((call != nullptr));
     CHECK(&call->Function() == constructor->Scope());
     CHECK(call->Arguments().size() == 2);
     CHECK(constructor->Scope()->parameters.size() == 1);
     CHECK(call->Function().memberClassInfo != nullptr);
     CHECK(FindNode<ecpps::ir::SSAIndirectStoreNode>(constructor->Body()) != nullptr);
     CHECK(FindNode<ecpps::ir::SSAIndirectStoreNode>(caller->Body()) == nullptr);
     CHECK(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Defaulted constructors emit regular procedures", "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeConstructorClass(
          fixture, "DefaultedCtorForIR",
          MakeConstructorDeclaration(fixture, "DefaultedCtorForIR", ecpps::ast::FunctionBodyKind::Defaulted)));
     ast.push_back(MakeDefaultConstructionCaller(fixture, "DefaultedCtorForIR", "callDefaultedCtor"));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* constructor = FindProcedure(nodes, "DefaultedCtorForIR");
     const auto* caller = FindProcedure(nodes, "callDefaultedCtor");
     REQUIRE((constructor != nullptr));
     REQUIRE((caller != nullptr));
     const auto* call = FindCall(*caller);
     REQUIRE((call != nullptr));
     CHECK(&call->Function() == constructor->Scope());
     CHECK(FindNode<ecpps::ir::SSAIndirectStoreNode>(constructor->Body()) != nullptr);
     CHECK(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Declaration-only constructors emit external calls", "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeConstructorClass(
          fixture, "ExternalCtorForIR",
          MakeConstructorDeclaration(fixture, "ExternalCtorForIR", ecpps::ast::FunctionBodyKind::None)));
     ast.push_back(MakeDefaultConstructionCaller(fixture, "ExternalCtorForIR", "callExternalCtor"));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* caller = FindProcedure(nodes, "callExternalCtor");
     REQUIRE((caller != nullptr));
     const auto* call = FindCall(*caller);
     REQUIRE((call != nullptr));
     CHECK(call->Function().memberClassInfo != nullptr);
     CHECK(FindProcedure(nodes, "ExternalCtorForIR") == nullptr);
     CHECK(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Out-of-class constructor definitions bind to declarations", "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeConstructorClass(fixture, "OutOfClassCtorForIR",
                                        MakeConstructorDeclaration(fixture, "OutOfClassCtorForIR",
                                                                   ecpps::ast::FunctionBodyKind::None,
                                                                   {{"int", "declaredValue"}})));

     std::vector<ecpps::ast::NodePointer> qualifiedPath{};
     qualifiedPath.push_back(fixture.MakeIdentifier("OutOfClassCtorForIR"));
     qualifiedPath.push_back(fixture.MakeIdentifier("OutOfClassCtorForIR"));
     auto constructorName =
          MakeASTNode<ecpps::ast::QualifiedIdNode>(fixture.astAllocator, std::move(qualifiedPath), Source);
     auto signature = fixture.MakeFunctionSignature("void", "temporary", {{"int", "bodyValue"}});
     signature.name = std::move(constructorName);
     ecpps::SBOVector<ecpps::ast::NodePointer> constructorBody{};
     constructorBody.Push(MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("value"), ecpps::ast::Operator::Assignment,
          fixture.MakeIdentifier("bodyValue"), Source));
     ast.push_back(fixture.MakeFunctionDefinition(std::move(signature), std::move(constructorBody)));
     std::vector<ecpps::ast::NodePointer> arguments{};
     arguments.push_back(fixture.MakeIntegerLiteral(8));
     ast.push_back(
          MakeDefaultConstructionCaller(fixture, "OutOfClassCtorForIR", "callOutOfClassCtor", std::move(arguments)));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* constructor = FindProcedure(nodes, "OutOfClassCtorForIR");
     const auto* caller = FindProcedure(nodes, "callOutOfClassCtor");
     REQUIRE((constructor != nullptr));
     REQUIRE((caller != nullptr));
     const auto* call = FindCall(*caller);
     REQUIRE((call != nullptr));
     CHECK(&call->Function() == constructor->Scope());
     CHECK(call->Arguments().size() == 2);
     CHECK(constructor->Scope()->parameters.size() == 1);
     CHECK(FindNode<ecpps::ir::SSAIndirectStoreNode>(constructor->Body()) != nullptr);
     CHECK(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Out-of-class defaulted constructors emit regular procedures", "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeConstructorClass(
          fixture, "OutOfClassDefaultedCtorForIR",
          MakeConstructorDeclaration(fixture, "OutOfClassDefaultedCtorForIR", ecpps::ast::FunctionBodyKind::None)));

     std::vector<ecpps::ast::NodePointer> qualifiedPath{};
     qualifiedPath.push_back(fixture.MakeIdentifier("OutOfClassDefaultedCtorForIR"));
     qualifiedPath.push_back(fixture.MakeIdentifier("OutOfClassDefaultedCtorForIR"));
     auto constructorName =
          MakeASTNode<ecpps::ast::QualifiedIdNode>(fixture.astAllocator, std::move(qualifiedPath), Source);
     auto signature = fixture.MakeFunctionSignature("void", "temporary", {});
     signature.name = std::move(constructorName);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     ast.push_back(MakeASTNode<ecpps::ast::FunctionDefinitionNode>(fixture.astAllocator, std::move(signature),
                                                                   std::move(body), Source,
                                                                   ecpps::ast::FunctionBodyKind::Defaulted));
     ast.push_back(
          MakeDefaultConstructionCaller(fixture, "OutOfClassDefaultedCtorForIR", "callOutOfClassDefaultedCtor"));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* constructor = FindProcedure(nodes, "OutOfClassDefaultedCtorForIR");
     const auto* caller = FindProcedure(nodes, "callOutOfClassDefaultedCtor");
     REQUIRE((constructor != nullptr));
     REQUIRE((caller != nullptr));
     const auto* call = FindCall(*caller);
     REQUIRE((call != nullptr));
     CHECK(&call->Function() == constructor->Scope());
     CHECK(FindNode<ecpps::ir::SSAIndirectStoreNode>(constructor->Body()) != nullptr);
     CHECK(fixture.diagnostics.diagnosticsList.empty());
}
