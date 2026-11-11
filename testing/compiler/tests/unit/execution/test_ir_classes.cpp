#define CATCH_CONFIG_MAIN
#include <CodeGeneration/PseudoAssembly.h>
#include <IRTestHelpers.h>
#include <Machine/Encoders/Backends/x86_64/Platforms/Linux/Linux.h>
#include <Machine/Mangling.h>
#include <Parsing/ASTs/Type.h>
#include <TypeSystem/ArithmeticTypes.h>
#include <catch_amalgamated.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace IRTestHelpers;

TEST_CASE("Mangling encodes lvalue and rvalue reference types", "[unit][machine][mangling]")
{
     using ecpps::abi::CallingConventionName;
     using ecpps::abi::Linkage;
     using ecpps::abi::Mangling;
     using ecpps::typeSystem::IntegralType;
     using ecpps::typeSystem::Qualifiers;
     using ecpps::typeSystem::ReferenceType;
     using ecpps::typeSystem::Signedness;
     using ecpps::typeSystem::TypeKind;

     const IntegralType intType{Signedness::Signed, TypeKind::Int, "int", Qualifiers::None};
     const ReferenceType lvalueReference{&intType, ReferenceType::Kind::LValue, "int&"};
     const ReferenceType rvalueReference{&intType, ReferenceType::Kind::RValue, "int&&"};

     CHECK(Mangling::MangleType(&lvalueReference) == "Ri");
     CHECK(Mangling::MangleType(&rvalueReference) == "Oi");
     CHECK(Mangling::MangleName(Linkage::External, "take_lvalue", CallingConventionName::Microsoftx64, nullptr,
                                {&lvalueReference}, std::vector<std::string>{})
                .ends_with("$Ri?"));
     CHECK(Mangling::MangleName(Linkage::External, "take_rvalue", CallingConventionName::Microsoftx64, nullptr,
                                {&rvalueReference}, std::vector<std::string>{})
                .ends_with("$Oi?"));
}

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
               {
                    if (const auto* address = dynamic_cast<const ecpps::ir::SSAAddNode*>(addressNode.get());
                        address != nullptr && &address->Result() == &store->Target())
                         for (const auto& offsetNode : body)
                              if (const auto* immediate = dynamic_cast<const ecpps::ir::SSAImmNode*>(offsetNode.get());
                                  immediate != nullptr && &address->Right() == &immediate->Result())
                                   offset = immediate->Value();
                    if (const auto* address = dynamic_cast<const ecpps::ir::SSAAddressOfNode*>(addressNode.get());
                        address != nullptr && &address->Result() == &store->Target())
                         offset = 0;
               }

               if (value.has_value() && offset.has_value()) stores.push_back({.value = *value, .offset = *offset});
          }
          return stores;
     }

     bool HasAggregateFieldStore(const std::vector<AggregateFieldStore>& stores, const std::uint64_t value,
                                 const std::uint64_t offset)
     {
          for (const auto& store : stores)
               if (store.value == value && store.offset == offset) return true;
          return false;
     }

     [[nodiscard]] bool IsParameterNode(const ecpps::ir::NodePointer& node)
     {
          return dynamic_cast<const ecpps::ir::ParameterNode*>(node.get()) != nullptr;
     }

     [[nodiscard]] bool IsAggregateIndirectStore(const ecpps::ir::NodePointer& node)
     {
          const auto* store = dynamic_cast<const ecpps::ir::SSAIndirectStoreNode*>(node.get());
          return store != nullptr && store->Access().isAggregate;
     }

     [[nodiscard]] bool HasLoadOfIndirectStoreTarget(const std::vector<ecpps::ir::NodePointer>& body,
                                                     const ecpps::ir::SSAIndirectStoreNode& store)
     {
          for (const auto& node : body)
               if (const auto* load = dynamic_cast<const ecpps::ir::SSALoadNode*>(node.get());
                   load != nullptr && &load->Result() == &store.Target())
                    return true;
          return false;
     }

     [[nodiscard]] bool HasAddressOfIndirectStoreTarget(const std::vector<ecpps::ir::NodePointer>& body,
                                                        const ecpps::ir::SSAIndirectStoreNode& store)
     {
          for (const auto& node : body)
               if (const auto* address = dynamic_cast<const ecpps::ir::SSAAddressOfNode*>(node.get());
                   address != nullptr && &address->Result() == &store.Target())
                    return true;
          return false;
     }

     [[nodiscard]] bool HasAddressOfCallArgument(const std::vector<ecpps::ir::NodePointer>& body,
                                                 const ecpps::ir::SSACallNode& call, const std::size_t argumentIndex)
     {
          if (argumentIndex >= call.Arguments().size()) return false;
          for (const auto& node : body)
               if (const auto* address = dynamic_cast<const ecpps::ir::SSAAddressOfNode*>(node.get());
                   address != nullptr && &address->Result() == call.Arguments()[argumentIndex])
                    return true;
          return false;
     }

     [[nodiscard]] bool IsCallNamed(const ecpps::ir::NodePointer& node, const std::string_view name)
     {
          const auto* call = dynamic_cast<const ecpps::ir::SSACallNode*>(node.get());
          return call != nullptr && call->Function().Name() == name;
     }

     struct CallNamedPredicate
     {
          std::string name;

          [[nodiscard]] bool operator()(const ecpps::ir::NodePointer& node) const
          {
               return IsCallNamed(node, this->name);
          }
     };

     struct FunctionNamePredicate
     {
          std::string_view name;

          [[nodiscard]] bool operator()(const std::shared_ptr<ecpps::ir::FunctionScope>& function) const
          {
               return function->Name() == this->name;
          }
     };

     struct CallWithArgumentCountPredicate
     {
          std::string name;
          std::size_t argumentCount{};

          [[nodiscard]] bool operator()(const ecpps::ir::NodePointer& node) const
          {
               if (!IsCallNamed(node, this->name)) return false;
               const auto* call = dynamic_cast<const ecpps::ir::SSACallNode*>(node.get());
               return call->Arguments().size() == this->argumentCount;
          }
     };

     struct CallCleanupPredicate
     {
          std::string name;
          ecpps::ir::ABICleanupKind cleanupKind{};

          [[nodiscard]] bool operator()(const ecpps::ir::NodePointer& node) const
          {
               if (!IsCallNamed(node, this->name)) return false;
               const auto* call = dynamic_cast<const ecpps::ir::SSACallNode*>(node.get());
               return call->AbiCleanupKind() == this->cleanupKind;
          }
     };

     struct SpecialMemberCallPredicate
     {
          std::string className;
          ecpps::typeSystem::ReferenceType::Kind referenceKind{};

          [[nodiscard]] bool operator()(const ecpps::ir::NodePointer& node) const
          {
               if (!IsCallNamed(node, this->className)) return false;
               const auto* call = dynamic_cast<const ecpps::ir::SSACallNode*>(node.get());
               if (call->Function().parameters.size() != 1) return false;
               const auto* reference =
                    call->Function().parameters.front().type->CastTo<ecpps::typeSystem::ReferenceType>();
               return reference != nullptr && reference->GetKind() == this->referenceKind;
          }
     };

     ecpps::ast::NodePointer MakeClassReferenceType(Fixture& fixture, const std::string& className,
                                                    ecpps::ast::ReferenceType::Kind referenceKind, bool isConst);

     ecpps::ast::NodePointer MakeClassReferenceReturnFunction(Fixture& fixture, const std::string& className,
                                                              const std::string& functionName,
                                                              const ecpps::ast::ReferenceType::Kind referenceKind)
     {
          auto signature = fixture.MakeFunctionSignature("void", functionName, {});
          signature.type = fixture.MakeClassType(className);
          ecpps::ast::FunctionParameter parameter{};
          parameter.type = MakeClassReferenceType(fixture, className, referenceKind, false);
          parameter.name = fixture.MakeIdentifier("item");
          signature.parameters.parameters.push_back(std::move(parameter));
          ecpps::SBOVector<ecpps::ast::NodePointer> body{};
          body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIdentifier("item"), Source));
          return fixture.MakeFunctionDefinition(std::move(signature), std::move(body));
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
                                                  std::vector<ecpps::ast::NodePointer> constructors)
     {
          std::vector<ecpps::ast::NodePointer> members = std::move(constructors);
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
          return MakeASTNode<ecpps::ast::ClassDefNode>(fixture.astAllocator, fixture.astAllocator.Intern(className),
                                                       ecpps::ast::ClassKey::Struct, std::move(members), false, false,
                                                       Source);
     }

     ecpps::ast::NodePointer MakeConstructorClass(Fixture& fixture, const std::string& className,
                                                  ecpps::ast::NodePointer constructor)
     {
          std::vector<ecpps::ast::NodePointer> constructors{};
          constructors.push_back(std::move(constructor));
          return MakeConstructorClass(fixture, className, std::move(constructors));
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

     ecpps::ast::FunctionSignature MakeClassFunctionSignature(
          Fixture& fixture, const std::string& returnType, const std::string& name,
          const std::vector<std::pair<std::string, std::string>>& parameters)
     {
          auto signature = fixture.MakeFunctionSignature("void", name, {});
          signature.type = returnType == "int" || returnType == "void" ? fixture.MakeBasicType(returnType)
                                                                       : fixture.MakeClassType(returnType);
          for (const auto& [type, parameterName] : parameters)
          {
               ecpps::ast::FunctionParameter parameter{};
               parameter.type = type == "int" ? fixture.MakeBasicType("int") : fixture.MakeClassType(type);
               parameter.name = fixture.MakeIdentifier(parameterName);
               signature.parameters.parameters.push_back(std::move(parameter));
          }
          return signature;
     }

     ecpps::ast::NodePointer MakeEqualInitializedVariable(Fixture& fixture, const std::string& type,
                                                          const std::string& name, ecpps::ast::NodePointer initialiser)
     {
          std::vector<ecpps::ast::VariableDeclarationNode::Declarator> declarators{};
          declarators.emplace_back(fixture.MakeIdentifier(name), ecpps::ast::InitialisationType::EqualInitialiser,
                                   std::move(initialiser));
          return MakeASTNode<ecpps::ast::VariableDeclarationNode>(
               fixture.astAllocator, fixture.MakeClassType(type), std::move(declarators),
               ecpps::ast::VariableDeclarationNode::Flags{}, std::nullopt, Source);
     }

     ecpps::ast::NodePointer MakeClassReferenceType(Fixture& fixture, const std::string& className,
                                                    const ecpps::ast::ReferenceType::Kind referenceKind,
                                                    const bool isConst)
     {
          auto classType =
               MakeASTNode<ecpps::ast::ClassType>(fixture.astAllocator, fixture.astAllocator.Intern(className),
                                                  ecpps::ast::ClassKey::Struct, isConst, false, Source);
          return MakeASTNode<ecpps::ast::ReferenceType>(fixture.astAllocator, std::move(classType), referenceKind,
                                                        Source);
     }

     ecpps::ast::NodePointer MakeCopyFromLValueFunction(Fixture& fixture, const std::string& className,
                                                        const std::string& functionName)
     {
          auto signature = fixture.MakeFunctionSignature("void", functionName, {});
          ecpps::ast::FunctionParameter source{};
          source.type = MakeClassReferenceType(fixture, className, ecpps::ast::ReferenceType::Kind::LValue, false);
          source.name = fixture.MakeIdentifier("source");
          signature.parameters.parameters.push_back(std::move(source));

          ecpps::SBOVector<ecpps::ast::NodePointer> body{};
          body.Push(MakeEqualInitializedVariable(fixture, className, "copy", fixture.MakeIdentifier("source")));
          body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
          return fixture.MakeFunctionDefinition(std::move(signature), std::move(body));
     }

     ecpps::ast::NodePointer MakeCopyConstructor(Fixture& fixture, const std::string& className,
                                                 const bool isExplicit = false)
     {
          ecpps::ast::FunctionParameter sourceParameter{};
          sourceParameter.type =
               MakeClassReferenceType(fixture, className, ecpps::ast::ReferenceType::Kind::LValue, true);
          sourceParameter.name = fixture.MakeIdentifier("source");
          ecpps::ast::ParametersAndQualifiers parameters{};
          parameters.parameters.push_back(std::move(sourceParameter));

          auto sourceValue = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
               fixture.astAllocator, fixture.MakeIdentifier("source"), ecpps::ast::Operator::FullStop,
               fixture.MakeIdentifier("value"), Source);
          auto assignValue = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
               fixture.astAllocator, fixture.MakeIdentifier("value"), ecpps::ast::Operator::Assignment,
               std::move(sourceValue), Source);

          ecpps::ast::MemberDeclarator declarator{};
          declarator.declaredIdExpression = fixture.MakeIdentifier(className);
          declarator.suffixes.emplace_back(std::move(parameters));
          declarator.functionBody = ecpps::ast::FunctionBodyKind::Block;
          declarator.body.push_back(std::move(assignValue));
          std::vector<ecpps::ast::MemberDeclarator> declarators{};
          declarators.push_back(std::move(declarator));
          ecpps::ast::DeclSpecifierSequence specifiers{};
          if (isExplicit) specifiers.functionSpecifier.explicitSpecifier = fixture.MakeIntegerLiteral(1);
          return MakeASTNode<ecpps::ast::MemberDeclarationNode>(
               fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
               std::move(specifiers), std::move(declarators), Source);
     }

     ecpps::ast::NodePointer MakeCopyConstructorDeclaration(
          Fixture& fixture, const std::string& className,
          const ecpps::ast::FunctionBodyKind bodyKind = ecpps::ast::FunctionBodyKind::None)
     {
          ecpps::ast::FunctionParameter sourceParameter{};
          sourceParameter.type =
               MakeClassReferenceType(fixture, className, ecpps::ast::ReferenceType::Kind::LValue, true);
          sourceParameter.name = fixture.MakeIdentifier("source");
          ecpps::ast::ParametersAndQualifiers parameters{};
          parameters.parameters.push_back(std::move(sourceParameter));

          ecpps::ast::MemberDeclarator declarator{};
          declarator.declaredIdExpression = fixture.MakeIdentifier(className);
          declarator.suffixes.emplace_back(std::move(parameters));
          declarator.functionBody = bodyKind;
          std::vector<ecpps::ast::MemberDeclarator> declarators{};
          declarators.push_back(std::move(declarator));
          return MakeASTNode<ecpps::ast::MemberDeclarationNode>(
               fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
               ecpps::ast::DeclSpecifierSequence{}, std::move(declarators), Source);
     }

     ecpps::ast::NodePointer MakeMoveConstructor(Fixture& fixture, const std::string& className)
     {
          ecpps::ast::FunctionParameter sourceParameter{};
          sourceParameter.type =
               MakeClassReferenceType(fixture, className, ecpps::ast::ReferenceType::Kind::RValue, false);
          sourceParameter.name = fixture.MakeIdentifier("source");
          ecpps::ast::ParametersAndQualifiers parameters{};
          parameters.parameters.push_back(std::move(sourceParameter));

          auto sourceValue = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
               fixture.astAllocator, fixture.MakeIdentifier("source"), ecpps::ast::Operator::FullStop,
               fixture.MakeIdentifier("value"), Source);
          auto assignValue = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
               fixture.astAllocator, fixture.MakeIdentifier("value"), ecpps::ast::Operator::Assignment,
               std::move(sourceValue), Source);

          ecpps::ast::MemberDeclarator declarator{};
          declarator.declaredIdExpression = fixture.MakeIdentifier(className);
          declarator.suffixes.emplace_back(std::move(parameters));
          declarator.functionBody = ecpps::ast::FunctionBodyKind::Block;
          declarator.body.push_back(std::move(assignValue));
          std::vector<ecpps::ast::MemberDeclarator> declarators{};
          declarators.push_back(std::move(declarator));
          return MakeASTNode<ecpps::ast::MemberDeclarationNode>(
               fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
               ecpps::ast::DeclSpecifierSequence{}, std::move(declarators), Source);
     }

     ecpps::ast::NodePointer MakeCopyAssignmentOperator(Fixture& fixture, const std::string& className,
                                                        const bool isConst = true)
     {
          ecpps::ast::FunctionParameter sourceParameter{};
          sourceParameter.type =
               MakeClassReferenceType(fixture, className, ecpps::ast::ReferenceType::Kind::LValue, isConst);
          sourceParameter.name = fixture.MakeIdentifier("source");
          ecpps::ast::ParametersAndQualifiers parameters{};
          parameters.parameters.push_back(std::move(sourceParameter));

          auto sourceValue = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
               fixture.astAllocator, fixture.MakeIdentifier("source"), ecpps::ast::Operator::FullStop,
               fixture.MakeIdentifier("value"), Source);
          auto assignValue = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
               fixture.astAllocator, fixture.MakeIdentifier("value"), ecpps::ast::Operator::Assignment,
               std::move(sourceValue), Source);
          auto thisExpression = MakeASTNode<ecpps::ast::UnaryOperatorNode>(
               fixture.astAllocator, ecpps::ast::Operator::Asterisk,
               MakeASTNode<ecpps::ast::ThisNode>(fixture.astAllocator, Source), ecpps::ast::UnaryOperatorType::Prefix,
               Source);
          ecpps::ast::MemberDeclarator declarator{};
          declarator.declaredIdExpression = MakeASTNode<ecpps::ast::OperatorFunctionId>(
               fixture.astAllocator, ecpps::ast::Operator::Assignment, Source);
          declarator.suffixes.emplace_back(std::move(parameters));
          declarator.functionBody = ecpps::ast::FunctionBodyKind::Block;
          declarator.body.push_back(std::move(assignValue));
          declarator.body.push_back(
               MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(thisExpression), Source));
          ecpps::ast::DeclSpecifierSequence specifiers{};
          specifiers.definingTypeSpecifier =
               MakeClassReferenceType(fixture, className, ecpps::ast::ReferenceType::Kind::LValue, false);
          std::vector<ecpps::ast::MemberDeclarator> declarators{};
          declarators.push_back(std::move(declarator));
          return MakeASTNode<ecpps::ast::MemberDeclarationNode>(
               fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
               std::move(specifiers), std::move(declarators), Source);
     }

     ecpps::ast::NodePointer MakeDestructor(
          Fixture& fixture, const std::string& className,
          const ecpps::ast::FunctionBodyKind bodyKind = ecpps::ast::FunctionBodyKind::Block)
     {
          ecpps::ast::MemberDeclarator declarator{};
          declarator.declaredIdExpression = fixture.MakeIdentifier("~" + className);
          declarator.suffixes.emplace_back(ecpps::ast::ParametersAndQualifiers{});
          declarator.functionBody = bodyKind;
          std::vector<ecpps::ast::MemberDeclarator> declarators{};
          declarators.push_back(std::move(declarator));
          return MakeASTNode<ecpps::ast::MemberDeclarationNode>(
               fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
               ecpps::ast::DeclSpecifierSequence{}, std::move(declarators), Source);
     }

     ecpps::ast::NodePointer MakeUnqualifiedMemberFunction(Fixture& fixture, const std::string& name)
     {
          ecpps::ast::MemberDeclarator declarator{};
          declarator.declaredIdExpression = fixture.MakeIdentifier(name);
          declarator.suffixes.emplace_back(ecpps::ast::ParametersAndQualifiers{});
          std::vector<ecpps::ast::MemberDeclarator> declarators{};
          declarators.push_back(std::move(declarator));
          return MakeASTNode<ecpps::ast::MemberDeclarationNode>(
               fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
               ecpps::ast::DeclSpecifierSequence{}, std::move(declarators), Source);
     }

     ecpps::ast::NodePointer MakeMemberFunctionWithDerivedReturn(Fixture& fixture, std::string name,
                                                                 const ecpps::ast::PtrOperator::Kind kind,
                                                                 const bool includeReturn = true)
     {
          ecpps::ast::DeclSpecifierSequence specifiers{};
          specifiers.definingTypeSpecifier = fixture.MakeBasicType("int");

          ecpps::ast::MemberDeclarator declarator{};
          declarator.declaredIdExpression = fixture.MakeIdentifier(std::move(name));
          ecpps::ast::PtrOperator pointerOperator{};
          pointerOperator.kind = kind;
          declarator.ptrOperators.push_back(std::move(pointerOperator));
          declarator.suffixes.emplace_back(ecpps::ast::ParametersAndQualifiers{});
          declarator.functionBody = ecpps::ast::FunctionBodyKind::Block;
          if (includeReturn)
          {
               auto returnExpression = fixture.MakeIdentifier("value");
               if (kind == ecpps::ast::PtrOperator::Kind::Pointer)
                    returnExpression = MakeASTNode<ecpps::ast::UnaryOperatorNode>(
                         fixture.astAllocator, ecpps::ast::Operator::Ampersand, std::move(returnExpression),
                         ecpps::ast::UnaryOperatorType::Prefix, Source);
               declarator.body.push_back(
                    MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(returnExpression), Source));
          }

          std::vector<ecpps::ast::MemberDeclarator> declarators{};
          declarators.push_back(std::move(declarator));
          return MakeASTNode<ecpps::ast::MemberDeclarationNode>(
               fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
               std::move(specifiers), std::move(declarators), Source);
     }
} // namespace

TEST_CASE("IR - Member functions can return pointers and references", "[unit][ir][classes]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> members{};
     members.push_back(fixture.MakeMemberDeclaration("int", "value"));
     members.push_back(
          MakeMemberFunctionWithDerivedReturn(fixture, "pointerResult", ecpps::ast::PtrOperator::Kind::Pointer));
     members.push_back(MakeMemberFunctionWithDerivedReturn(fixture, "referenceResult",
                                                           ecpps::ast::PtrOperator::Kind::LValueReference));
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeASTNode<ecpps::ast::ClassDefNode>(
          fixture.astAllocator, fixture.astAllocator.Intern("DerivedReturnMethods"), ecpps::ast::ClassKey::Struct,
          std::move(members), false, false, Source));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());

     REQUIRE((FindProcedure(nodes, "pointerResult") != nullptr));
     REQUIRE((FindProcedure(nodes, "referenceResult") != nullptr));
     const auto classId = fixture.context.globalScope->classes.at("DerivedReturnMethods");
     const auto* classScope = fixture.context.classTable->classes.at(classId).scope;
     REQUIRE((classScope != nullptr));
     const auto pointerFunction = std::ranges::find_if(classScope->functions, FunctionNamePredicate{"pointerResult"});
     const auto referenceFunction =
          std::ranges::find_if(classScope->functions, FunctionNamePredicate{"referenceResult"});
     REQUIRE((pointerFunction != classScope->functions.end()));
     REQUIRE((referenceFunction != classScope->functions.end()));
     CHECK((*pointerFunction)->returnType->CastTo<ecpps::typeSystem::PointerType>() != nullptr);
     CHECK((*referenceFunction)->returnType->CastTo<ecpps::typeSystem::ReferenceType>() != nullptr);
}

TEST_CASE("IR - Assigning to a const-char pointer member is modifiable", "[unit][ir][classes]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     const auto makeConstCharPointer = [&fixture]()
     {
          auto charType = MakeASTNode<ecpps::ast::BasicType>(fixture.astAllocator, fixture.astAllocator.Intern("char"),
                                                             Source, true, false);
          return MakeASTNode<ecpps::ast::PointerType>(fixture.astAllocator, std::move(charType), Source);
     };

     ecpps::ast::DeclSpecifierSequence fieldSpecifiers{};
     fieldSpecifiers.definingTypeSpecifier = makeConstCharPointer();
     ecpps::ast::MemberDeclarator field{};
     field.declaredIdExpression = fixture.MakeIdentifier("value");
     std::vector<ecpps::ast::MemberDeclarator> fields{};
     fields.push_back(std::move(field));
     std::vector<ecpps::ast::NodePointer> members{};
     members.push_back(MakeASTNode<ecpps::ast::MemberDeclarationNode>(
          fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
          std::move(fieldSpecifiers), std::move(fields), Source));

     ecpps::ast::FunctionParameter parameter{};
     parameter.type = makeConstCharPointer();
     parameter.name = fixture.MakeIdentifier("data");
     ecpps::ast::ParametersAndQualifiers parameters{};
     parameters.parameters.push_back(std::move(parameter));
     ecpps::ast::MemberDeclarator setter{};
     setter.declaredIdExpression = fixture.MakeIdentifier("set");
     setter.suffixes.emplace_back(std::move(parameters));
     setter.functionBody = ecpps::ast::FunctionBodyKind::Block;
     setter.body.push_back(MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("value"), ecpps::ast::Operator::Assignment,
          fixture.MakeIdentifier("data"), Source));
     std::vector<ecpps::ast::MemberDeclarator> setters{};
     setters.push_back(std::move(setter));
     ecpps::ast::DeclSpecifierSequence setterSpecifiers{};
     setterSpecifiers.definingTypeSpecifier = fixture.MakeBasicType("void");
     members.push_back(MakeASTNode<ecpps::ast::MemberDeclarationNode>(
          fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
          std::move(setterSpecifiers), std::move(setters), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeASTNode<ecpps::ast::ClassDefNode>(
          fixture.astAllocator, fixture.astAllocator.Intern("ConstPointeePointerMember"), ecpps::ast::ClassKey::Struct,
          std::move(members), false, false, Source));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     CHECK(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Missing non-void member return warns and adds an unreachable node", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> members{};
     members.push_back(MakeMemberFunctionWithDerivedReturn(fixture, "missingReturn",
                                                           ecpps::ast::PtrOperator::Kind::LValueReference, false));
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeASTNode<ecpps::ast::ClassDefNode>(
          fixture.astAllocator, fixture.astAllocator.Intern("MissingMemberReturn"), ecpps::ast::ClassKey::Struct,
          std::move(members), false, false, Source));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* procedure = FindProcedure(nodes, "missingReturn");
     REQUIRE((procedure != nullptr));
     const auto unreachableCount =
          std::ranges::count_if(procedure->Body(),
                                [](const auto& node)
                                {
                                     return dynamic_cast<const ecpps::ir::SSAUnreachableNode*>(node.get()) != nullptr;
                                });
     REQUIRE((unreachableCount == 1));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 1));
     const auto& diagnostic = *fixture.diagnostics.diagnosticsList.front();
     CHECK((diagnostic.Level() == ecpps::diagnostics::DiagnosticsLevel::Warning));
     CHECK((diagnostic.Message().find("missingReturn") != std::string::npos));
     CHECK((diagnostic.Message().find("without returning a value") != std::string::npos));
}

TEST_CASE("IR - Class definition records field layout", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("PairForLayout", {{"int", "number"}, {"char", "marker"}}));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
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
     const ecpps::ir::SSAIndirectStoreNode* store = nullptr;
     for (const auto& node : procedure->Body())
     {
          if (const auto* immediate = dynamic_cast<const ecpps::ir::SSAImmNode*>(node.get()))
               if (immediate->Value() == 7) seven = immediate;
          if (const auto* indirectStore = dynamic_cast<const ecpps::ir::SSAIndirectStoreNode*>(node.get());
              indirectStore != nullptr && seven != nullptr && &indirectStore->Src() == &seven->Result())
               store = indirectStore;
     }
     REQUIRE((seven != nullptr));
     REQUIRE((store != nullptr));
     CHECK(HasAddressOfIndirectStoreTarget(procedure->Body(), *store));

     const auto* fieldLoad = FindNode<ecpps::ir::SSADerefNode>(procedure->Body());
     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
     REQUIRE((fieldLoad != nullptr));
     REQUIRE((returnNode != nullptr));
     REQUIRE((returnNode->Operand() == &fieldLoad->Result()));
}

TEST_CASE("IR - Class parameters copy and class returns use caller storage", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("ValueCopy", {{"int", "value"}}));

     ecpps::SBOVector<ecpps::ast::NodePointer> identityBody{};
     identityBody.Push(
          MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIdentifier("item"), Source));
     ast.push_back(fixture.MakeFunctionDefinition(
          MakeClassFunctionSignature(fixture, "ValueCopy", "identity", {{"ValueCopy", "item"}}),
          std::move(identityBody)));

     ecpps::SBOVector<ecpps::ast::NodePointer> callerBody{};
     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(fixture.MakeIntegerLiteral(17));
     callerBody.Push(fixture.MakeVariableDeclaration(
          "ValueCopy", "source",
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source)));

     ecpps::SBOVector<ecpps::ast::NodePointer> arguments{};
     arguments.Push(fixture.MakeIdentifier("source"));
     auto call = MakeASTNode<ecpps::ast::CallOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("identity"),
                                                           std::move(arguments), Source);
     callerBody.Push(MakeEqualInitializedVariable(fixture, "ValueCopy", "result", std::move(call)));
     auto memberAccess = MakeASTNode<ecpps::ast::BinaryOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("result"), ecpps::ast::Operator::FullStop,
          fixture.MakeIdentifier("value"), Source);
     callerBody.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(memberAccess), Source));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "caller", {}), std::move(callerBody)));

     auto nodes = fixture.Parse(std::move(ast));
     const auto* identity = FindProcedure(nodes, "identity");
     const auto* caller = FindProcedure(nodes, "caller");
     REQUIRE((identity != nullptr));
     REQUIRE((caller != nullptr));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());

     REQUIRE(identity->Scope()->returnsByPointer);
     const auto parameterNodes = std::ranges::count_if(identity->Body(), IsParameterNode);
     REQUIRE((parameterNodes == 2));
     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(identity->Body());
     REQUIRE((returnNode != nullptr));
     REQUIRE_FALSE(returnNode->HasOperand());

     const auto* callNode = FindCall(*caller);
     REQUIRE((callNode != nullptr));
     REQUIRE(callNode->Function().returnsByPointer);
     REQUIRE((callNode->Arguments().size() == 2));
     REQUIRE(FindNode<ecpps::ir::SSADerefNode>(identity->Body()) != nullptr);
     const auto aggregateStore = std::ranges::find_if(identity->Body(), IsAggregateIndirectStore);
     REQUIRE((aggregateStore != identity->Body().end()));
}

TEST_CASE("IR - Class assignment copies the source object", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("AssignableValue", {{"int", "value"}}));

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     auto assignment = MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("left"),
                                                                   ecpps::ast::Operator::Assignment,
                                                                   fixture.MakeIdentifier("right"), Source);
     body.Push(std::move(assignment));
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
     ast.push_back(fixture.MakeFunctionDefinition(
          MakeClassFunctionSignature(fixture, "void", "assign",
                                     {{"AssignableValue", "left"}, {"AssignableValue", "right"}}),
          std::move(body)));

     auto nodes = fixture.Parse(std::move(ast));
     const auto* procedure = FindProcedure(nodes, "assign");
     REQUIRE((procedure != nullptr));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
     const auto aggregateStore = std::ranges::find_if(procedure->Body(), IsAggregateIndirectStore);
     REQUIRE((aggregateStore != procedure->Body().end()));
     const auto* store = dynamic_cast<const ecpps::ir::SSAIndirectStoreNode*>(aggregateStore->get());
     REQUIRE((store != nullptr));
     CHECK(HasLoadOfIndirectStoreTarget(procedure->Body(), *store));
}

TEST_CASE("IR - Class assignment to a local passes its address to aggregate copy", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("LocalAssignableValue", {{"int", "value"}}));

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     for (const auto name : {"left", "right"})
     {
          auto initialiser = MakeASTNode<ecpps::ast::InitialiserListNode>(
               fixture.astAllocator, std::vector<ecpps::ast::NodePointer>{}, Source);
          body.Push(fixture.MakeVariableDeclaration("LocalAssignableValue", name, std::move(initialiser)));
     }
     body.Push(MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("left"),
                                                           ecpps::ast::Operator::Assignment,
                                                           fixture.MakeIdentifier("right"), Source));
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", "assignLocals", {}), std::move(body)));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* procedure = FindProcedure(nodes, "assignLocals");
     REQUIRE((procedure != nullptr));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());

     const ecpps::ir::SSAIndirectStoreNode* lastAggregateStore{};
     for (const auto& node : procedure->Body())
          if (const auto* store = dynamic_cast<const ecpps::ir::SSAIndirectStoreNode*>(node.get());
              store != nullptr && store->Access().isAggregate)
               lastAggregateStore = store;
     REQUIRE((lastAggregateStore != nullptr));
     CHECK(HasAddressOfIndirectStoreTarget(procedure->Body(), *lastAggregateStore));
}

TEST_CASE("IR - By-value class arguments invoke the selected copy constructor", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string className{"CopyConstructedValue"};
     std::vector<ecpps::ast::NodePointer> members{};
     members.push_back(MakeConstructorDeclaration(fixture, className, ecpps::ast::FunctionBodyKind::Defaulted));
     members.push_back(MakeCopyConstructor(fixture, className));
     members.push_back(MakeDestructor(fixture, className));
     ast.push_back(MakeConstructorClass(fixture, className, std::move(members)));

     auto consumeSignature = fixture.MakeFunctionSignature("int", "consume", {});
     ecpps::ast::FunctionParameter consumeParameter{};
     consumeParameter.type = fixture.MakeClassType(className);
     consumeParameter.name = fixture.MakeIdentifier("item");
     consumeSignature.parameters.parameters.push_back(std::move(consumeParameter));
     ecpps::SBOVector<ecpps::ast::NodePointer> consumeBody{};
     auto itemValue = MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("item"),
                                                                  ecpps::ast::Operator::FullStop,
                                                                  fixture.MakeIdentifier("value"), Source);
     consumeBody.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(itemValue), Source));
     ast.push_back(fixture.MakeFunctionDefinition(std::move(consumeSignature), std::move(consumeBody)));

     auto relaySignature = fixture.MakeFunctionSignature("int", "relay", {});
     ecpps::ast::FunctionParameter relayParameter{};
     relayParameter.type = fixture.MakeClassType(className);
     relayParameter.name = fixture.MakeIdentifier("item");
     relaySignature.parameters.parameters.push_back(std::move(relayParameter));
     ecpps::SBOVector<ecpps::ast::NodePointer> relayBody{};
     ecpps::SBOVector<ecpps::ast::NodePointer> consumeArguments{};
     consumeArguments.Push(fixture.MakeIdentifier("item"));
     auto consumeCall = MakeASTNode<ecpps::ast::CallOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("consume"), std::move(consumeArguments), Source);
     relayBody.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(consumeCall), Source));
     ast.push_back(fixture.MakeFunctionDefinition(std::move(relaySignature), std::move(relayBody)));

     ecpps::SBOVector<ecpps::ast::NodePointer> makeCopyBody{};
     makeCopyBody.Push(
          MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIdentifier("item"), Source));
     ast.push_back(fixture.MakeFunctionDefinition(
          MakeClassFunctionSignature(fixture, className, "makeCopy", {{className, "item"}}), std::move(makeCopyBody)));

     ecpps::SBOVector<ecpps::ast::NodePointer> localCopyBody{};
     localCopyBody.Push(
          fixture.MakeVariableDeclaration(className, "source",
                                          MakeASTNode<ecpps::ast::InitialiserListNode>(
                                               fixture.astAllocator, std::vector<ecpps::ast::NodePointer>{}, Source)));
     ecpps::SBOVector<ecpps::ast::NodePointer> localCopyArguments{};
     localCopyArguments.Push(fixture.MakeIdentifier("source"));
     localCopyBody.Push(MakeASTNode<ecpps::ast::CallOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("consume"), std::move(localCopyArguments), Source));
     localCopyBody.Push(
          MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIntegerLiteral(0), Source));
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "copyLocal", {}),
                                                  std::move(localCopyBody)));

     ecpps::SBOVector<ecpps::ast::NodePointer> copyFromReturnBody{};
     copyFromReturnBody.Push(
          fixture.MakeVariableDeclaration(className, "source",
                                          MakeASTNode<ecpps::ast::InitialiserListNode>(
                                               fixture.astAllocator, std::vector<ecpps::ast::NodePointer>{}, Source)));
     ecpps::SBOVector<ecpps::ast::NodePointer> makeCopyArguments{};
     makeCopyArguments.Push(fixture.MakeIdentifier("source"));
     auto makeCopyCall = MakeASTNode<ecpps::ast::CallOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("makeCopy"), std::move(makeCopyArguments), Source);
     copyFromReturnBody.Push(MakeEqualInitializedVariable(fixture, className, "result", std::move(makeCopyCall)));
     copyFromReturnBody.Push(
          MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIntegerLiteral(0), Source));
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("int", "copyFromReturn", {}),
                                                  std::move(copyFromReturnBody)));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* relay = FindProcedure(nodes, "relay");
     const auto* copyLocal = FindProcedure(nodes, "copyLocal");
     const auto* copyFromReturn = FindProcedure(nodes, "copyFromReturn");
     REQUIRE((relay != nullptr));
     REQUIRE((copyLocal != nullptr));
     REQUIRE((copyFromReturn != nullptr));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());

     bool calledCopyConstructor = false;
     bool calledConsume = false;
     std::size_t destructorCalls{};
     for (const auto& node : relay->Body())
          if (const auto* call = dynamic_cast<const ecpps::ir::SSACallNode*>(node.get()))
          {
               calledCopyConstructor |= call->Function().Name() == className;
               calledConsume |= call->Function().Name() == "consume";
               destructorCalls += static_cast<std::size_t>(call->Function().Name() == "~" + className);
          }
     REQUIRE(calledCopyConstructor);
     REQUIRE(calledConsume);
     INFO(relay->ToString(0));
     CHECK((destructorCalls == 2));

     const ecpps::ir::SSACallNode* localCopyConstructorCall{};
     for (const auto& node : copyLocal->Body())
          if (const auto* call = dynamic_cast<const ecpps::ir::SSACallNode*>(node.get());
              call != nullptr && call->Function().Name() == className && call->Arguments().size() == 2)
               localCopyConstructorCall = call;
     REQUIRE((localCopyConstructorCall != nullptr));
     CHECK(HasAddressOfCallArgument(copyLocal->Body(), *localCopyConstructorCall, 1));

     CHECK(std::ranges::count_if(copyFromReturn->Body(), CallWithArgumentCountPredicate{className, 2}) == 1);

     const auto* consume = FindProcedure(nodes, "consume");
     REQUIRE((consume != nullptr));
     const auto consumeDestructorCalls = std::ranges::count_if(consume->Body(), CallNamedPredicate{"~" + className});
     CHECK((consumeDestructorCalls == 1));

     const auto consumeCallPosition = std::ranges::find_if(copyLocal->Body(), CallNamedPredicate{"consume"});
     REQUIRE((consumeCallPosition != copyLocal->Body().end()));
     const auto destructorAfterConsume =
          std::ranges::find_if(std::ranges::next(consumeCallPosition), copyLocal->Body().end(),
                               CallCleanupPredicate{"~" + className, ecpps::ir::ABICleanupKind::ByValueArgument});
     CHECK((destructorAfterConsume != copyLocal->Body().end()));
}

TEST_CASE("IR - Move-eligible class return expressions select move constructors", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string className{"MoveReturnedValue"};
     std::vector<ecpps::ast::NodePointer> members{};
     members.push_back(MakeConstructorDeclaration(fixture, className, ecpps::ast::FunctionBodyKind::Defaulted));
     members.push_back(MakeCopyConstructor(fixture, className));
     members.push_back(MakeMoveConstructor(fixture, className));
     members.push_back(MakeDestructor(fixture, className));
     ast.push_back(MakeConstructorClass(fixture, className, std::move(members)));

     ecpps::SBOVector<ecpps::ast::NodePointer> parameterReturnBody{};
     parameterReturnBody.Push(
          MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIdentifier("item"), Source));
     ast.push_back(fixture.MakeFunctionDefinition(
          MakeClassFunctionSignature(fixture, className, "returnParameter", {{className, "item"}}),
          std::move(parameterReturnBody)));

     ecpps::SBOVector<ecpps::ast::NodePointer> localReturnBody{};
     localReturnBody.Push(
          fixture.MakeVariableDeclaration(className, "item",
                                          MakeASTNode<ecpps::ast::InitialiserListNode>(
                                               fixture.astAllocator, std::vector<ecpps::ast::NodePointer>{}, Source)));
     localReturnBody.Push(
          MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIdentifier("item"), Source));
     ast.push_back(fixture.MakeFunctionDefinition(MakeClassFunctionSignature(fixture, className, "returnLocal", {}),
                                                  std::move(localReturnBody)));

     ast.push_back(MakeClassReferenceReturnFunction(fixture, className, "returnLvalueReference",
                                                    ecpps::ast::ReferenceType::Kind::LValue));
     ast.push_back(MakeClassReferenceReturnFunction(fixture, className, "returnRvalueReference",
                                                    ecpps::ast::ReferenceType::Kind::RValue));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* parameterReturn = FindProcedure(nodes, "returnParameter");
     const auto* localReturn = FindProcedure(nodes, "returnLocal");
     const auto* lvalueReferenceReturn = FindProcedure(nodes, "returnLvalueReference");
     const auto* rvalueReferenceReturn = FindProcedure(nodes, "returnRvalueReference");
     REQUIRE((parameterReturn != nullptr));
     REQUIRE((localReturn != nullptr));
     REQUIRE((lvalueReferenceReturn != nullptr));
     REQUIRE((rvalueReferenceReturn != nullptr));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());

     CHECK(std::ranges::count_if(
                parameterReturn->Body(),
                SpecialMemberCallPredicate{className, ecpps::typeSystem::ReferenceType::Kind::RValue}) == 1);
     CHECK(std::ranges::count_if(
                localReturn->Body(),
                SpecialMemberCallPredicate{className, ecpps::typeSystem::ReferenceType::Kind::RValue}) == 1);
     CHECK(std::ranges::count_if(
                lvalueReferenceReturn->Body(),
                SpecialMemberCallPredicate{className, ecpps::typeSystem::ReferenceType::Kind::LValue}) == 1);
     CHECK(std::ranges::count_if(
                rvalueReferenceReturn->Body(),
                SpecialMemberCallPredicate{className, ecpps::typeSystem::ReferenceType::Kind::RValue}) == 1);
}

TEST_CASE("IR - Implicit class copies invoke declared member copy constructors", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string memberClassName{"DeclaredCopyMember"};
     const std::string wrapperClassName{"CopyMemberWrapper"};
     ast.push_back(
          MakeConstructorClass(fixture, memberClassName, MakeCopyConstructorDeclaration(fixture, memberClassName)));
     ast.push_back(fixture.MakeStructDefinition(wrapperClassName, {{memberClassName, "member"}}));

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIdentifier("value"), Source));
     ast.push_back(fixture.MakeFunctionDefinition(
          MakeClassFunctionSignature(fixture, wrapperClassName, "copyWrapper", {{wrapperClassName, "value"}}),
          std::move(body)));

     const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
     const auto* copyWrapper = FindProcedure(nodes, "copyWrapper");
     REQUIRE((copyWrapper != nullptr));
     CHECK(std::ranges::any_of(copyWrapper->Body(), CallNamedPredicate{memberClassName}));
}

TEST_CASE("IR - Copy initialization does not select an explicit copy constructor", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string className{"ExplicitCopyValue"};
     ast.push_back(MakeConstructorClass(fixture, className, MakeCopyConstructor(fixture, className, true)));

     auto signature = fixture.MakeFunctionSignature("void", "copyInitialize", {});
     ecpps::ast::FunctionParameter source{};
     source.type = MakeClassReferenceType(fixture, className, ecpps::ast::ReferenceType::Kind::LValue, true);
     source.name = fixture.MakeIdentifier("source");
     signature.parameters.parameters.push_back(std::move(source));
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeEqualInitializedVariable(fixture, className, "copy", fixture.MakeIdentifier("source")));
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
     ast.push_back(fixture.MakeFunctionDefinition(std::move(signature), std::move(body)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 1));
     const auto& diagnostic = *fixture.diagnostics.diagnosticsList.front();
     CHECK(diagnostic.Message().find("No viable copy or move constructor") != std::string::npos);
     REQUIRE((diagnostic.SubDiagnostics().size() == 1));
     CHECK(diagnostic.SubDiagnostics().front()->Message().find(
                "Candidate: ExplicitCopyValue::ExplicitCopyValue(const ExplicitCopyValue& source)") !=
           std::string::npos);
     REQUIRE((diagnostic.SubDiagnostics().front()->SubDiagnostics().size() == 1));
     CHECK(diagnostic.SubDiagnostics().front()->SubDiagnostics().front()->Message() ==
           "This explicit constructor is not considered by copy-initialisation");
}

TEST_CASE("IR - Copy assignment invokes the declared operator= overload", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string className{"CopyAssignedValue"};
     std::vector<ecpps::ast::NodePointer> members{};
     members.push_back(MakeCopyAssignmentOperator(fixture, className));
     ast.push_back(MakeConstructorClass(fixture, className, std::move(members)));

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("left"),
                                                           ecpps::ast::Operator::Assignment,
                                                           fixture.MakeIdentifier("right"), Source));
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
     ast.push_back(fixture.MakeFunctionDefinition(
          MakeClassFunctionSignature(fixture, "void", "assign", {{className, "left"}, {className, "right"}}),
          std::move(body)));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* assign = FindProcedure(nodes, "assign");
     REQUIRE((assign != nullptr));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
     const auto calls = std::ranges::count_if(assign->Body(), CallNamedPredicate{"operator="});
     REQUIRE((calls == 1));
}

TEST_CASE("IR - Implicit copy assignment rejects const data members", "[unit][ir][classes][by-value]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string className{"ConstAssignmentValue"};
     ecpps::ast::DeclSpecifierSequence fieldSpecifiers{};
     fieldSpecifiers.definingTypeSpecifier = MakeASTNode<ecpps::ast::BasicType>(
          fixture.astAllocator, fixture.astAllocator.Intern("int"), Source, true, false);
     ecpps::ast::MemberDeclarator field{};
     field.declaredIdExpression = fixture.MakeIdentifier("value");
     std::vector<ecpps::ast::MemberDeclarator> fields{};
     fields.push_back(std::move(field));
     std::vector<ecpps::ast::NodePointer> members{};
     members.push_back(MakeASTNode<ecpps::ast::MemberDeclarationNode>(
          fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
          std::move(fieldSpecifiers), std::move(fields), Source));
     ast.push_back(MakeASTNode<ecpps::ast::ClassDefNode>(fixture.astAllocator, fixture.astAllocator.Intern(className),
                                                         ecpps::ast::ClassKey::Struct, std::move(members), false, false,
                                                         Source));

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("left"),
                                                           ecpps::ast::Operator::Assignment,
                                                           fixture.MakeIdentifier("right"), Source));
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
     ast.push_back(fixture.MakeFunctionDefinition(
          MakeClassFunctionSignature(fixture, "void", "assignConst", {{className, "left"}, {className, "right"}}),
          std::move(body)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics,
                                    "Implicit copy assignment for 'ConstAssignmentValue' is deleted"));
}

TEST_CASE("IR - Class assignment rejects const target objects", "[unit][ir][classes][by-value]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string className{"ConstAssignmentTarget"};
     ast.push_back(fixture.MakeStructDefinition(className, {{"int", "value"}}));

     auto signature =
          MakeClassFunctionSignature(fixture, "void", "assignConstTarget", {{className, "left"}, {className, "right"}});
     signature.parameters.parameters.front().type =
          MakeASTNode<ecpps::ast::ClassType>(fixture.astAllocator, fixture.astAllocator.Intern(className),
                                             ecpps::ast::ClassKey::Struct, true, false, Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("left"),
                                                           ecpps::ast::Operator::Assignment,
                                                           fixture.MakeIdentifier("right"), Source));
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
     ast.push_back(fixture.MakeFunctionDefinition(std::move(signature), std::move(body)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "Class assignment requires a modifiable lvalue"));
}

TEST_CASE("IR - Class locals are destroyed in reverse lifetime order", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string firstClassName{"FirstLifetimeValue"};
     const std::string secondClassName{"SecondLifetimeValue"};
     ast.push_back(MakeConstructorClass(fixture, firstClassName, MakeDestructor(fixture, firstClassName)));
     ast.push_back(MakeConstructorClass(fixture, secondClassName, MakeDestructor(fixture, secondClassName)));

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(
          fixture.MakeVariableDeclaration(firstClassName, "first",
                                          MakeASTNode<ecpps::ast::InitialiserListNode>(
                                               fixture.astAllocator, std::vector<ecpps::ast::NodePointer>{}, Source)));
     body.Push(
          fixture.MakeVariableDeclaration(secondClassName, "second",
                                          MakeASTNode<ecpps::ast::InitialiserListNode>(
                                               fixture.astAllocator, std::vector<ecpps::ast::NodePointer>{}, Source)));
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", "lifetime", {}), std::move(body)));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* lifetime = FindProcedure(nodes, "lifetime");
     REQUIRE((lifetime != nullptr));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
     std::vector<std::string> destructorCalls{};
     for (const auto& node : lifetime->Body())
          if (const auto* call = dynamic_cast<const ecpps::ir::SSACallNode*>(node.get()))
          {
               if (call->Function().Name() == "~" + firstClassName || call->Function().Name() == "~" + secondClassName)
                    destructorCalls.push_back(*call->Function().Name());
          }
     REQUIRE((destructorCalls == std::vector<std::string>{"~" + secondClassName, "~" + firstClassName}));
}

TEST_CASE("IR - By-value class calls can target external declarations", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string className{"ExternalValue"};
     ast.push_back(fixture.MakeStructDefinition(className, {{"int", "value"}}));

     auto externalSignature =
          MakeClassFunctionSignature(fixture, className, "externalIdentity", {{className, "value"}});
     ast.push_back(
          MakeASTNode<ecpps::ast::FunctionDeclarationNode>(fixture.astAllocator, std::move(externalSignature), Source));

     ecpps::SBOVector<ecpps::ast::NodePointer> arguments{};
     arguments.Push(fixture.MakeIdentifier("value"));
     auto externalCall = MakeASTNode<ecpps::ast::CallOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("externalIdentity"), std::move(arguments), Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(externalCall), Source));
     ast.push_back(fixture.MakeFunctionDefinition(
          MakeClassFunctionSignature(fixture, className, "callExternalIdentity", {{className, "value"}}),
          std::move(body)));

     const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
     const auto* caller = FindProcedure(nodes, "callExternalIdentity");
     REQUIRE((caller != nullptr));
     const auto* call = FindCall(*caller);
     REQUIRE((call != nullptr));
     CHECK_FALSE(call->Function().isMemberFunctionDefined);
}

TEST_CASE("IR - Class ABI calls bind to later compiler-defined declarations", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string className{"DefinedValue"};
     ast.push_back(fixture.MakeStructDefinition(className, {{"int", "value"}}));

     auto identitySignature = MakeClassFunctionSignature(fixture, className, "identity", {{className, "value"}});
     ast.push_back(
          MakeASTNode<ecpps::ast::FunctionDeclarationNode>(fixture.astAllocator, std::move(identitySignature), Source));

     ecpps::SBOVector<ecpps::ast::NodePointer> callerBody{};
     ecpps::SBOVector<ecpps::ast::NodePointer> arguments{};
     arguments.Push(fixture.MakeIdentifier("value"));
     auto identityCall = MakeASTNode<ecpps::ast::CallOperatorNode>(
          fixture.astAllocator, fixture.MakeIdentifier("identity"), std::move(arguments), Source);
     callerBody.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(identityCall), Source));
     ast.push_back(fixture.MakeFunctionDefinition(
          MakeClassFunctionSignature(fixture, className, "callIdentity", {{className, "value"}}),
          std::move(callerBody)));

     ecpps::SBOVector<ecpps::ast::NodePointer> identityBody{};
     identityBody.Push(
          MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIdentifier("value"), Source));
     ast.push_back(fixture.MakeFunctionDefinition(
          MakeClassFunctionSignature(fixture, className, "identity", {{className, "value"}}), std::move(identityBody)));

     const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
     const auto* caller = FindProcedure(nodes, "callIdentity");
     REQUIRE((caller != nullptr));
     const auto* call = FindCall(*caller);
     REQUIRE((call != nullptr));
     CHECK(call->Function().isMemberFunctionDefined);
}

TEST_CASE("IR - By-value function declarations ignore top-level class cv", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string className{"QualifiedParameter"};
     ast.push_back(fixture.MakeStructDefinition(className, {{"int", "value"}}));

     auto declaration = MakeClassFunctionSignature(fixture, "int", "read", {{className, "value"}});
     ast.push_back(
          MakeASTNode<ecpps::ast::FunctionDeclarationNode>(fixture.astAllocator, std::move(declaration), Source));

     ecpps::SBOVector<ecpps::ast::NodePointer> callerBody{};
     ecpps::SBOVector<ecpps::ast::NodePointer> arguments{};
     arguments.Push(fixture.MakeIdentifier("value"));
     auto call = MakeASTNode<ecpps::ast::CallOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("read"),
                                                           std::move(arguments), Source);
     callerBody.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(call), Source));
     ast.push_back(fixture.MakeFunctionDefinition(
          MakeClassFunctionSignature(fixture, "int", "callRead", {{className, "value"}}), std::move(callerBody)));

     auto definitionSignature = MakeClassFunctionSignature(fixture, "int", "read", {{className, "value"}});
     definitionSignature.parameters.parameters.front().type =
          MakeASTNode<ecpps::ast::ClassType>(fixture.astAllocator, fixture.astAllocator.Intern(className),
                                             ecpps::ast::ClassKey::Struct, true, false, Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> definitionBody{};
     definitionBody.Push(
          MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, fixture.MakeIntegerLiteral(1), Source));
     ast.push_back(fixture.MakeFunctionDefinition(std::move(definitionSignature), std::move(definitionBody)));

     const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
     const auto* caller = FindProcedure(nodes, "callRead");
     REQUIRE((caller != nullptr));
     const auto* callNode = FindCall(*caller);
     REQUIRE((callNode != nullptr));
     CHECK(callNode->Function().isMemberFunctionDefined);
}

TEST_CASE("IR - Implicit member assignment preserves source constness", "[unit][ir][classes][by-value]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string memberClassName{"NonConstAssignableMember"};
     const std::string ownerClassName{"ConstMemberAssignmentOwner"};
     std::vector<ecpps::ast::NodePointer> memberClassMembers{};
     memberClassMembers.push_back(MakeCopyAssignmentOperator(fixture, memberClassName, false));
     ast.push_back(MakeConstructorClass(fixture, memberClassName, std::move(memberClassMembers)));

     ecpps::ast::DeclSpecifierSequence fieldSpecifiers{};
     fieldSpecifiers.definingTypeSpecifier = fixture.MakeClassType(memberClassName);
     ecpps::ast::MemberDeclarator field{};
     field.declaredIdExpression = fixture.MakeIdentifier("member");
     std::vector<ecpps::ast::MemberDeclarator> fields{};
     fields.push_back(std::move(field));
     std::vector<ecpps::ast::NodePointer> ownerMembers{};
     ownerMembers.push_back(MakeASTNode<ecpps::ast::MemberDeclarationNode>(
          fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
          std::move(fieldSpecifiers), std::move(fields), Source));
     ast.push_back(MakeASTNode<ecpps::ast::ClassDefNode>(
          fixture.astAllocator, fixture.astAllocator.Intern(ownerClassName), ecpps::ast::ClassKey::Struct,
          std::move(ownerMembers), false, false, Source));

     auto signature = MakeClassFunctionSignature(fixture, "void", "assignConstMember",
                                                 {{ownerClassName, "left"}, {ownerClassName, "right"}});
     signature.parameters.parameters.back().type =
          MakeASTNode<ecpps::ast::ClassType>(fixture.astAllocator, fixture.astAllocator.Intern(ownerClassName),
                                             ecpps::ast::ClassKey::Struct, true, false, Source);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("left"),
                                                           ecpps::ast::Operator::Assignment,
                                                           fixture.MakeIdentifier("right"), Source));
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
     ast.push_back(fixture.MakeFunctionDefinition(std::move(signature), std::move(body)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics,
                                    "No viable copy or move assignment operator for 'NonConstAssignableMember'"));
}

TEST_CASE("IR - User destructors destroy class members", "[unit][ir][classes][by-value]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string memberClassName{"LifetimeMember"};
     const std::string ownerClassName{"LifetimeOwner"};
     ast.push_back(MakeConstructorClass(fixture, memberClassName, MakeDestructor(fixture, memberClassName)));

     std::vector<ecpps::ast::NodePointer> ownerMembers{};
     ownerMembers.push_back(MakeDestructor(fixture, ownerClassName));
     ecpps::ast::DeclSpecifierSequence memberSpecifiers{};
     memberSpecifiers.definingTypeSpecifier = fixture.MakeClassType(memberClassName);
     ecpps::ast::MemberDeclarator memberDeclarator{};
     memberDeclarator.declaredIdExpression = fixture.MakeIdentifier("member");
     std::vector<ecpps::ast::MemberDeclarator> members{};
     members.push_back(std::move(memberDeclarator));
     ownerMembers.push_back(MakeASTNode<ecpps::ast::MemberDeclarationNode>(
          fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
          std::move(memberSpecifiers), std::move(members), Source));
     ast.push_back(MakeASTNode<ecpps::ast::ClassDefNode>(
          fixture.astAllocator, fixture.astAllocator.Intern(ownerClassName), ecpps::ast::ClassKey::Struct,
          std::move(ownerMembers), false, false, Source));

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(
          fixture.MakeVariableDeclaration(ownerClassName, "owner",
                                          MakeASTNode<ecpps::ast::InitialiserListNode>(
                                               fixture.astAllocator, std::vector<ecpps::ast::NodePointer>{}, Source)));
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", "destroyOwner", {}), std::move(body)));

     const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
     const auto* ownerDestructor = FindProcedure(nodes, "~" + ownerClassName);
     REQUIRE((ownerDestructor != nullptr));
     const auto* memberDestructorCall = FindCall(*ownerDestructor);
     REQUIRE((memberDestructorCall != nullptr));
     CHECK(memberDestructorCall->Function().Name() == "~" + memberClassName);
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
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(
          fixture.astAllocator, fixture.astAllocator.Intern("second"), fixture.MakeIntegerLiteral(9), Source));
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
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(
          fixture.astAllocator, fixture.astAllocator.Intern("second"), fixture.MakeIntegerLiteral(9), Source));
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(
          fixture.astAllocator, fixture.astAllocator.Intern("first"), fixture.MakeIntegerLiteral(5), Source));
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
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(
          fixture.astAllocator, fixture.astAllocator.Intern("missing"), fixture.MakeIntegerLiteral(7), Source));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("PairWithUnknownDesignator", "pair", std::move(initialiser)));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", "unknownField", {}), std::move(body)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));

     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "'missing' is not a direct data member"));
}

TEST_CASE("IR - Duplicate designated aggregate members are diagnosed", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("PairWithDuplicateDesignator", {{"int", "first"}, {"int", "second"}}));

     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(
          fixture.astAllocator, fixture.astAllocator.Intern("first"), fixture.MakeIntegerLiteral(5), Source));
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(
          fixture.astAllocator, fixture.astAllocator.Intern("first"), fixture.MakeIntegerLiteral(9), Source));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("PairWithDuplicateDesignator", "pair", std::move(initialiser)));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", "duplicateField", {}), std::move(body)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));

     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "Member 'first' is initialised more than once"));
}

TEST_CASE("IR - Positional and designated aggregate initialisers cannot be mixed", "[unit][ir][classes]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeStructDefinition("PairWithMixedInitialisers", {{"int", "first"}, {"int", "second"}}));

     std::vector<ecpps::ast::NodePointer> initialisers{};
     initialisers.push_back(fixture.MakeIntegerLiteral(5));
     initialisers.push_back(MakeASTNode<ecpps::ast::DesignatedInitialiserNode>(
          fixture.astAllocator, fixture.astAllocator.Intern("second"), fixture.MakeIntegerLiteral(9), Source));
     auto initialiser =
          MakeASTNode<ecpps::ast::InitialiserListNode>(fixture.astAllocator, std::move(initialisers), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(fixture.MakeVariableDeclaration("PairWithMixedInitialisers", "pair", std::move(initialiser)));
     ast.push_back(fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", "mixedInitialisers", {}),
                                                  std::move(body)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));

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
     CHECK((&call->Function() == constructor->Scope()));
     CHECK((call->Arguments().size() == 2));
     CHECK((constructor->Scope()->parameters.size() == 1));
     CHECK((call->Function().memberClassInfo != nullptr));
     CHECK((FindNode<ecpps::ir::SSAIndirectStoreNode>(constructor->Body()) != nullptr));
     CHECK((FindNode<ecpps::ir::SSAIndirectStoreNode>(caller->Body()) == nullptr));
     CHECK(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Class construction accepts a string literal", "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     const std::string className{"StringLiteralCtorForIR"};

     ecpps::ast::FunctionParameter parameter{};
     auto charType = MakeASTNode<ecpps::ast::BasicType>(fixture.astAllocator, fixture.astAllocator.Intern("char"),
                                                        Source, true, false);
     parameter.type = MakeASTNode<ecpps::ast::PointerType>(fixture.astAllocator, std::move(charType), Source);
     parameter.name = fixture.MakeIdentifier("value");
     ecpps::ast::ParametersAndQualifiers parameters{};
     parameters.parameters.push_back(std::move(parameter));

     ecpps::ast::MemberDeclarator constructor{};
     constructor.declaredIdExpression = fixture.MakeIdentifier(className);
     constructor.suffixes.emplace_back(std::move(parameters));
     constructor.functionBody = ecpps::ast::FunctionBodyKind::Block;
     std::vector<ecpps::ast::MemberDeclarator> constructors{};
     constructors.push_back(std::move(constructor));
     auto constructorDeclaration = MakeASTNode<ecpps::ast::MemberDeclarationNode>(
          fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
          ecpps::ast::DeclSpecifierSequence{}, std::move(constructors), Source);

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeConstructorClass(fixture, className, std::move(constructorDeclaration)));
     std::vector<ecpps::ast::NodePointer> arguments{};
     arguments.push_back(MakeASTNode<ecpps::ast::StringLiteralNode>(fixture.astAllocator,
                                                                    fixture.astAllocator.Intern("hello"), Source));
     ast.push_back(
          MakeDefaultConstructionCaller(fixture, className, "constructFromStringLiteral", std::move(arguments)));

     const auto nodes = fixture.Parse(std::move(ast));
     const auto* caller = FindProcedure(nodes, "constructFromStringLiteral");
     REQUIRE((caller != nullptr));
     const auto* call = FindCall(*caller);
     REQUIRE((call != nullptr));
     CHECK((call->Function().parameters.size() == 1));
     CHECK((call->Function().parameters.front().type->CastTo<ecpps::typeSystem::PointerType>() != nullptr));
     CHECK(fixture.diagnostics.diagnosticsList.empty());

     std::array<char, 6> executable{'e', 'c', 'p', 'p', 's', '\0'};
     std::array<char, 11> source{'s', 'o', 'u', 'r', 'c', 'e', '.', 'c', 'p', 'p', '\0'};
     std::array<char*, 2> commandLineArguments{executable.data(), source.data()};
     ecpps::CompilerConfig config{2, commandLineArguments.data()};
     ecpps::codegen::AssemblyContext assembly{config};
     ecpps::abi::api::Target target{};
     target.EnsureVRM();
     target.platform = std::make_unique<ecpps::abi::encoders::x8664::LinuxPlatform>(nullptr);
     ecpps::SourceFile sourceFile{};
     ecpps::codegen::Compile(assembly, config, sourceFile, nodes, &target);

     const std::array<ecpps::codegen::Byte, 7> expected{u8'h', u8'e', u8'l', u8'l', u8'o', u8'\0', u8'\0'};
     CHECK(std::ranges::equal(assembly.GetStringSection(), expected));
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
     CHECK((&call->Function() == constructor->Scope()));
     CHECK((FindNode<ecpps::ir::SSAIndirectStoreNode>(constructor->Body()) != nullptr));
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
     CHECK((call->Function().memberClassInfo != nullptr));
     CHECK((FindProcedure(nodes, "ExternalCtorForIR") == nullptr));
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
     CHECK((&call->Function() == constructor->Scope()));
     CHECK((call->Arguments().size() == 2));
     CHECK((constructor->Scope()->parameters.size() == 1));
     CHECK((FindNode<ecpps::ir::SSAIndirectStoreNode>(constructor->Body()) != nullptr));
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
     CHECK((&call->Function() == constructor->Scope()));
     CHECK((FindNode<ecpps::ir::SSAIndirectStoreNode>(constructor->Body()) != nullptr));
     CHECK(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - No matching constructor diagnostics list declared candidates", "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeConstructorClass(fixture, "NoMatchCtorForIR",
                                        MakeConstructorDeclaration(fixture, "NoMatchCtorForIR",
                                                                   ecpps::ast::FunctionBodyKind::None,
                                                                   {{"int", "valueParam"}})));
     ast.push_back(MakeDefaultConstructionCaller(fixture, "NoMatchCtorForIR", "callNoMatchCtor"));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 1));
     const auto& diagnostic = *fixture.diagnostics.diagnosticsList.front();
     CHECK((diagnostic.Message() == "No constructor of 'NoMatchCtorForIR' matches these arguments"));
     REQUIRE((diagnostic.SubDiagnostics().size() == 1));
     CHECK((diagnostic.SubDiagnostics().front()->Message().find(
                 "Candidate: NoMatchCtorForIR::NoMatchCtorForIR(int valueParam)") != std::string::npos));
}

TEST_CASE("IR - String literal with copy constructor candidate reports no match", "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     const std::string className{"StringLiteralNoMatchForIR"};
     std::vector<ecpps::ast::NodePointer> constructors{};
     constructors.push_back(MakeCopyConstructorDeclaration(fixture, className));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeConstructorClass(fixture, className, std::move(constructors)));
     std::vector<ecpps::ast::NodePointer> arguments{};
     arguments.push_back(MakeASTNode<ecpps::ast::StringLiteralNode>(fixture.astAllocator,
                                                                    fixture.astAllocator.Intern("hello"), Source));
     ast.push_back(MakeDefaultConstructionCaller(fixture, className, "callStringLiteralNoMatch", std::move(arguments)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics,
                                    "No constructor of 'StringLiteralNoMatchForIR' matches these arguments"));
}

TEST_CASE("IR - Invalid constructor and destructor names are diagnosed", "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     std::vector<ecpps::ast::NodePointer> members{};
     members.push_back(MakeUnqualifiedMemberFunction(fixture, "X"));
     members.push_back(MakeUnqualifiedMemberFunction(fixture, "~X"));
     ast.push_back(MakeConstructorClass(fixture, "CX", std::move(members)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 2));
     CHECK(fixture.diagnostics.diagnosticsList[0]->Message() ==
           "Invalid constructor name 'X' in class 'CX', expected 'CX'");
     CHECK(fixture.diagnostics.diagnosticsList[1]->Message() ==
           "Invalid destructor name '~X' in class 'CX', expected '~CX'");
}

TEST_CASE("IR - Deleted copy constructor diagnostics include the candidate", "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string className{"DeletedCopyCandidate"};
     ast.push_back(MakeConstructorClass(
          fixture, className,
          MakeCopyConstructorDeclaration(fixture, className, ecpps::ast::FunctionBodyKind::Deleted)));

     auto signature = MakeClassFunctionSignature(fixture, "void", "copyDeleted", {{className, "source"}});
     signature.parameters.parameters.front().type =
          MakeClassReferenceType(fixture, className, ecpps::ast::ReferenceType::Kind::LValue, true);
     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeEqualInitializedVariable(fixture, className, "copy", fixture.MakeIdentifier("source")));
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
     ast.push_back(fixture.MakeFunctionDefinition(std::move(signature), std::move(body)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 1));
     const auto& diagnostic = *fixture.diagnostics.diagnosticsList.front();
     CHECK(diagnostic.Message() == "The selected constructor is deleted");
     REQUIRE((diagnostic.SubDiagnostics().size() == 1));
     CHECK(diagnostic.SubDiagnostics().front()->Message().find(
                "Candidate: DeletedCopyCandidate::DeletedCopyCandidate(const DeletedCopyCandidate& source)") !=
           std::string::npos);
     REQUIRE((diagnostic.SubDiagnostics().front()->SubDiagnostics().size() == 1));
     CHECK(diagnostic.SubDiagnostics().front()->SubDiagnostics().front()->Message() == "This candidate is deleted");
}

TEST_CASE("IR - Implicit copy constructor is deleted when a member copy constructor is deleted",
          "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string memberName{"MemberWithDeletedCopy"};
     const std::string ownerName{"OwnerWithDeletedCopy"};
     ast.push_back(MakeConstructorClass(
          fixture, memberName,
          MakeCopyConstructorDeclaration(fixture, memberName, ecpps::ast::FunctionBodyKind::Deleted)));
     ecpps::ast::DeclSpecifierSequence specifiers{};
     specifiers.definingTypeSpecifier = fixture.MakeBasicType(memberName);
     ecpps::ast::MemberDeclarator member{};
     member.declaredIdExpression = fixture.MakeIdentifier("member");
     member.suffixes.emplace_back(ecpps::ast::ArrayBound{.size = fixture.MakeIntegerLiteral(2), .location = Source});
     std::vector<ecpps::ast::MemberDeclarator> declarators{};
     declarators.push_back(std::move(member));
     std::vector<ecpps::ast::NodePointer> members{};
     members.push_back(MakeASTNode<ecpps::ast::MemberDeclarationNode>(
          fixture.astAllocator, std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
          std::move(specifiers), std::move(declarators), Source));
     ast.push_back(MakeASTNode<ecpps::ast::ClassDefNode>(fixture.astAllocator, fixture.astAllocator.Intern(ownerName),
                                                         ecpps::ast::ClassKey::Struct, std::move(members), false, false,
                                                         Source));
     ast.push_back(MakeCopyFromLValueFunction(fixture, ownerName, "copyOwnerWithDeletedMember"));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 1));
     const auto& diagnostic = *fixture.diagnostics.diagnosticsList.front();
     CHECK(diagnostic.Message() == "The selected constructor is deleted");
     REQUIRE((diagnostic.SubDiagnostics().size() == 1));
     CHECK(diagnostic.SubDiagnostics().front()->Message().find(
                "Candidate: OwnerWithDeletedCopy::OwnerWithDeletedCopy(const OwnerWithDeletedCopy& source)") !=
           std::string::npos);
}

TEST_CASE("IR - Deleted destructor diagnostics include the candidate", "[unit][ir][classes][destructors]")
{
     ecpps::abi::ABI::Current().SetPointerSize(sizeof(std::uintptr_t));
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     const std::string className{"DeletedDestructorCandidate"};
     ast.push_back(MakeConstructorClass(fixture, className,
                                        MakeDestructor(fixture, className, ecpps::ast::FunctionBodyKind::Deleted)));

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(
          fixture.MakeVariableDeclaration(className, "value",
                                          MakeASTNode<ecpps::ast::InitialiserListNode>(
                                               fixture.astAllocator, std::vector<ecpps::ast::NodePointer>{}, Source)));
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, nullptr, Source));
     ast.push_back(
          fixture.MakeFunctionDefinition(fixture.MakeFunctionSignature("void", "destroyDeleted", {}), std::move(body)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 1));
     const auto& diagnostic = *fixture.diagnostics.diagnosticsList.front();
     CHECK(diagnostic.Message() == "Destructor for 'DeletedDestructorCandidate' is deleted");
     REQUIRE((diagnostic.SubDiagnostics().size() == 1));
     CHECK(diagnostic.SubDiagnostics().front()->Message().find(
                "Candidate: DeletedDestructorCandidate::~DeletedDestructorCandidate()") != std::string::npos);
     REQUIRE((diagnostic.SubDiagnostics().front()->SubDiagnostics().size() == 1));
     CHECK(diagnostic.SubDiagnostics().front()->SubDiagnostics().front()->Message() == "This candidate is deleted");
}

TEST_CASE("IR - Ambiguous constructor diagnostics list tied candidates", "[unit][ir][classes][constructors]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     std::vector<ecpps::ast::NodePointer> constructors{};
     constructors.push_back(MakeConstructorDeclaration(
          fixture, "AmbiguousCtorForIR", ecpps::ast::FunctionBodyKind::None, {{"unsigned int", "unsignedValue"}}));
     constructors.push_back(MakeConstructorDeclaration(fixture, "AmbiguousCtorForIR",
                                                       ecpps::ast::FunctionBodyKind::None, {{"long", "longValue"}}));
     ast.push_back(MakeConstructorClass(fixture, "AmbiguousCtorForIR", std::move(constructors)));
     std::vector<ecpps::ast::NodePointer> arguments{};
     arguments.push_back(fixture.MakeIntegerLiteral(10));
     ast.push_back(
          MakeDefaultConstructionCaller(fixture, "AmbiguousCtorForIR", "callAmbiguousCtor", std::move(arguments)));

     [[maybe_unused]] const auto nodes = fixture.Parse(std::move(ast));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 1));
     const auto& diagnostic = *fixture.diagnostics.diagnosticsList.front();
     CHECK((diagnostic.Message() == "Constructor call for 'AmbiguousCtorForIR' is ambiguous"));
     REQUIRE((diagnostic.SubDiagnostics().size() == 2));
     CHECK((diagnostic.SubDiagnostics()[0]->Message().find("AmbiguousCtorForIR::AmbiguousCtorForIR(unsigned int") !=
            std::string::npos));
     CHECK((diagnostic.SubDiagnostics()[1]->Message().find("AmbiguousCtorForIR::AmbiguousCtorForIR(long") !=
            std::string::npos));
}
