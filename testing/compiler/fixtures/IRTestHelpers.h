#pragma once

#include <Execution/Context.h>
#include <Execution/IR.h>
#include <Execution/Operations.h>
#include <Execution/Procedural.h>
#include <Parsing/AST.h>
#include <Parsing/ASTs/General.h>
#include <Shared/BumpAllocator.h>
#include <Shared/Diagnostics.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace IRTestHelpers
{
     constexpr ecpps::Location Source{1, 1, 1};

     template <typename T, typename... TArgs>
     ecpps::ast::NodePointer MakeASTNode(ecpps::ast::ASTContext& allocator, TArgs&&... args)
     {
          return ecpps::ast::NodePointer{new (allocator) T(std::forward<TArgs>(args)...), ecpps::ast::ASTDeleter{}};
     }

     struct Fixture
     {
          ecpps::Diagnostics diagnostics{};
          ecpps::ast::ASTContext astAllocator{64uz * 1024};
          ecpps::BumpAllocator irAllocator{64uz * 1024};
          ecpps::ir::Context context{diagnostics, irAllocator};

          [[nodiscard]] ecpps::ast::NodePointer MakeBasicType(std::string name)
          {
               return MakeASTNode<ecpps::ast::BasicType>(this->astAllocator, this->astAllocator.Intern(name), Source,
                                                         false, false);
          }

          [[nodiscard]] ecpps::ast::NodePointer MakeIdentifier(std::string name)
          {
               return MakeASTNode<ecpps::ast::IdentifierNode>(this->astAllocator, this->astAllocator.Intern(name),
                                                              Source);
          }

          [[nodiscard]] ecpps::ast::FunctionSignature MakeFunctionSignature(
               std::string returnType, std::string name,
               const std::vector<std::pair<std::string, std::string>>& parameters)
          {
               ecpps::SBOVector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>> attributes{};
               ecpps::ast::FunctionSignature signature{this->MakeBasicType(std::move(returnType)),
                                                       false,
                                                       false,
                                                       ecpps::ast::ConstantExpressionSpecifier::None,
                                                       std::move(attributes),
                                                       this->MakeIdentifier(std::move(name)),
                                                       ecpps::abi::ABI::Current().DefaultCallingConventionName()};
               for (const auto& [type, parameterName] : parameters)
               {
                    ecpps::ast::FunctionParameter parameter{};
                    parameter.type = this->MakeBasicType(type);
                    parameter.name = this->MakeIdentifier(parameterName);
                    signature.parameters.parameters.push_back(std::move(parameter));
               }
               return signature;
          }

          [[nodiscard]] ecpps::ast::NodePointer MakeFunctionDeclaration(const std::string& name,
                                                                        const std::string& parameterType)
          {
               auto signature = this->MakeFunctionSignature("int", name, {{parameterType, "value"}});
               return MakeASTNode<ecpps::ast::FunctionDeclarationNode>(this->astAllocator, std::move(signature),
                                                                       Source);
          }

          [[nodiscard]] ecpps::ast::NodePointer MakeOverloadCaller(const std::string& parameterType)
          {
               ecpps::SBOVector<ecpps::ast::NodePointer> arguments{};
               arguments.Push(this->MakeIdentifier("value"));
               auto call = MakeASTNode<ecpps::ast::CallOperatorNode>(this->astAllocator, this->MakeIdentifier("pick"),
                                                                     std::move(arguments), Source);

               ecpps::SBOVector<ecpps::ast::NodePointer> body{};
               body.Push(MakeASTNode<ecpps::ast::ReturnNode>(this->astAllocator, std::move(call), Source));

               auto signature = this->MakeFunctionSignature("int", "caller", {{parameterType, "value"}});
               return MakeASTNode<ecpps::ast::FunctionDefinitionNode>(this->astAllocator, std::move(signature),
                                                                      std::move(body), Source);
          }

          [[nodiscard]] ecpps::ast::NodePointer MakeMemberDeclaration(const std::string& type, const std::string& name)
          {
               ecpps::ast::DeclSpecifierSequence specifiers{};
               specifiers.definingTypeSpecifier = this->MakeBasicType(type);

               ecpps::ast::MemberDeclarator declarator{};
               declarator.declaredIdExpression = this->MakeIdentifier(name);

               std::vector<ecpps::ast::MemberDeclarator> declarators{};
               declarators.push_back(std::move(declarator));
               return MakeASTNode<ecpps::ast::MemberDeclarationNode>(
                    this->astAllocator,
                    std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>{},
                    std::move(specifiers), std::move(declarators), Source);
          }

          [[nodiscard]] ecpps::ast::NodePointer MakeStructDefinition(
               std::string name, const std::vector<std::pair<std::string, std::string>>& fields)
          {
               std::vector<ecpps::ast::NodePointer> members{};
               members.reserve(fields.size());
               for (const auto& [type, fieldName] : fields)
                    members.push_back(this->MakeMemberDeclaration(type, fieldName));
               return MakeASTNode<ecpps::ast::ClassDefNode>(this->astAllocator, this->astAllocator.Intern(name),
                                                            ecpps::ast::ClassKey::Struct, std::move(members), false,
                                                            false, Source);
          }

          [[nodiscard]] ecpps::ast::NodePointer MakeClassType(std::string name)
          {
               return MakeASTNode<ecpps::ast::ClassType>(this->astAllocator, this->astAllocator.Intern(name),
                                                         ecpps::ast::ClassKey::Struct, false, false, Source);
          }

          [[nodiscard]] ecpps::ast::NodePointer MakeVariableDeclaration(std::string typeName, std::string variableName,
                                                                        ecpps::ast::NodePointer initialiser)
          {
               std::vector<ecpps::ast::VariableDeclarationNode::Declarator> declarators{};
               declarators.emplace_back(this->MakeIdentifier(std::move(variableName)),
                                        ecpps::ast::InitialisationType::BracedInitialiser, std::move(initialiser));
               return MakeASTNode<ecpps::ast::VariableDeclarationNode>(
                    this->astAllocator, this->MakeClassType(std::move(typeName)), std::move(declarators),
                    ecpps::ast::VariableDeclarationNode::Flags{}, std::nullopt, Source);
          }

          [[nodiscard]] ecpps::ast::NodePointer MakeIntegerLiteral(const unsigned long long value)
          {
               return MakeASTNode<ecpps::ast::IntegerLiteralNode>(this->astAllocator, value, Source);
          }

          [[nodiscard]] ecpps::ast::NodePointer MakeFunctionDefinition(ecpps::ast::FunctionSignature signature,
                                                                       ecpps::SBOVector<ecpps::ast::NodePointer> body)
          {
               return MakeASTNode<ecpps::ast::FunctionDefinitionNode>(this->astAllocator, std::move(signature),
                                                                      std::move(body), Source);
          }

          [[nodiscard]] std::vector<ecpps::ir::NodePointer> Parse(std::vector<ecpps::ast::NodePointer> ast)
          {
               return ecpps::ir::IR::Parse(this->context, ast);
          }
     };

     [[nodiscard]] inline const ecpps::ir::ProcedureNode* FindProcedure(
          const std::vector<ecpps::ir::NodePointer>& nodes, const std::string& name)
     {
          const auto it = std::ranges::find_if(nodes,
                                               [&name](const ecpps::ir::NodePointer& node)
                                               {
                                                    const auto* procedure =
                                                         dynamic_cast<const ecpps::ir::ProcedureNode*>(node.get());
                                                    return procedure != nullptr && procedure->Name() == name;
                                               });
          return it == nodes.end() ? nullptr : dynamic_cast<const ecpps::ir::ProcedureNode*>(it->get());
     }

     [[nodiscard]] inline const ecpps::ir::SSACallNode* FindCall(const ecpps::ir::ProcedureNode& procedure)
     {
          const auto it =
               std::ranges::find_if(procedure.Body(),
                                    [](const ecpps::ir::NodePointer& node)
                                    {
                                         return dynamic_cast<const ecpps::ir::SSACallNode*>(node.get()) != nullptr;
                                    });
          return it == procedure.Body().end() ? nullptr : dynamic_cast<const ecpps::ir::SSACallNode*>(it->get());
     }

     template <typename TNode>
     [[nodiscard]] inline const TNode* FindNode(const std::vector<ecpps::ir::NodePointer>& nodes)
     {
          const auto it = std::ranges::find_if(nodes,
                                               [](const ecpps::ir::NodePointer& node)
                                               {
                                                    return dynamic_cast<const TNode*>(node.get()) != nullptr;
                                               });
          return it == nodes.end() ? nullptr : dynamic_cast<const TNode*>(it->get());
     }

     [[nodiscard]] inline const ecpps::typeSystem::ClassInfo& FindClassInfo(const Fixture& fixture,
                                                                            const std::string& name)
     {
          const auto id = fixture.context.globalScope->classes.at(name);
          return ecpps::ir::GetTypeContext().Class(id);
     }

     [[nodiscard]] inline bool HasTypeErrorContaining(const ecpps::Diagnostics& diagnostics, const std::string& message)
     {
          return std::ranges::any_of(diagnostics.diagnosticsList,
                                     [&message](const ecpps::diagnostics::DiagnosticsMessage& diagnostic)
                                     {
                                          return diagnostic != nullptr &&
                                                 diagnostic->Level() == ecpps::diagnostics::DiagnosticsLevel::Error &&
                                                 diagnostic->Name() == "type-error" &&
                                                 diagnostic->Message().find(message) != std::string::npos;
                                     });
     }
} // namespace IRTestHelpers
