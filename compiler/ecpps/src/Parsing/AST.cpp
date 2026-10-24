#include "AST.h"
#include <RuntimeAssert.h>
#include <format>
#include <memory>
#include <ranges>
#include <unordered_set>
#include <utility>
#include <vector>
#include "ASTs/Type.h"
#include "Parsing/ASTContext.h"
#include "Parsing/Tokeniser.h"
#include "Shared/Error.h"

ecpps::ast::Node::~Node(void) = default;

using ecpps::ast::NodePointer;
static std::unordered_set<std::string> SimpleTypes = {"char",     "char8_t", "char16_t", "char32_t", "wchar_t",
                                                      "bool",     "short",   "int",      "long",     "signed",
                                                      "unsigned", "float",   "double",   "void"};

static std::string CombineTypeWords(const std::vector<std::string>& words);

static bool IsOperatorToken(const ecpps::Token& token, std::string_view op)
{
     return token.type == ecpps::TokenType::Operator && token.AsOperator() == op;
}

static bool IsWord(const ecpps::Token& token, std::string_view word)
{
     if (token.type == ecpps::TokenType::Keyword) return token.AsKeyword() == word;
     if (token.type == ecpps::TokenType::Identifier) return token.AsIdentifier() == word;
     return false;
}

std::vector<NodePointer> ecpps::ast::AST::Parse(ASTContext& context)
{
     std::vector<NodePointer> nodes{};

     while (!this->AtEnd()) // declaration-seq
     {
          const auto before = CurrentPosition();
          auto node = this->ParseDeclaration(context);
          if (node != nullptr) nodes.push_back(std::move(node));
          else if (CurrentPosition() == before)
               Advance(); // stray '}' etc.
     }

     return nodes;
}

std::unique_ptr<ecpps::ast::IdentifierNode, ecpps::ast::ASTDeleter> ecpps::ast::AST::ParseIdentifier(
     ASTContext& context)
{
     if (Peek().type != TokenType::Identifier) return nullptr;
     const auto& identifier = Peek();
     Advance();
     runtime_assert(std::holds_alternative<std::string>(identifier.value), "Identifier was not an identifier");
     return std::unique_ptr<IdentifierNode, ecpps::ast::ASTDeleter>(
          new (context) IdentifierNode(std::get<std::string>(identifier.value), identifier.location));
}

NodePointer ecpps::ast::AST::ParseSimpleTypeSpecifier(ASTContext& context, bool isConst, bool isVolatile)
{
     auto source = Peek().location;

     if (Peek().type == TokenType::Keyword && SimpleTypes.contains(std::get<std::string>(Peek().value)))
     {
          source.endPosition = Peek().location.endPosition;
          Advance();
          return std::unique_ptr<BasicType, ecpps::ast::ASTDeleter>(
               new (context) BasicType(std::get<std::string>(Peek(-1).value), source, isConst, isVolatile));
     }
     const auto& peek = Peek();
     this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
          std::format("Expected a simple-type-specifier, found "), peek.location));
     Advance();
     return nullptr; // TODO: Error
}

ecpps::ast::ASTExpected ecpps::ast::AST::ParseTypeId(ASTContext& context)
{
     auto source = this->Peek().location;

     auto parseCV = [this]
     {
          bool isConst = false;
          bool isVolatile = false;

          while (Peek().type == TokenType::Keyword)
          {
               const auto& kw = std::get<std::string>(Peek().value);
               if (kw == "const")
               {
                    isConst = true;
                    Advance();
               }
               else if (kw == "volatile")
               {
                    isVolatile = true;
                    Advance();
               }
               else
                    break;
          }

          return std::pair{isConst, isVolatile};
     };

     auto [leadConst, leadVolatile] = parseCV();

     NodePointer type = nullptr;

     if (Peek().type == TokenType::Keyword && SimpleTypes.contains(std::get<std::string>(Peek().value)))
     {
          std::vector<std::string> typeWords;
          while (Peek().type == TokenType::Keyword && SimpleTypes.contains(std::get<std::string>(Peek().value)))
          {
               typeWords.push_back(std::get<std::string>(Peek().value));
               Advance();
          }

          std::string combinedType = CombineTypeWords(typeWords);

          type = std::unique_ptr<BasicType, ecpps::ast::ASTDeleter>(
               new (context) BasicType(combinedType, source, leadConst, leadVolatile));
     }
     else if (Peek().type == TokenType::Identifier)
     {
          const auto& typeName = std::get<std::string>(Peek().value);
          const auto& peekSource = Peek().location;
          source.endPosition = peekSource.endPosition;
          Advance();
          type = std::unique_ptr<BasicType, ecpps::ast::ASTDeleter>(
               new (context) BasicType(typeName, source, leadConst, leadVolatile));
     }
     else
          type = ParseSimpleTypeSpecifier(context, leadConst, leadVolatile);

     if (type == nullptr) return ASTExpected{std::move(type)};

     while (true)
     {
          if (IsOperatorToken(Peek(), "*"))
          {
               Advance();
               auto [isConst, isVolatile] = parseCV();
               source.endPosition = Peek(-1).location.endPosition;
               type = std::unique_ptr<PointerType, ASTDeleter>(
                    new (context) PointerType(std::move(type), isConst, isVolatile, source));
          }
          else if (IsOperatorToken(Peek(), "&"))
          {
               Advance();
               source.endPosition = Peek(-1).location.endPosition;
               type = std::unique_ptr<ReferenceType, ASTDeleter>(
                    new (context) ReferenceType(std::move(type), ReferenceType::Kind::LValue, source));
          }
          else if (IsOperatorToken(Peek(), "&&"))
          {
               Advance();
               source.endPosition = Peek(-1).location.endPosition;
               type = std::unique_ptr<ReferenceType, ASTDeleter>(
                    new (context) ReferenceType(std::move(type), ReferenceType::Kind::RValue, source));
          }
          else
          {
               auto [isConst, isVolatile] = parseCV();
               if (isConst || isVolatile)
               {
                    if (auto* const basicType = dynamic_cast<BasicType*>(type.get()); basicType != nullptr)
                    {
                         if (isConst) basicType->SetConst(true);
                         if (isVolatile) basicType->SetVolatile(true);
                    }
                    else if (auto* const pointerType = dynamic_cast<PointerType*>(type.get()); pointerType != nullptr)
                    {
                         if (isConst) pointerType->SetConst(true);
                         if (isVolatile) pointerType->SetVolatile(true);
                    }
                    else if (auto* const classType = dynamic_cast<ClassType*>(type.get()); classType != nullptr)
                    {
                         if (isConst) classType->SetConst(true);
                         if (isVolatile) classType->SetVolatile(true);
                    }
               }
               else
                    break;
          }
     }

     return ASTExpected{std::move(type)};
}

NodePointer ecpps::ast::AST::ParseDeclaration(ASTContext& context)
{
     // TODO: special-declaration

     const auto diagnosticsBefore = this->_diagnostics.get().diagnosticsList.size();
     auto node = ParseNameDeclaration(context);

     if (node == nullptr && this->_diagnostics.get().diagnosticsList.size() > diagnosticsBefore) SynchroniseMember();

     return node;
}

NodePointer ecpps::ast::AST::ParseBlockDeclaration(ASTContext& context)
{
     Location source = Peek().location;

     if (Match(TokenType::SemiColon)) return nullptr; // empty-declaration
     if (Peek().type == TokenType::Keyword)
     {
          runtime_assert(std::holds_alternative<std::string>(Peek().value), "Keyword was not an identifier");
          const auto& keyword = std::get<std::string>(Peek().value);

          if (keyword == "namespace")
          {
               Advance();
               auto name = ParseIdentifier(context);

               if (name != nullptr && Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "=")
               {
                    Advance();
                    SBOVector<std::unique_ptr<IdentifierNode, ecpps::ast::ASTDeleter>> aliased{};
                    while (!AtEnd())
                    {
                         auto nested = ParseIdentifier(context);
                         if (nested == nullptr) return nullptr; // TODO: Error
                         aliased.Push(std::move(nested));
                         if (Peek().type != TokenType::Operator || std::get<std::string>(Peek().value) != "::") break;
                         Advance(); // eat ::
                    }
                    if (!Match(TokenType::SemiColon))
                    {
                         // TODO: Error
                         return nullptr;
                    }
                    source.endPosition = Peek(-1).location.endPosition;
                    return std::unique_ptr<NamespaceAliasNode, ecpps::ast::ASTDeleter>(
                         new (context) NamespaceAliasNode(std::move(name), std::move(aliased), source));
               }
               std::vector<std::pair<std::unique_ptr<IdentifierNode, ecpps::ast::ASTDeleter>, bool>> nestedNames;
               nestedNames.emplace_back(std::move(name), false);

               while (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "::")
               {
                    Advance(); // eat ::
                    auto nestedName = ParseIdentifier(context);
                    if (nestedName == nullptr)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Expected identifier in nested namespace declaration", Peek().location));
                         return nullptr;
                    }
                    nestedNames.emplace_back(std::move(nestedName), false);
               }

               if (Peek().type == TokenType::LeftBrace)
               {
                    Advance(); // {
                    SBOVector<NodePointer> declarations{};

                    while (!AtEnd() && Peek().type != TokenType::RightBrace)
                    {
                         const auto before = CurrentPosition();
                         auto decl = ParseDeclaration(context);
                         if (decl != nullptr) declarations.Push(std::move(decl));
                         else if (CurrentPosition() == before)
                              Advance();
                    }

                    if (!Match(TokenType::RightBrace))
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Expected '}' at end of namespace definition", Peek().location));
                         return nullptr;
                    }

                    source.endPosition = Peek(-1).location.endPosition;

                    auto back = std::move(nestedNames.back());
                    nestedNames.pop_back();
                    std::unique_ptr<NamespaceNode, ecpps::ast::ASTDeleter> currentNode =
                         std::unique_ptr<NamespaceNode, ecpps::ast::ASTDeleter>(
                              new (context) NamespaceNode(std::move(back.first), std::move(declarations), source));
                    for (auto& nestedName : std::ranges::reverse_view(nestedNames) | std::views::keys)
                    {
                         SBOVector<NodePointer> decls{};
                         decls.Push(std::move(currentNode));
                         currentNode = std::unique_ptr<NamespaceNode, ecpps::ast::ASTDeleter>(
                              new (context) NamespaceNode(std::move(nestedName), std::move(decls), source));
                    }
                    return currentNode;
               }
          }
          else if (keyword == "using")
          {
               Advance(); // using

               auto aliasName = ParseIdentifier(context);
               if (aliasName == nullptr)
               {
                    this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                         "Expected identifier after 'using'", Peek().location));
                    return nullptr;
               }

               if (Peek().type != TokenType::Operator || std::get<std::string>(Peek().value) != "=")
               {
                    this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                         "Expected '=' in using alias-declaration", Peek().location));
                    return nullptr;
               }
               Advance(); // =

               NodePointer targetType = nullptr;
               SBOVector<QualifiedType::Section> sections;
               bool nextConst = false;
               bool nextVolatile = false;

               while (Peek().type == TokenType::Keyword)
               {
                    const auto& qual = std::get<std::string>(Peek().value);
                    if (qual == "const")
                    {
                         nextConst = true;
                         Advance();
                    }
                    else if (qual == "volatile")
                    {
                         nextVolatile = true;
                         Advance();
                    }
                    else
                         break;
               }

               if (Peek().type == TokenType::Keyword && SimpleTypes.contains(std::get<std::string>(Peek().value)))
               {
                    std::vector<std::string> typeWords;
                    while (Peek().type == TokenType::Keyword &&
                           SimpleTypes.contains(std::get<std::string>(Peek().value)))
                    {
                         typeWords.push_back(std::get<std::string>(Peek().value));
                         Advance();
                    }

                    std::string combinedType = CombineTypeWords(typeWords);

                    targetType = std::unique_ptr<BasicType, ecpps::ast::ASTDeleter>(
                         new (context) BasicType(combinedType, source, nextConst, nextVolatile));
               }
               else if (Peek().type == TokenType::Identifier)
               {
                    while (true)
                    {
                         NodePointer part = std::unique_ptr<BasicType, ecpps::ast::ASTDeleter>(new (context) BasicType(
                              std::get<std::string>(Peek().value), Peek().location, nextConst, nextVolatile));
                         Advance();
                         nextConst = false;
                         nextVolatile = false;

                         bool isTemplate = false;
                         if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "<")
                         {
                              // TODO: Implement template arguments parsing
                              part = std::unique_ptr<QualifiedType, ecpps::ast::ASTDeleter>(new (context) QualifiedType(
                                   SBOVector<QualifiedType::Section>{}, std::move(part), Peek().location));
                              isTemplate = true;
                         }

                         if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "::")
                         {
                              Advance();
                              sections.EmplaceBack(std::move(part), isTemplate);
                         }
                         else
                         {
                              targetType = std::unique_ptr<QualifiedType, ecpps::ast::ASTDeleter>(
                                   new (context) QualifiedType(std::move(sections), std::move(part), Peek().location));
                              break;
                         }
                    }
               }
               else
               {
                    this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                         "Expected type-id after '=' in using alias-declaration", Peek().location));
                    return nullptr;
               }

               while (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "*")
               {
                    Advance();

                    // TODO: cv qualifiers
                    while (Peek().type == TokenType::Keyword)
                    {
                         const auto& qual = std::get<std::string>(Peek().value);
                         if (qual == "const" || qual == "volatile")
                         {
                              Advance();
                         }
                         else
                              break;
                    }

                    targetType = std::unique_ptr<PointerType, ecpps::ast::ASTDeleter>(
                         new (context) PointerType(std::move(targetType), source));
               }

               if (!Match(TokenType::SemiColon))
               {
                    this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                         "Expected ';' at end of using alias-declaration", Peek().location));
                    return nullptr;
               }

               source.endPosition = Peek(-1).location.endPosition;
               return std::unique_ptr<TypeAliasNode, ecpps::ast::ASTDeleter>(
                    new (context) TypeAliasNode(std::move(aliasName), std::move(targetType), source));
          }
     }

     return ParseSimpleDeclaration(context);
}

NodePointer ecpps::ast::AST::ParseNameDeclaration(ASTContext& context)
{
     if (Match(TokenType::SemiColon)) return nullptr; // empty-declaration

     if (Peek().type == TokenType::Keyword && std::holds_alternative<std::string>(Peek().value))
     {
          const auto& keyword = std::get<std::string>(Peek().value);

          if (keyword == "using" || keyword == "namespace")
          {
               return ParseBlockDeclaration(context);
          }

          if (keyword == "typedef" || keyword == "friend" || keyword == "thread_local" || keyword == "mutable" ||
              keyword == "class" || keyword == "struct" || keyword == "union")
               return ParseSimpleDeclaration(context);
     }

     return ParseFunctionDefinition(context);
}
NodePointer ecpps::ast::AST::ParseClassDeclaration(ASTContext& context, bool isConst, bool isVolatile)
{
     auto source = Peek().location;

     runtime_assert(Peek().type == TokenType::Keyword, "Cannot parse a non-keyword as a class-key");
     const auto& classKeyString = Peek().AsKeyword();
     Advance();
     ClassKey key{};
     if (classKeyString == "class") key = ClassKey::Class;
     else if (classKeyString == "struct")
          key = ClassKey::Struct;
     else if (classKeyString == "union")
          key = ClassKey::Union;
     // TODO: Attributes
     if (!Match(TokenType::Identifier))
     {
          this->_diagnostics.get().diagnosticsList.push_back(
               std::make_unique<diagnostics::SyntaxError>("Expected a class name", Peek().location));
          return nullptr; // TODO: Attempt to recover
     }
     const auto& className = Peek(-1).AsIdentifier();
     if (!Match(TokenType::LeftBrace))
     {
          source.endPosition = Peek().location.endPosition;

          return std::unique_ptr<ClassType, ecpps::ast::ASTDeleter>(
               new (context) ClassType(className, key, isConst, isVolatile, source));
     }
     std::vector<NodePointer> body{};
     while (!Match(TokenType::RightBrace)) body.emplace_back(ParseMemberSpecification(context));
     if (Peek().type != TokenType::SemiColon)
          this->_diagnostics.get().diagnosticsList.push_back(
               std::make_unique<diagnostics::SyntaxError>("Not implemented", Peek().location));

     return std::unique_ptr<ClassDefNode, ecpps::ast::ASTDeleter>(
          new (context) ClassDefNode(className, key, std::move(body), isConst, isVolatile, source));
}

NodePointer ecpps::ast::AST::ParseFunctionDefinition(ASTContext& context)
{
     const auto declarationStart = CurrentPosition();
     const auto diagnosticsStart = this->_diagnostics.get().diagnosticsList.size();
     auto source = Peek().location;

     if (Peek().type == TokenType::Keyword && std::holds_alternative<std::string>(Peek().value))
     {
          const auto& keyword = std::get<std::string>(Peek().value);
          if (keyword == "using" || keyword == "typedef" || keyword == "namespace")
          {
               if (keyword == "using" || keyword == "namespace") return ParseBlockDeclaration(context);

               return ParseSimpleDeclaration(context);
          }
     }

     const auto fallBackToSimpleDeclaration = [&]() -> NodePointer
     {
          auto& list = this->_diagnostics.get().diagnosticsList;
          list.erase(list.begin() + static_cast<std::ptrdiff_t>(diagnosticsStart), list.end());
          SetPosition(declarationStart);
          return ParseSimpleDeclaration(context);
     };

     auto parsedAttributes = ParseAttributeSpecifierSeq(context);
     SBOVector<std::unique_ptr<AttributeNode, ASTDeleter>> attributes{};
     attributes.Reserve(parsedAttributes.size());
     for (auto& attribute : parsedAttributes) attributes.Push(std::move(attribute));

     bool isExtern = false;
     std::optional<std::string> externOptional = std::nullopt;
     while (Peek().type == TokenType::Keyword)
     {
          const auto& keyword = std::get<std::string>(Peek().value);
          if (keyword != "extern") break;

          Advance();
          isExtern = true;
          if (Peek().type == TokenType::Literal && std::holds_alternative<StringLiteral>(Peek().value))
          {
               externOptional = std::get<StringLiteral>(Peek().value).value;
               Advance();
          }
     }

     auto specifiers = ParseDeclSpecifiers(context);
     if (specifiers.definingTypeSpecifier == nullptr) return fallBackToSimpleDeclaration();

     abi::CallingConventionName callingConvention = abi::ABI::Current().DefaultCallingConventionName();
     if (Peek().type == TokenType::Keyword && std::get<std::string>(Peek().value) == "__mscall")
     {
          Advance();
          callingConvention = abi::CallingConventionName::Microsoftx64;
     }

     auto declarator = ParseDeclarator(context);
     if (!declarator.has_value()) return nullptr; // already reported; ParseDeclaration resynchronises
     if (!declarator->IsFunction()) return fallBackToSimpleDeclaration();

     if (declarator->nested != nullptr)
     {
          this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
               "Parenthesised declarators in function definitions are not yet supported", declarator->location));
          return nullptr;
     }

     auto& suffix = std::get<ParametersAndQualifiers>(declarator->suffixes.front());
     if (suffix.isConst || suffix.isVolatile || suffix.refQualifier != RefQualifier::None)
          this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
               "A non-member function cannot have cv-qualifiers or a ref-qualifier", suffix.location));

     NodePointer type = std::move(specifiers.definingTypeSpecifier);
     if (suffix.trailingReturnType != nullptr) type = std::move(suffix.trailingReturnType);
     for (auto& op : declarator->ptrOperators)
     {
          switch (op.kind)
          {
          case PtrOperator::Kind::Pointer:
               type = std::unique_ptr<PointerType, ASTDeleter>(
                    new (context) PointerType(std::move(type), op.isConst, op.isVolatile, op.location));
               break;
          case PtrOperator::Kind::LValueReference:
               type = std::unique_ptr<ReferenceType, ASTDeleter>(
                    new (context) ReferenceType(std::move(type), ReferenceType::Kind::LValue, op.location));
               break;
          case PtrOperator::Kind::RValueReference:
               type = std::unique_ptr<ReferenceType, ASTDeleter>(
                    new (context) ReferenceType(std::move(type), ReferenceType::Kind::RValue, op.location));
               break;
          case PtrOperator::Kind::PointerToMember:
               this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                    "Pointer-to-member return types are not yet supported", op.location));
               return nullptr;
          }
     }

     for (auto& attribute : declarator->attributes) attributes.Push(std::move(attribute));

     FunctionSignature signature{std::move(type),       specifiers.isFriend,
                                 specifiers.isInline,   specifiers.constexprSpecifier,
                                 std::move(attributes), std::move(declarator->declaredIdExpression),
                                 callingConvention};
     signature.isExtern = isExtern || (specifiers.storageSpecifier & StorageClassSpecifier::Extern);
     signature.externOptional = externOptional;
     signature.storageClassSpecifier = specifiers.storageSpecifier;
     signature.parameters.parameters = std::move(suffix.parameters);
     signature.isVariadic = suffix.isVariadic;
     signature.noexceptSpecifier = std::move(suffix.noexceptSpecifier);

     bool isOverride = false;
     bool isFinal = false;
     const auto virtLocation = Peek().location;
     ParseVirtSpecifierSeq(isOverride, isFinal);
     if (isOverride || isFinal)
          this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
               "'override' and 'final' can only be used on virtual member functions", virtLocation));

     if (Match(TokenType::SemiColon))
     {
          source.endPosition = Peek(-1).location.endPosition;
          return std::unique_ptr<FunctionDeclarationNode, ASTDeleter>(
               new (context) FunctionDeclarationNode(std::move(signature), source));
     }
     if (!Match(TokenType::LeftBrace))
     {
          this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
               "Expected a function body or ';' after the function declarator", Peek().location));
          return nullptr;
     }

     std::vector<NodePointer> statements{};
     ParseBlockBody(context, statements);
     SBOVector<NodePointer> body{};
     body.Reserve(statements.size());
     for (auto& statement : statements) body.Push(std::move(statement));

     source.endPosition = Peek(-1).location.endPosition;
     return std::unique_ptr<FunctionDefinitionNode, ASTDeleter>(
          new (context) FunctionDefinitionNode(std::move(signature), std::move(body), source));
}

NodePointer ecpps::ast::AST::ParseMemberSpecification(ASTContext& context)
{
     if (Peek().type == TokenType::Keyword)
     {
          const auto& keyword = Peek().AsKeyword();
          if ((keyword == "public" || keyword == "private" || keyword == "protected") &&
              Peek(1).type == TokenType::Colon)
          {
               auto source = Peek().location;
               Advance();
               Advance();

               AccessSpecifier specifier{};
               if (keyword == "public") specifier = AccessSpecifier::Public;
               else if (keyword == "private")
                    specifier = AccessSpecifier::Private;
               else if (keyword == "protected")
                    specifier = AccessSpecifier::Protected;

               source.endPosition = Peek(-1).location.endPosition;
               return std::unique_ptr<AccessSpecifierNode, ecpps::ast::ASTDeleter>(
                    new (context) AccessSpecifierNode(specifier, source));
          }
     }

     return ParseMemberDeclaration(context);
}
NodePointer ecpps::ast::AST::ParseMemberDeclaration(ASTContext& context)
{
     /*
     [*] attribute-specifier-seqopt decl-specifier-seqopt member-declarator-listopt ;
     function-definition
     [*] using-declaration
     [*] using-enum-declaration
     static_assert-declaration
     template-declaration
     explicit-specialization
     deduction-guide
     [*] alias-declaration
     opaque-enum-declaration
     [*] empty-declaration
*/
     if (Match(TokenType::SemiColon)) return nullptr;
     if (Peek().type == TokenType::Keyword)
     {
          const auto& keyword = Peek().AsKeyword();
          if (keyword == "using")
          {
               if (Peek(1).type == TokenType::Keyword && Peek(1).AsKeyword() == "namespace")
               {
                    this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                         "using-directive cannot appear in this context", Peek().location));

                    return nullptr;
               }
               return ParseBlockDeclaration(context);
          }
     }

     auto source = Peek().location;

     auto attributes = ParseAttributeSpecifierSeq(context);
     auto declSpecifiers = ParseDeclSpecifiers(context);

     std::vector<MemberDeclarator> members{};
     if (Peek().type != TokenType::SemiColon)
     {
          members = ParseMemberDeclarators(context);
          if (members.empty()) // error already reported
          {
               SynchroniseMember();
               return nullptr;
          }
     }

     const bool endsWithFunctionBody = !members.empty() && members.back().functionBody == FunctionBodyKind::Block;

     if (endsWithFunctionBody)
     {
          if (members.size() > 1)
          {
               this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                    "A function definition cannot appear in a member-declarator-list", source));
               return nullptr;
          }
     }
     else if (!Match(TokenType::SemiColon))
     {
          this->_diagnostics.get().diagnosticsList.push_back(
               std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
          SynchroniseMember();
          return nullptr;
     }

     source.endPosition = Peek(-1).location.endPosition;
     return std::unique_ptr<MemberDeclarationNode, ASTDeleter>(new (context) MemberDeclarationNode(
          std::move(attributes), std::move(declSpecifiers), std::move(members), source));
}

bool ecpps::ast::AST::IsDeclarationStart([[maybe_unused]] ASTContext& context)
{
     std::ptrdiff_t i = 0;
     bool sawSpecifier = false;

     while (CurrentPosition() + static_cast<std::size_t>(i) < this->_tokens.size() &&
            Peek(i).type == TokenType::Keyword)
     {
          const auto& kw = std::get<std::string>(Peek(i).value);
          if (SimpleTypes.contains(kw) || kw == "struct" || kw == "class" || kw == "union" || kw == "typedef" ||
              kw == "using" || kw == "namespace")
               return true;
          if (kw != "const" && kw != "volatile" && kw != "extern" && kw != "static" && kw != "inline" &&
              kw != "constexpr" && kw != "consteval" && kw != "constinit" && kw != "thread_local" && kw != "mutable")
               break;
          sawSpecifier = true;
          ++i;
     }
     if (sawSpecifier) return true;
     if (Peek(i).type != TokenType::Identifier) return false;

     while (IsOperatorToken(Peek(i + 1), "::") && Peek(i + 2).type == TokenType::Identifier) i += 2; // a::b::c

     std::ptrdiff_t j = i + 1;
     if (Peek(j).type == TokenType::Identifier) return true; // Foo x
     while (CurrentPosition() + static_cast<std::size_t>(j) < this->_tokens.size() &&
            (IsOperatorToken(Peek(j), "*") || IsOperatorToken(Peek(j), "&") || IsOperatorToken(Peek(j), "&&")))
          j++;
     return j > i + 1 && Peek(j).type == TokenType::Identifier; // Foo* x, Foo& x
}

std::string CombineTypeWords(const std::vector<std::string>& words)
{
     std::string result;
     for (const auto& word : words)
     {
          if (!result.empty()) result += " ";
          result += word;
     }
     return result;
}
std::tuple<NodePointer, ecpps::ast::InitialisationType> ecpps::ast::AST::ParseInitialiser(ASTContext& context)
{
     InitialisationType initialisationType = InitialisationType::Default;
     NodePointer initialiser = nullptr;
     if (IsOperatorToken(Peek(), "="))
     {
          initialisationType = InitialisationType::EqualInitialiser;
          Advance();
          initialiser = ParseInitialiserClause(context);
          if (!initialiser) return {nullptr, InitialisationType::Error};
     }
     else if (Match(TokenType::LeftParenthesis))
     {
          initialisationType = InitialisationType::ParenthesisedExpressionList;
          if (Match(TokenType::RightParenthesis)) return {nullptr, initialisationType};
          initialiser = ParseExpression(context);
          if (!initialiser) return {nullptr, InitialisationType::Error};
          if (!Match(TokenType::RightParenthesis))
          {
               this->_diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::SyntaxError>("Expected )", Peek().location));
          }
     }
     else if (Peek().type == TokenType::LeftBrace) // braced-init-list
     {
          initialisationType = InitialisationType::BracedInitialiser;
          initialiser = ParseInitialiserList(context);
     }

     return {std::move(initialiser), initialisationType};
}
NodePointer ecpps::ast::AST::ParseInitialiserList(ASTContext& context)
{
     if (!Match(TokenType::LeftBrace))
     {
          this->_diagnostics.get().diagnosticsList.push_back(
               std::make_unique<diagnostics::SyntaxError>("Expected {", Peek().location));
          return nullptr;
     }

     std::vector<NodePointer> initialisers{};
     // TODO: designated initialisers
     while (!AtEnd() && Peek().type != TokenType::RightBrace)
     {
          auto initialiser = ParseInitialiserClause(context);
          initialisers.push_back(std::move(initialiser));
          if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == ",")
          {
               Advance();
          }
          else
               break;
     }
     if (!Match(TokenType::RightBrace))
     {
          this->_diagnostics.get().diagnosticsList.push_back(
               std::make_unique<diagnostics::SyntaxError>("Expected }", Peek().location));
          return nullptr;
     }
     return std::unique_ptr<InitialiserListNode, ecpps::ast::ASTDeleter>(
          new (context) InitialiserListNode(std::move(initialisers), Peek(-1).location));
}
NodePointer ecpps::ast::AST::ParseInitialiserClause(ASTContext& context)
{
     return Peek().type == TokenType::LeftBrace ? ParseInitialiserList(context) : ParseAssignmentExpression(context);
}
ecpps::ast::DeclSpecifierSequence ecpps::ast::AST::ParseDeclSpecifiers(ASTContext& context)
{
     Location source = Peek().location;

     bool isFriend = false;
     bool isTypedef = false;
     bool isConstexpr = false;
     bool isConsteval = false;
     bool isConstinit = false;
     bool isInline = false;
     bool isVirtual = false;
     bool isStatic = false;
     bool isThreadLocal = false;
     bool isExtern = false;
     bool isMutable = false;
     std::optional<NodePointer> optExplicitSpecifier = std::nullopt;

     NodePointer typeSpecifier = nullptr;
     SBOVector<QualifiedType::Section> sections;
     bool nextConst = false;
     bool nextVolatile = false;

     while (true)
     {
          if (Peek().type == TokenType::Keyword)
          {
               auto& keyword = std::get<std::string>(Peek().value);
               if (keyword == "friend")
               {
                    if (isFriend)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'friend' specifier", Peek().location));
                    }
                    isFriend = true;
               }
               else if (keyword == "typedef")
               {
                    if (isTypedef)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'typedef' specifier", Peek().location));
                    }
                    isTypedef = true;
               }
               else if (keyword == "virtual")
               {
                    if (isVirtual)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'virtual' specifier", Peek().location));
                    }
                    isVirtual = true;
               }
               else if (keyword == "constexpr")
               {
                    if (isConstexpr)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'constexpr' specifier", Peek().location));
                    }
                    isConstexpr = true;
               }
               else if (keyword == "consteval")
               {
                    if (isConsteval)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'consteval' specifier", Peek().location));
                    }
                    isConsteval = true;
               }
               else if (keyword == "constinit")
               {
                    if (isConstinit)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'constinit' specifier", Peek().location));
                    }
                    isConstinit = true;
               }
               else if (keyword == "inline")
               {
                    if (isInline)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'inline' specifier", Peek().location));
                    }
                    isInline = true;
               }
               else if (keyword == "static")
               {
                    if (isStatic)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'static' specifier", Peek().location));
                    }
                    isStatic = true;
               }
               else if (keyword == "thread_local")
               {
                    if (isThreadLocal)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'thread_local' specifier", Peek().location));
                    }
                    isThreadLocal = true;
               }
               else if (keyword == "extern")
               {
                    if (isExtern)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'extern' specifier", Peek().location));
                    }
                    isExtern = true;
               }
               else if (keyword == "mutable")
               {
                    if (isMutable)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'mutable' specifier", Peek().location));
                    }
                    isMutable = true;
               }
               else if (keyword == "explicit") // explicit-specifier
               {
                    if (optExplicitSpecifier.has_value())
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'explicit' specifier", Peek().location));
                    }
                    Advance();
                    NodePointer condition = nullptr;
                    if (Match(TokenType::LeftParenthesis))
                    {
                         condition = ParseConditionalExpression(context);
                         if (!Match(TokenType::RightParenthesis))
                              this->_diagnostics.get().diagnosticsList.push_back(
                                   std::make_unique<diagnostics::SyntaxError>("Expected ')' after explicit(...)",
                                                                              Peek().location));
                    }
                    optExplicitSpecifier = std::move(condition);
                    continue;
               }
               else if (keyword == "const") // cv-qualifier
               {
                    if (nextConst)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'const' specifier", Peek().location));
                    }
                    nextConst = true;
               }
               else if (keyword == "volatile") // cv-qualifier
               {
                    if (nextVolatile)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Duplicate 'volatile' specifier", Peek().location));
                    }
                    nextVolatile = true;
               }
               else if (keyword == "struct" || keyword == "class" ||
                        keyword == "union") // elaborated-type-specifier & defining-type-specifier
               {
                    if (typeSpecifier != nullptr)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                              "Cannot declare a class in this context", Peek().location));
                    }
                    const auto isConst = std::exchange(nextConst, false);
                    const auto isVolatile = std::exchange(nextVolatile, false);
                    typeSpecifier = ParseClassDeclaration(context, isConst, isVolatile);
                    continue;
               }
               else if (keyword == "enum") // elaborated-type-specifier
               {
               }
               else if (SimpleTypes.contains(keyword)) // simple-type-specifier
               {
                    std::vector<std::string> typeWords;
                    while (Peek().type == TokenType::Keyword &&
                           SimpleTypes.contains(std::get<std::string>(Peek().value)))
                    {
                         typeWords.push_back(std::get<std::string>(Peek().value));
                         Advance();
                    }

                    std::string combinedType = CombineTypeWords(typeWords);

                    // Parse any pointer declarators immediately following the base type
                    std::size_t pointerLevel = 0;
                    while (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "*")
                    {
                         ++pointerLevel;
                         Advance();
                    }

                    // Build the base type
                    NodePointer base = std::unique_ptr<BasicType, ecpps::ast::ASTDeleter>(new (context) BasicType(
                         combinedType, source, std::exchange(nextConst, false), std::exchange(nextVolatile, false)));

                    // Wrap it in PointerType layers
                    for (std::size_t i = 0; i < pointerLevel; i++)
                    {
                         base = std::unique_ptr<PointerType, ecpps::ast::ASTDeleter>(
                              new (context) PointerType(std::move(base), source));
                    }

                    typeSpecifier = std::move(base);
                    break;
               }
               else
                    break;

               Advance();
          }
          else if (Peek().type == TokenType::Identifier)
          {
               if (typeSpecifier != nullptr) break;
               if (Peek(1).type == TokenType::LeftParenthesis) break;
               while (true)
               {
                    NodePointer part = std::unique_ptr<BasicType, ecpps::ast::ASTDeleter>(new (context) BasicType(
                         std::get<std::string>(Peek().value), Peek().location, nextConst, nextVolatile));
                    Advance();
                    nextConst = false;
                    nextVolatile = false;

                    bool isTemplate = false;
                    if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "<")
                    {
                         // TODO: Implement
                         part = std::unique_ptr<QualifiedType, ecpps::ast::ASTDeleter>(new (context) QualifiedType(
                              SBOVector<QualifiedType::Section>{}, std::move(part), Peek().location));
                         isTemplate = true;
                    }

                    if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "::")
                    {
                         Advance();
                         sections.EmplaceBack(std::move(part), isTemplate);
                    }
                    else
                    {
                         typeSpecifier = std::unique_ptr<QualifiedType, ecpps::ast::ASTDeleter>(
                              new (context) QualifiedType(std::move(sections), std::move(part), Peek().location));
                         break;
                    }
               }

               std::size_t pointerLevel = 0;
               while (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "*")
               {
                    pointerLevel++;
                    Advance();
               }

               for (std::size_t i = 0; i < pointerLevel; i++)
               {
                    typeSpecifier = std::unique_ptr<PointerType, ecpps::ast::ASTDeleter>(
                         new (context) PointerType(std::move(typeSpecifier), source));
               }

               break;
          }
          else
               break;
     }
     ecpps::ast::ConstantExpressionSpecifier constexprSpec{};

     if (isConstexpr) constexprSpec = ecpps::ast::ConstantExpressionSpecifier::Constexpr;
     else if (isConsteval)
          constexprSpec = ecpps::ast::ConstantExpressionSpecifier::Consteval;
     else if (isConstinit)
          constexprSpec = ecpps::ast::ConstantExpressionSpecifier::Constinit;

     ecpps::ast::StorageClassSpecifier storageSpecifier = ecpps::ast::StorageClassSpecifier::None;
     if (isStatic) storageSpecifier |= ecpps::ast::StorageClassSpecifier::Static;
     if (isThreadLocal) storageSpecifier |= ecpps::ast::StorageClassSpecifier::ThreadLocal;
     if (isExtern) storageSpecifier |= ecpps::ast::StorageClassSpecifier::Extern;
     if (isMutable) storageSpecifier |= ecpps::ast::StorageClassSpecifier::Mutable;

     return ecpps::ast::DeclSpecifierSequence{
          .isFriend = isFriend,
          .isTypedef = isTypedef,
          .constexprSpecifier = constexprSpec,
          .isInline = isInline,
          .storageSpecifier = storageSpecifier,
          .definingTypeSpecifier = std::move(typeSpecifier),
          .functionSpecifier =
               FunctionSpecifier{.isVirtual = isVirtual, .explicitSpecifier = std::move(optExplicitSpecifier)}};
}

void ecpps::ast::AST::SynchroniseMember(void)
{
     int braceDepth = 0;
     while (!AtEnd())
     {
          const auto type = Peek().type;
          if (type == TokenType::LeftBrace) braceDepth++;
          else if (type == TokenType::RightBrace)
          {
               if (braceDepth == 0) return;
               if (--braceDepth == 0)
               {
                    Advance();
                    return;
               }
          }
          else if (type == TokenType::SemiColon && braceDepth == 0)
          {
               Advance();
               return;
          }
          Advance();
     }
}

std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>> ecpps::ast::AST::
     ParseAttributeSpecifierSeq(ASTContext& context)
{
     std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>> attributes{};

     while (Peek().type == TokenType::LeftBracket && Peek(1).type == TokenType::LeftBracket)
     {
          Advance();
          Advance();

          while (!AtEnd() && Peek().type != TokenType::RightBracket)
          {
               auto attributeSource = Peek().location;
               if (Peek().type != TokenType::Identifier)
               {
                    this->_diagnostics.get().diagnosticsList.push_back(
                         std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
                    break;
               }

               std::string name = Peek().AsIdentifier();
               Advance();
               while (IsOperatorToken(Peek(), "::") && Peek(1).type == TokenType::Identifier) // scoped attribute
               {
                    Advance();
                    name += "::" + Peek().AsIdentifier();
                    Advance();
               }

               SBOVector<Token> arguments{};
               if (Match(TokenType::LeftParenthesis))
               {
                    int depth = 1;
                    while (!AtEnd())
                    {
                         if (Peek().type == TokenType::LeftParenthesis) ++depth;
                         else if (Peek().type == TokenType::RightParenthesis && --depth == 0)
                              break;
                         arguments.EmplaceBack(Peek());
                         Advance();
                    }
                    if (!Match(TokenType::RightParenthesis))

                         this->_diagnostics.get().diagnosticsList.push_back(
                              std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
               }

               attributeSource.endPosition = Peek(-1).location.endPosition;
               attributes.emplace_back(std::unique_ptr<AttributeNode, ASTDeleter>(
                    new (context) AttributeNode(name, arguments, attributeSource)));

               if (IsOperatorToken(Peek(), ",")) Advance();
               else
                    break;
          }

          if (!Match(TokenType::RightBracket) || !Match(TokenType::RightBracket))
               this->_diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
     }

     return attributes;
}

void ecpps::ast::AST::ParseCvQualifierSeq(bool& isConst, bool& isVolatile)
{
     while (Peek().type == TokenType::Keyword)
     {
          const auto& keyword = Peek().AsKeyword();
          if (keyword == "const")
          {
               if (isConst)
                    this->_diagnostics.get().diagnosticsList.push_back(
                         std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
               isConst = true;
          }
          else if (keyword == "volatile")
          {
               if (isVolatile)
                    this->_diagnostics.get().diagnosticsList.push_back(
                         std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
               isVolatile = true;
          }
          else
               break;
          Advance();
     }
}

std::vector<ecpps::ast::PtrOperator> ecpps::ast::AST::ParsePtrOperators(ASTContext& context)
{
     auto isPointerToMember = [this]
     {
          std::ptrdiff_t i = 0;
          if (IsOperatorToken(Peek(i), "::")) ++i;
          bool sawName = false;
          while (Peek(i).type == TokenType::Identifier && IsOperatorToken(Peek(i + 1), "::"))
          {
               i += 2;
               sawName = true;
          }
          return sawName && IsOperatorToken(Peek(i), "*");
     };

     std::vector<PtrOperator> operators{};

     while (!AtEnd())
     {
          PtrOperator op{};
          op.location = Peek().location;

          if (IsOperatorToken(Peek(), "*"))
          {
               Advance();
               op.kind = PtrOperator::Kind::Pointer;
               op.attributes = ParseAttributeSpecifierSeq(context);
               ParseCvQualifierSeq(op.isConst, op.isVolatile);
          }
          else if (IsOperatorToken(Peek(), "&"))
          {
               Advance();
               op.kind = PtrOperator::Kind::LValueReference;
               op.attributes = ParseAttributeSpecifierSeq(context);
          }
          else if (IsOperatorToken(Peek(), "&&"))
          {
               Advance();
               op.kind = PtrOperator::Kind::RValueReference;
               op.attributes = ParseAttributeSpecifierSeq(context);
          }
          else if (isPointerToMember())
          {
               auto source = Peek().location;
               std::vector<NodePointer> parts{};
               if (IsOperatorToken(Peek(), "::"))
               {
                    Advance();
                    parts.push_back(
                         std::unique_ptr<GlobalScopeNode, ASTDeleter>(new (context) GlobalScopeNode(source)));
               }
               while (Peek().type == TokenType::Identifier && IsOperatorToken(Peek(1), "::"))
               {
                    parts.push_back(std::unique_ptr<IdentifierNode, ASTDeleter>(
                         new (context) IdentifierNode(Peek().AsIdentifier(), Peek().location)));
                    Advance(); // identifier
                    Advance(); // ::
               }
               source.endPosition = Peek(-1).location.endPosition;

               if (parts.size() == 1) op.nestedNameSpecifier = std::move(parts.front());
               else
                    op.nestedNameSpecifier = std::unique_ptr<QualifiedIdNode, ASTDeleter>(
                         new (context) QualifiedIdNode(std::move(parts), source));

               Advance(); // *
               op.kind = PtrOperator::Kind::PointerToMember;
               op.attributes = ParseAttributeSpecifierSeq(context);
               ParseCvQualifierSeq(op.isConst, op.isVolatile);
          }
          else
               break;

          op.location.endPosition = Peek(-1).location.endPosition;
          operators.push_back(std::move(op));
     }

     return operators;
}

NodePointer ecpps::ast::AST::ParseDeclaratorId(ASTContext& context)
{
     const auto& token = Peek();

     if (token.type == TokenType::Identifier) return ParseIdExpression(context);

     if (token.type == TokenType::Operator)
     {
          if (IsOperatorToken(token, "::")) return ParseIdExpression(context);

          if (IsOperatorToken(token, "~") && Peek(1).type == TokenType::Identifier)
          {
               auto source = token.location;
               Advance(); // ~
               std::string name = "~" + Peek().AsIdentifier();
               source.endPosition = Peek().location.endPosition;
               Advance();
               return std::unique_ptr<IdentifierNode, ASTDeleter>(new (context)
                                                                       IdentifierNode(std::move(name), source));
          }
     }

     this->_diagnostics.get().diagnosticsList.push_back(
          std::make_unique<diagnostics::SyntaxError>("Expected a declarator-id", token.location));
     return nullptr;
}

std::optional<ecpps::ast::ParametersAndQualifiers> ecpps::ast::AST::ParseParametersAndQualifiers(ASTContext& context)
{
     ParametersAndQualifiers result{};
     result.location = Peek().location;

     if (!Match(TokenType::LeftParenthesis))
     {
          this->_diagnostics.get().diagnosticsList.push_back(
               std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
          return std::nullopt;
     }

     // parameter-declaration-clause
     if (!Match(TokenType::RightParenthesis))
     {
          while (!AtEnd())
          {
               if (IsOperatorToken(Peek(), "..."))
               {
                    Advance();
                    result.isVariadic = true;
                    break;
               }

               auto parameter = ParseFunctionParameter(context);
               if (parameter.type == nullptr) return std::nullopt; // error already reported
               result.parameters.push_back(std::move(parameter));

               if (IsOperatorToken(Peek(), ",")) Advance();
               else
                    break;
          }

          if (!Match(TokenType::RightParenthesis))
          {
               this->_diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
               return std::nullopt;
          }
     }

     ParseCvQualifierSeq(result.isConst, result.isVolatile);

     if (IsOperatorToken(Peek(), "&"))
     {
          result.refQualifier = RefQualifier::LValue;
          Advance();
     }
     else if (IsOperatorToken(Peek(), "&&"))
     {
          result.refQualifier = RefQualifier::RValue;
          Advance();
     }

     if (Peek().type == TokenType::Keyword && Peek().AsKeyword() == "noexcept")
     {
          Advance();
          result.noexceptSpecifier.isPresent = true;
          if (Match(TokenType::LeftParenthesis))
          {
               result.noexceptSpecifier.condition = ParseConditionalExpression(context);
               if (result.noexceptSpecifier.condition == nullptr)
               {
                    this->_diagnostics.get().diagnosticsList.push_back(
                         std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
                    return std::nullopt;
               }
               if (!Match(TokenType::RightParenthesis))
               {
                    this->_diagnostics.get().diagnosticsList.push_back(
                         std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
                    return std::nullopt;
               }
          }
     }

     result.attributes = ParseAttributeSpecifierSeq(context);

     if (IsOperatorToken(Peek(), "->"))
     {
          Advance();
          auto typeId = ParseTypeId(context);
          if (!typeId.WasSuccessful())
          {
               for (auto& diagnostic : typeId.diagnostics)
                    this->_diagnostics.get().diagnosticsList.push_back(std::move(diagnostic));
               return std::nullopt;
          }
          result.trailingReturnType = std::move(typeId.value);
     }

     result.location.endPosition = Peek(-1).location.endPosition;
     return std::optional<ParametersAndQualifiers>{std::move(result)};
}

std::optional<ecpps::ast::Declarator> ecpps::ast::AST::ParseDeclarator(ASTContext& context)
{
     Declarator declarator{};
     declarator.location = Peek().location;

     declarator.ptrOperators = ParsePtrOperators(context);

     if (Peek().type == TokenType::LeftParenthesis) // ( ptr-declarator )
     {
          Advance();
          auto inner = ParseDeclarator(context);
          if (!inner.has_value()) return std::nullopt;
          if (!Match(TokenType::RightParenthesis))
          {
               this->_diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
               return std::nullopt;
          }
          declarator.nested = std::make_unique<Declarator>(std::move(*inner));
     }
     else
     {
          if (IsOperatorToken(Peek(), "..."))
          {
               Advance();
               declarator.isParameterPack = true;
          }

          declarator.declaredIdExpression = ParseDeclaratorId(context);
          if (declarator.declaredIdExpression == nullptr) return std::nullopt; // error already reported

          declarator.attributes = ParseAttributeSpecifierSeq(context);
     }

     while (!AtEnd())
     {
          if (Peek().type == TokenType::LeftParenthesis)
          {
               auto parameters = ParseParametersAndQualifiers(context);
               if (!parameters.has_value()) return std::nullopt;
               declarator.suffixes.emplace_back(std::move(*parameters));
          }
          else if (Peek().type == TokenType::LeftBracket && Peek(1).type != TokenType::LeftBracket)
          {
               ArrayBound bound{.location = Location{0, 0, 0}};
               bound.location = Peek().location;
               Advance(); // [

               if (Peek().type != TokenType::RightBracket)
               {
                    bound.size = ParseConditionalExpression(context);
                    if (bound.size == nullptr)
                    {
                         this->_diagnostics.get().diagnosticsList.push_back(
                              std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
                         return std::nullopt;
                    }
               }
               if (!Match(TokenType::RightBracket))
               {
                    this->_diagnostics.get().diagnosticsList.push_back(
                         std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
                    return std::nullopt;
               }

               bound.attributes = ParseAttributeSpecifierSeq(context);
               bound.location.endPosition = Peek(-1).location.endPosition;
               declarator.suffixes.emplace_back(std::move(bound));
          }
          else
               break;
     }

     declarator.location.endPosition = Peek(-1).location.endPosition;
     return std::optional<Declarator>{std::move(declarator)};
}

std::optional<ecpps::ast::MemberDeclarator> ecpps::ast::AST::ParseMemberDeclarator(ASTContext& context)
{
     MemberDeclarator member{};

     const bool startsUnnamedBitField = Peek().type == TokenType::Colon || (Peek().type == TokenType::LeftBracket &&
                                                                            Peek(1).type == TokenType::LeftBracket);

     if (startsUnnamedBitField)
     {
          member.location = Peek().location;
          member.attributes = ParseAttributeSpecifierSeq(context);
          if (Peek().type != TokenType::Colon)
          {
               this->_diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
               return std::nullopt;
          }
     }
     else
     {
          auto declarator = ParseDeclarator(context);
          if (!declarator.has_value()) return std::nullopt;
          static_cast<Declarator&>(member) = std::move(*declarator);
     }

     const bool isFunction = member.IsFunction();

     if (Peek().type == TokenType::Colon)
     {
          if (isFunction)
          {
               // TODO: ctor-initialiser (`Foo() : a(1), b{2} {}`)
               this->_diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
               return std::nullopt;
          }

          Advance(); // :
          member.isBitField = true;
          member.bitFieldWidth = ParseConditionalExpression(context);
          if (member.bitFieldWidth == nullptr)
          {
               this->_diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
               return std::nullopt;
          }
     }
     else if (isFunction)
     {
          ParseVirtSpecifierSeq(member.isOverride, member.isFinal);

          if (IsOperatorToken(Peek(), "="))
          {
               const auto& next = Peek(1);
               if (next.type == TokenType::Literal && std::holds_alternative<IntegerLiteral>(next.value) &&
                   std::get<IntegerLiteral>(next.value).value == 0)
                    member.isPure = true;
               else if (IsWord(next, "default"))
                    member.functionBody = FunctionBodyKind::Defaulted;
               else if (IsWord(next, "delete"))
                    member.functionBody = FunctionBodyKind::Deleted;
               else
               {
                    this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                         "Expected '0', 'default' or 'delete' after '=' in a function declarator", next.location));
                    return std::nullopt;
               }
               Advance(); // =
               Advance(); // 0 | default | delete
               return std::optional<MemberDeclarator>{std::move(member)};
          }

          if (Peek().type == TokenType::LeftBrace)
          {
               Advance(); // {
               member.functionBody = FunctionBodyKind::Block;
               ParseBlockBody(context, member.body);
          }

          return std::optional<MemberDeclarator>{std::move(member)};
     }

     if (IsOperatorToken(Peek(), "="))
     {
          Advance();
          member.initialisationType = InitialisationType::EqualInitialiser;
          member.initialiser = ParseInitialiserClause(context);
          if (member.initialiser == nullptr)
          {
               this->_diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::SyntaxError>("syntax error", Peek().location));
               return std::nullopt;
          }
     }
     else if (Peek().type == TokenType::LeftBrace)
     {
          member.initialisationType = InitialisationType::BracedInitialiser;
          member.initialiser = ParseInitialiserList(context);
          if (member.initialiser == nullptr) return std::nullopt; // error already reported
     }

     return std::optional<MemberDeclarator>{std::move(member)};
}

std::vector<ecpps::ast::MemberDeclarator> ecpps::ast::AST::ParseMemberDeclarators(ASTContext& context)
{
     std::vector<MemberDeclarator> declarators{};

     // clang-format off
     do
     {
          // clang-format on
          auto declarator = ParseMemberDeclarator(context);
          if (!declarator.has_value())
          {
               declarators.clear();
               return declarators;
          }

          const bool hasBlockBody = declarator->functionBody == FunctionBodyKind::Block;
          declarators.push_back(std::move(*declarator));

          if (hasBlockBody) break;
     } while (IsOperatorToken(Peek(), ",") && (Advance(), true));

     return declarators;
}
NodePointer ecpps::ast::AST::ParseSimpleDeclaration(ASTContext& context)
{
     const auto start = CurrentPosition();
     Location source = Peek().location;

     auto [isFriend, isTypedef, constexprSpecifier, isInline, storageSpecifier, definingTypeSpecifier,
           functionSpecifier] = ParseDeclSpecifiers(context);
     auto [isVirtual, optExplicitSpecifier] = std::move(functionSpecifier);
     // note: definingTypeSpecifier is a bit misleading. this has to be addressed at some point

     if (definingTypeSpecifier == nullptr)
     {
          this->_diagnostics.get().diagnosticsList.push_back(
               std::make_unique<diagnostics::SyntaxError>("Expected a type specifier", Peek().location));
          if (CurrentPosition() == start) Advance();
          return nullptr;
     }
     if (Match(TokenType::SemiColon))
     {
          // TODO: Check if it's defining
          return std::move(definingTypeSpecifier);
     }

     using VariableDeclarator = VariableDeclarationNode::Declarator;

     std::vector<VariableDeclarator> declarators{};
     // clang-format off
     do
     {
          // clang-format on
          NodePointer id = ParseIdExpression(context);
          if (!id) return nullptr;

          std::vector<NodePointer> arrayLevels{};
          while (Peek().type == TokenType::LeftBracket)
          {
               Advance();

               if (Peek().type == TokenType::RightBracket)
               {
                    Advance();
                    arrayLevels.emplace_back(nullptr);
                    continue;
               }

               auto value = ParseExpression(context);
               arrayLevels.emplace_back(std::move(value));

               if (Peek().type != TokenType::RightBracket)
               {
                    this->_diagnostics.get().diagnosticsList.push_back(
                         std::make_unique<diagnostics::SyntaxError>("Expected ]", Peek().location));
                    break;
               }
               Advance();
          }

          auto [initialiser, initialisationType] = ParseInitialiser(context);

          declarators.emplace_back(std::move(id), initialisationType, std::move(initialiser), std::move(arrayLevels));
     } while ((Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == ",") && (Advance(), true));

     if (!Match(TokenType::SemiColon))
          this->_diagnostics.get().diagnosticsList.push_back(
               std::make_unique<diagnostics::SyntaxError>("Expected ';'", Peek(-1).location));

     return std::unique_ptr<VariableDeclarationNode, ecpps::ast::ASTDeleter>(new (context) VariableDeclarationNode(
          std::move(definingTypeSpecifier), std::move(declarators),
          /*flags*/
          VariableDeclarationNode::Flags{.isTypedef = isTypedef,
                                         .isFriend = isFriend,
                                         .constexprSpecifier = constexprSpecifier,
                                         .isInline = isInline,
                                         .storageClassSpecifier = storageSpecifier},
          std::move(optExplicitSpecifier), source));
}

NodePointer ecpps::ast::AST::TryParseDeclSpecifier([[maybe_unused]] ASTContext& context)
{
     return {};
}

NodePointer ecpps::ast::AST::TryParseDefiningTypeSpecifier([[maybe_unused]] ASTContext& context)
{
     return {};
}

NodePointer ecpps::ast::AST::ParseInitDeclarator([[maybe_unused]] ASTContext& context)
{
     return {};
}

NodePointer ecpps::ast::AST::TryParseDeclarator([[maybe_unused]] ASTContext& context)
{
     return {};
}

NodePointer ecpps::ast::AST::TryParsePtrDeclarator([[maybe_unused]] ASTContext& context)
{
     return {};
}

NodePointer ecpps::ast::AST::TryParseNoPtrDeclarator([[maybe_unused]] ASTContext& context)
{
     return {};
}

NodePointer ecpps::ast::AST::ParsePrimaryExpression([[maybe_unused]] ASTContext& context)
{
     if (this->AtEnd()) return nullptr;

     const auto& currentToken = this->Peek();

     switch (currentToken.type)
     {
     case TokenType::Literal:
     {
          this->Advance();
          return std::visit(
               OverloadedVisitor{[&currentToken, &context](const bool boolean) -> NodePointer
                                 {
                                      return std::unique_ptr<BooleanLiteralNode, ecpps::ast::ASTDeleter>(
                                           new (context) BooleanLiteralNode(boolean, currentToken.location));
                                 },
                                 [&currentToken, &context](const StringLiteral& literal) -> NodePointer
                                 {
                                      return std::unique_ptr<StringLiteralNode, ecpps::ast::ASTDeleter>(
                                           new (context) StringLiteralNode(literal.value, currentToken.location));
                                 },
                                 [&currentToken, &context](const IntegerLiteral& literal) -> NodePointer
                                 {
                                      return std::unique_ptr<IntegerLiteralNode, ecpps::ast::ASTDeleter>(
                                           new (context) IntegerLiteralNode(literal.value, currentToken.location));
                                 },
                                 [&currentToken, &context](const char literal) -> NodePointer
                                 {
                                      return std::unique_ptr<CharacterLiteralNode, ecpps::ast::ASTDeleter>(
                                           new (context) CharacterLiteralNode(literal, currentToken.location));
                                 },
                                 [](auto&&) static -> NodePointer
                                 {
                                      return nullptr;
                                 }},
               currentToken.value);
     }
     case TokenType::Keyword:
     {
          const auto& keyword = std::get<std::string>(currentToken.value);
          if (keyword == "this")
          {
               this->Advance();
               return std::unique_ptr<ThisNode, ecpps::ast::ASTDeleter>(new (context) ThisNode(currentToken.location));
          }
     }
     break;
     case TokenType::LeftParenthesis: // ( expression )
     {
          this->Advance();
          auto expression = this->ParseExpression(context);
          if (!Match(TokenType::RightParenthesis))
          {
               // TODO: Error
               return nullptr;
          }
          return expression;
     }
     case TokenType::Operator:
     {
          // ::
          if (std::get<std::string>(currentToken.value) == "::")
          {
               auto idExpr = ParseIdExpression(context);
               if (!idExpr)
               {
                    return nullptr;
               }
               return idExpr;
          }
          break;
     }
     case TokenType::Identifier:
     {
          return ParseIdExpression(context);
     }
     default:
          this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
               std::format("Expected a primary expression (literal, identifier, "
                           "'this', or parenthesized expression), "
                           "but found an unexpected token"),
               currentToken.location));
          Advance();
          return nullptr;
     }

     return nullptr;
}

NodePointer ecpps::ast::AST::ParseIdExpression([[maybe_unused]] ASTContext& context)
{
     const auto source = Peek().location;
     std::vector<NodePointer> parts;

     bool sawTemplateKeyword = false;
     bool isGlobalScope = false;

     if (!AtEnd() && Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "::")
     {
          Advance();
          isGlobalScope = true;
     }

     if (isGlobalScope)
     {
          parts.push_back(
               std::unique_ptr<GlobalScopeNode, ecpps::ast::ASTDeleter>(new (context) GlobalScopeNode(source)));
     }

     while (true)
     {
          if (!AtEnd() && Peek().type == TokenType::Keyword && std::get<std::string>(Peek().value) == "template")
          {
               sawTemplateKeyword = true;
               Advance();
          }
          if (AtEnd() || Peek().type != TokenType::Identifier)
          {
               this->_diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::SyntaxError>("Expected an identifier", Peek().location));
               return nullptr;
          }

          const auto& currentToken = Peek();
          const auto& identifierName = std::get<std::string>(currentToken.value);
          Advance();

          if (!AtEnd() && IsOperatorToken(Peek(), "<"))
          {
               const auto savedPosition = CurrentPosition();
               const auto savedDiagnostics = this->_diagnostics.get().diagnosticsList.size();
               bool isTemplateId = false;
               Advance(); // <
               while (!AtEnd())
               {
                    if (ParseSimpleTypeSpecifier(context) == nullptr || AtEnd()) break;
                    if (IsOperatorToken(Peek(), ","))
                    {
                         Advance();
                         continue;
                    }
                    if (IsOperatorToken(Peek(), ">"))
                    {
                         Advance();
                         isTemplateId = true;
                         break;
                    }
                    if (IsOperatorToken(Peek(), ">>")) // split
                    {
                         Peek().value = std::string(">");
                         isTemplateId = true;
                         break;
                    }
                    break;
               }
               if (!isTemplateId)
               {
                    SetPosition(savedPosition);
                    auto& list = this->_diagnostics.get().diagnosticsList;
                    list.erase(list.begin() + static_cast<std::ptrdiff_t>(savedDiagnostics), list.end());
               }
               else
               {
                    this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                         std::format("Template syntax is not yet supported: '{}<...>'", identifierName),
                         currentToken.location));
                    return nullptr;
               }
          }

          parts.push_back(std::unique_ptr<IdentifierNode, ecpps::ast::ASTDeleter>(
               new (context) IdentifierNode(identifierName, currentToken.location)));

          if (sawTemplateKeyword)
          {
               // TODO: Implement template keyword disambiguation
               this->_diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::SyntaxError>(std::format("The 'template' keyword for dependent name "
                                                                           "disambiguation is not yet supported"),
                                                               currentToken.location));
               return nullptr;
          }

          sawTemplateKeyword = false;

          if (AtEnd()) break;

          if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "::")
          {
               Advance();

               if (AtEnd() || Peek().type != TokenType::Identifier) // TODO: template & operator op
               {
                    this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                         "Expected an identifier after '::' in qualified-id", Peek().location));
                    return nullptr;
               }

               continue;
          }

          break;
     }

     if (parts.empty()) return nullptr;
     if (parts.size() == 1) return std::move(parts.front());
     return std::unique_ptr<QualifiedIdNode, ecpps::ast::ASTDeleter>(new (context)
                                                                          QualifiedIdNode(std::move(parts), source));
}

NodePointer ecpps::ast::AST::ParsePostfixExpresssion([[maybe_unused]] ASTContext& context)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     if (currentToken.type == TokenType::Keyword)
     {
          /*
           * TODO:
           * dynamic_cast < type-id > ( expression )
           * static_cast < type-id > ( expression )
           * reinterpret_cast < type-id > ( expression )
           * const_cast < type-id > ( expression )
           * typeid ( expression )
           * typeid ( type-id )
           */
     }

     NodePointer expression = nullptr;
     if (currentToken.type == TokenType::Keyword && SimpleTypes.contains(std::get<std::string>(currentToken.value)))
     {
          std::vector<std::string> typeWords;
          while (!AtEnd() && Peek().type == TokenType::Keyword &&
                 SimpleTypes.contains(std::get<std::string>(Peek().value)))
          {
               typeWords.push_back(std::get<std::string>(Peek().value));
               Advance();
          }
          source.endPosition = Peek(-1).location.endPosition;
          expression = std::unique_ptr<BasicType, ASTDeleter>(
               new (context) BasicType(CombineTypeWords(typeWords), source, false, false));
     }
     else
          expression = ParsePrimaryExpression(context);
     if (expression == nullptr) return nullptr;

     while (!AtEnd())
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               if (std::get<std::string>(currentToken.value) == "++" ||
                   std::get<std::string>(currentToken.value) == "--")
               {
                    Advance();
                    source.endPosition = currentToken.location.endPosition;
                    Operator operatorId =
                         std::get<std::string>(currentToken.value) == "++" ? Operator::Increment : Operator::Decrement;
                    expression = std::unique_ptr<UnaryOperatorNode, ecpps::ast::ASTDeleter>(
                         new (context)
                              UnaryOperatorNode(operatorId, std::move(expression), UnaryOperatorType::Postfix, source));
                    continue;
               }
               if (std::get<std::string>(currentToken.value) == "." ||
                   std::get<std::string>(currentToken.value) == "->")
               {
                    Advance();
                    // TODO: template
                    source.endPosition = currentToken.location.endPosition;
                    Operator operatorId =
                         std::get<std::string>(currentToken.value) == "." ? Operator::FullStop : Operator::Arrow;
                    expression =
                         std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                              std::move(expression), operatorId, ParseIdExpression(context), source));
                    continue;
               }
          }
          if (currentToken.type == TokenType::LeftBracket)
          {
               Advance();
               auto subscriptExpression = ParseExpression(context);
               if (!Match(TokenType::RightBracket))
               {
                    this->_diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                              "Expected a closing bracket in subscript expression", currentToken.location));
                    return nullptr;
               }
               source.endPosition = currentToken.location.endPosition;
               expression =
                    std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                         std::move(expression), Operator::Subscript, std::move(subscriptExpression), source));
               continue;
          }
          if (currentToken.type == TokenType::LeftParenthesis)
          {
               SBOVector<NodePointer> argumentList{}; // TODO: Initialiser lists; for now expressions only
               while (!AtEnd())
               {
                    Advance();
                    if (Match(TokenType::RightParenthesis)) break;
                    argumentList.Push(ParseAssignmentExpression(context));
                    if (AtEnd() || Match(TokenType::RightParenthesis)) break;
                    const auto& token = Peek();
                    if (token.type == TokenType::Operator && std::get<std::string>(token.value) == ",") continue;

                    this->_diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                              "Expected a comma in function argument list", token.location));
                    return nullptr;
               }

               source.endPosition = currentToken.location.endPosition;
               expression = std::unique_ptr<CallOperatorNode, ecpps::ast::ASTDeleter>(
                    new (context) CallOperatorNode(std::move(expression), std::move(argumentList), source));

               continue;
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseUnaryExpression(ASTContext& context)
{
     const auto& currentToken = this->Peek();
     if (currentToken.type == TokenType::Operator)
     {
          if (std::get<std::string>(currentToken.value) == "++" || std::get<std::string>(currentToken.value) == "--")
          {
               Advance();
               auto source = currentToken.location;
               auto expression = ParseCastExpression(context);
               source.endPosition = Peek().location.endPosition;
               const auto operatorId =
                    std::get<std::string>(currentToken.value) == "++" ? Operator::Increment : Operator::Decrement;
               return std::unique_ptr<UnaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) UnaryOperatorNode(
                    operatorId, std::move(expression), UnaryOperatorType::Prefix, source));
          }
     }
     if (currentToken.type == TokenType::Operator)
     {
          if (std::get<std::string>(currentToken.value) == "+" || std::get<std::string>(currentToken.value) == "-")
          {
               Advance();
               auto source = currentToken.location;
               auto expression = ParseCastExpression(context);
               source.endPosition = Peek().location.endPosition;
               const auto operatorId =
                    std::get<std::string>(currentToken.value) == "+" ? Operator::Plus : Operator::Minus;
               return std::unique_ptr<UnaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) UnaryOperatorNode(
                    operatorId, std::move(expression), UnaryOperatorType::Prefix, source));
          }
          if (std::get<std::string>(currentToken.value) == "*" || std::get<std::string>(currentToken.value) == "&")
          {
               Advance();
               auto source = currentToken.location;
               auto expression = ParseCastExpression(context);
               source.endPosition = Peek().location.endPosition;
               const auto operatorId =
                    std::get<std::string>(currentToken.value) == "*" ? Operator::Asterisk : Operator::Ampersand;
               return std::unique_ptr<UnaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) UnaryOperatorNode(
                    operatorId, std::move(expression), UnaryOperatorType::Prefix, source));
          }
          if (std::get<std::string>(currentToken.value) == "~" || std::get<std::string>(currentToken.value) == "!")
          {
               Advance();
               auto source = currentToken.location;
               auto expression = ParseCastExpression(context);
               source.endPosition = Peek().location.endPosition;
               const auto operatorId =
                    std::get<std::string>(currentToken.value) == "!" ? Operator::Exclamation : Operator::Tilde;
               return std::unique_ptr<UnaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) UnaryOperatorNode(
                    operatorId, std::move(expression), UnaryOperatorType::Prefix, source));
          }
     }
     // sizeof / alignof
     if (currentToken.type == TokenType::Keyword && (std::get<std::string>(currentToken.value) == "sizeof" ||
                                                     std::get<std::string>(currentToken.value) == "alignof"))
     {
          NodePointer typeOrExpressionInside{};
          bool canHoldTypeId = false;
          Advance();
          if (this->Peek().type == TokenType::LeftParenthesis)
          {
               const auto currentPosition = CurrentPosition();
               Advance();
               canHoldTypeId = true;
               if (Peek().type == TokenType::Keyword && !SimpleTypes.contains(std::get<std::string>(Peek().value)))
               {
                    typeOrExpressionInside = ParseUnaryExpression(context);
                    canHoldTypeId = false;
               }
               else if (Peek().type != TokenType::Literal)
               {
                    auto typeId = ParseTypeId(context);
                    if (!typeId.WasSuccessful())
                    {
                         std::vector<diagnostics::DiagnosticsMessage> diagnostics = std::move(typeId.diagnostics);

                         std::ranges::move(diagnostics, std::back_inserter(this->_diagnostics.get().diagnosticsList));
                    }
                    typeOrExpressionInside = std::move(typeId.value);
               }
               else
               {
                    typeOrExpressionInside = ParseUnaryExpression(context);
                    canHoldTypeId = false;
               }
               if (!Match(TokenType::RightParenthesis))
               {
                    this->SetPosition(currentPosition);
                    typeOrExpressionInside = ParseUnaryExpression(context);
                    canHoldTypeId = false;
               }
          }
          else
               typeOrExpressionInside = ParseUnaryExpression(context);
          auto source = currentToken.location;
          source.endPosition = Peek().location.endPosition;

          if (std::get<std::string>(currentToken.value) == "alignof")
               return std::unique_ptr<AlignOfNode, ecpps::ast::ASTDeleter>(
                    new (context) AlignOfNode(std::move(typeOrExpressionInside), canHoldTypeId, source));

          return std::unique_ptr<SizeOfNode, ecpps::ast::ASTDeleter>(
               new (context) SizeOfNode(std::move(typeOrExpressionInside), canHoldTypeId, source));
     }

     // TODO:
     // unary-operator cast-expression
     // sizeof unary-expression
     // sizeof (type-id)
     // sizeof... (identifier)
     // alignof (type-id)
     // noexcept-expression
     // new-expression
     // delete-expression

     return ParsePostfixExpresssion(context);
}

NodePointer ecpps::ast::AST::ParseCastExpression(ASTContext& context)
{
     const auto currentToken = this->Peek();
     if (currentToken.type == TokenType::LeftParenthesis)
     {
          // TODO: (type-id) cast-expression
     }

     return ParseUnaryExpression(context);
}

NodePointer ecpps::ast::AST::ParsePmExpression(ASTContext& context)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseCastExpression(context);
     while (true)
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               if (std::get<std::string>(currentToken.value) == ".*" ||
                   std::get<std::string>(currentToken.value) == "->*")
               {
                    Advance();
                    // TODO: implement PM and not.. that placeholder
                    source.endPosition = currentToken.location.endPosition;
                    const auto operatorId = std::get<std::string>(currentToken.value) == ".*"
                                                 ? Operator::PointerToMemberObject
                                                 : Operator::PointerToMember;
                    expression =
                         std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                              std::move(expression), operatorId, ParseCastExpression(context), source));
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseMultiplicativeExpression(ASTContext& context)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParsePmExpression(context);
     while (true)
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               if (std::get<std::string>(currentToken.value) == "*" ||
                   std::get<std::string>(currentToken.value) == "/" || std::get<std::string>(currentToken.value) == "%")
               {
                    Advance();
                    source.endPosition = currentToken.location.endPosition;
                    const auto operatorId = std::get<std::string>(currentToken.value) == "*"   ? Operator::Asterisk
                                            : std::get<std::string>(currentToken.value) == "/" ? Operator::Solidus
                                                                                               : Operator::Percent;

                    expression =
                         std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                              std::move(expression), operatorId, ParsePmExpression(context), source));
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseAdditiveExpression(ASTContext& context)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseMultiplicativeExpression(context);
     while (true)
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               if (std::get<std::string>(currentToken.value) == "+" || std::get<std::string>(currentToken.value) == "-")
               {
                    Advance();
                    source.endPosition = currentToken.location.endPosition;
                    const auto operatorId =
                         std::get<std::string>(currentToken.value) == "+" ? Operator::Plus : Operator::Minus;
                    expression =
                         std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                              std::move(expression), operatorId, ParseMultiplicativeExpression(context), source));
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseShiftExpression(ASTContext& context)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseAdditiveExpression(context);
     while (true)
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               if (std::get<std::string>(currentToken.value) == ">>" ||
                   std::get<std::string>(currentToken.value) == "<<")
               {
                    Advance();
                    source.endPosition = currentToken.location.endPosition;
                    const auto operatorId =
                         std::get<std::string>(currentToken.value) == ">>" ? Operator::RightShift : Operator::LeftShift;
                    expression =
                         std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                              std::move(expression), operatorId, ParseAdditiveExpression(context), source));
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseCompareExpression(ASTContext& context)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseShiftExpression(context);
     while (true)
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               if (std::get<std::string>(currentToken.value) == "<=>")
               {
                    Advance();
                    source.endPosition = currentToken.location.endPosition;
                    expression =
                         std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                              std::move(expression), Operator::Spaceship, ParseShiftExpression(context), source));
                    continue;
               }
          }

          break;
     }
     return expression;
}

#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

NodePointer ecpps::ast::AST::ParseRelationalExpression(ASTContext& context)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseCompareExpression(context);
     while (true)
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               const auto& operatorr = currentToken.AsOperator();
               if (operatorr == "<" || operatorr == ">" || operatorr == "<=" || operatorr == ">=")
               {
                    Advance();
                    source.endPosition = currentToken.location.endPosition;
                    const auto operatorId = operatorr == "<"    ? Operator::Less
                                            : operatorr == ">"  ? Operator::Greater
                                            : operatorr == "<=" ? Operator::LessEqual
                                                                : Operator::GreaterEqual;
                    expression =
                         std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                              std::move(expression), operatorId, ParseCompareExpression(context), source));
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseEqualityExpression(ASTContext& context)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseRelationalExpression(context);
     while (true)
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               const auto& operatorr = currentToken.AsOperator();
               if (operatorr == "==" || operatorr == "!=")
               {
                    Advance();
                    source.endPosition = currentToken.location.endPosition;
                    const auto operatorId = operatorr == "==" ? Operator::EqualsSign : Operator::NotEqual;
                    expression =
                         std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                              std::move(expression), operatorId, ParseRelationalExpression(context), source));
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseBinaryAndExpression(ASTContext& context)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseEqualityExpression(context);
     while (true)
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               if (std::get<std::string>(currentToken.value) == "&")
               {
                    Advance();
                    source.endPosition = currentToken.location.endPosition;
                    const auto operatorId = Operator::Ampersand;
                    expression =
                         std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                              std::move(expression), operatorId, ParseEqualityExpression(context), source));
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseBinaryExclusiveOrExpression(ASTContext& context)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseBinaryAndExpression(context);
     while (true)
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               if (std::get<std::string>(currentToken.value) == "^")
               {
                    Advance();
                    source.endPosition = currentToken.location.endPosition;
                    const auto operatorId = Operator::CircumflexAccent;
                    expression =
                         std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                              std::move(expression), operatorId, ParseBinaryAndExpression(context), source));
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseBinaryInclusiveOrExpression(ASTContext& context)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseBinaryExclusiveOrExpression(context);
     while (true)
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               if (std::get<std::string>(currentToken.value) == "|")
               {
                    Advance();
                    source.endPosition = currentToken.location.endPosition;
                    const auto operatorId = Operator::VerticalLine;
                    expression =
                         std::unique_ptr<BinaryOperatorNode, ecpps::ast::ASTDeleter>(new (context) BinaryOperatorNode(
                              std::move(expression), operatorId, ParseBinaryExclusiveOrExpression(context), source));
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseLogicalAndExpression(ASTContext& context)
{
     auto source = this->Peek().location;
     auto expression = ParseBinaryInclusiveOrExpression(context);
     while (!AtEnd() && IsOperatorToken(Peek(), "&&"))
     {
          Advance();
          auto right = ParseBinaryInclusiveOrExpression(context);
          source.endPosition = Peek(-1).location.endPosition;
          expression = std::unique_ptr<BinaryOperatorNode, ASTDeleter>(
               new (context) BinaryOperatorNode(std::move(expression), Operator::LogicalAnd, std::move(right), source));
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseLogicalOrExpression(ASTContext& context)
{
     auto source = this->Peek().location;
     auto expression = ParseLogicalAndExpression(context);
     while (!AtEnd() && IsOperatorToken(Peek(), "||"))
     {
          Advance();
          auto right = ParseLogicalAndExpression(context);
          source.endPosition = Peek(-1).location.endPosition;
          expression = std::unique_ptr<BinaryOperatorNode, ASTDeleter>(
               new (context) BinaryOperatorNode(std::move(expression), Operator::LogicalOr, std::move(right), source));
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseConditionalExpression(ASTContext& context)
{
     [[maybe_unused]] auto currentToken = this->Peek();
     [[maybe_unused]] auto source = currentToken.location;

     auto expression = ParseLogicalOrExpression(context);
     return expression;
}

NodePointer ecpps::ast::AST::ParseAssignmentExpression(ASTContext& context)
{
     auto source = this->Peek().location;
     auto expression = ParseConditionalExpression(context);
     if (AtEnd() || Peek().type != TokenType::Operator) return expression;

     static const std::unordered_map<std::string_view, Operator> assignmentOperators{
          {"=", Operator::Assignment},        {"+=", Operator::PlusAssign},       {"-=", Operator::MinusAssign},
          {"*=", Operator::MultiplyAssign},   {"/=", Operator::DivideAssign},     {"%=", Operator::ModuloAssign},
          {"^=", Operator::XorAssign},        {"&=", Operator::AndAssign},        {"|=", Operator::OrAssign},
          {"<<=", Operator::LeftShiftAssign}, {">>=", Operator::RightShiftAssign}};

     const auto found = assignmentOperators.find(std::string_view{Peek().AsOperator()});
     if (found == assignmentOperators.end()) return expression;
     Advance();
     auto right = ParseAssignmentExpression(context);
     source.endPosition = Peek(-1).location.endPosition;
     return std::unique_ptr<BinaryOperatorNode, ASTDeleter>(
          new (context) BinaryOperatorNode(std::move(expression), found->second, std::move(right), source));
}

NodePointer ecpps::ast::AST::ParseExpression(ASTContext& context)
{
     auto source = this->Peek().location;
     auto expression = ParseAssignmentExpression(context);
     while (!AtEnd() && IsOperatorToken(Peek(), ","))
     {
          Advance();
          auto right = ParseAssignmentExpression(context);
          source.endPosition = Peek(-1).location.endPosition;
          expression = std::unique_ptr<BinaryOperatorNode, ASTDeleter>(
               new (context) BinaryOperatorNode(std::move(expression), Operator::Comma, std::move(right), source));
     }
     return expression;
}
NodePointer ecpps::ast::AST::ParseCondition(ASTContext& context)
{
     // TODO: decl-specifier-seq declarator brace-or-equal-initializer
     return ParseExpression(context);
}

#ifdef __clang__
#pragma GCC diagnostic pop
#endif

NodePointer ecpps::ast::AST::ParseStatement(ASTContext& context)
{
     auto source = this->Peek().location;
     if (Peek().type == TokenType::Identifier && Peek(1).type == TokenType::Colon)
     {
          const auto& name = std::get<std::string>(Peek().value);
          Advance();
          Advance();

          return std::unique_ptr<LabelNode, ecpps::ast::ASTDeleter>(new (context) LabelNode(name, source));
     }
     if (Peek().type == TokenType::Keyword)
     {
          if (std::get<std::string>(Peek().value) == "goto" && Peek(1).type == TokenType::Identifier)
          {
               Advance();
               const auto& name = std::get<std::string>(Peek().value);
               Advance();
               if (!Match(TokenType::SemiColon))
               {
                    // TODO: Error
                    return nullptr;
               }

               return std::unique_ptr<GotoNode, ecpps::ast::ASTDeleter>(new (context) GotoNode(name, source));
          }
          if (std::get<std::string>(Peek().value) == "return")
          {

               Advance();
               if (Match(TokenType::SemiColon))
                    return std::unique_ptr<ReturnNode, ecpps::ast::ASTDeleter>(new (context)
                                                                                    ReturnNode(nullptr, source));
               auto value = ParseExpression(context);
               source.endPosition = Peek().location.endPosition;
               if (!Match(TokenType::SemiColon))
               {
                    this->_diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing semi-colon",
                                                                                           source));
               }
               return std::unique_ptr<ReturnNode, ecpps::ast::ASTDeleter>(new (context)
                                                                               ReturnNode(std::move(value), source));
          }
          if (std::get<std::string>(Peek().value) == "break")
          {

               Advance();

               if (!Match(TokenType::SemiColon))
               {
                    this->_diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing semi-colon",
                                                                                           source));
               }
               return std::unique_ptr<BreakNode, ecpps::ast::ASTDeleter>(new (context) BreakNode(source));
          }
          if (std::get<std::string>(Peek().value) == "continue")
          {

               Advance();

               if (!Match(TokenType::SemiColon))
               {
                    this->_diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing semi-colon",
                                                                                           source));
               }
               return std::unique_ptr<ContinueNode, ecpps::ast::ASTDeleter>(new (context) ContinueNode(source));
          }
          if (std::get<std::string>(Peek().value) == "if")
          {

               Advance();
               return ParseIfStatement(context);
          }
          if (std::get<std::string>(Peek().value) == "while")
          {

               Advance();
               return ParseWhileStatement(context);
          }
          if (std::get<std::string>(Peek().value) == "for")
          {

               Advance();
               return ParseForStatement(context);
          }
          if (std::get<std::string>(Peek().value) == "do")
          {

               Advance();
               return ParseDoWhileStatement(context);
          }
     }
     if (IsDeclarationStart(context)) return ParseDeclarationStatement(context);
     return ParseExpressionStatement(context);
}
NodePointer ecpps::ast::AST::ParseIfStatement(ASTContext& context)
{
     auto source = this->Peek().location;

     bool hadLeftParen = true;
     if (!Match(TokenType::LeftParenthesis))
     {
          hadLeftParen = false;
          this->_diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing left parenthesis", source));
     }
     auto condition = ParseCondition(context);
     if (!Match(TokenType::RightParenthesis))
     {
          auto diagnostic =
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing right parenthesis", source);
          if (!hadLeftParen)
               diagnostic->SubDiagnostics().push_back(std::make_unique<diagnostics::Information>(
                    "hint", "The condition needs to be parenthesised", source));
          this->_diagnostics.get().diagnosticsList.push_back(std::move(diagnostic));
     }
     std::vector<NodePointer> body{};
     std::vector<NodePointer> elseBody{};

     if (Match(TokenType::LeftBrace))
          while (!Match(TokenType::RightBrace)) body.emplace_back(ParseStatement(context));
     else
          body.emplace_back(ParseStatement(context));

     if (Peek().type == TokenType::Keyword && Peek().AsKeyword() == "else")
     {
          Advance();
          if (Match(TokenType::LeftBrace))
               while (!Match(TokenType::RightBrace)) elseBody.emplace_back(ParseStatement(context));
          else
               elseBody.emplace_back(ParseStatement(context));
     }

     source.endPosition = Peek(-1).location.endPosition;
     return std::unique_ptr<IfStatementNode, ecpps::ast::ASTDeleter>(
          new (context) IfStatementNode(std::move(condition), std::move(body), std::move(elseBody), source));
}
NodePointer ecpps::ast::AST::ParseWhileStatement(ASTContext& context)
{
     auto source = this->Peek().location;

     bool hadLeftParen = true;
     if (!Match(TokenType::LeftParenthesis))
     {
          hadLeftParen = false;
          this->_diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing left parenthesis", source));
     }
     auto condition = ParseCondition(context);
     if (!Match(TokenType::RightParenthesis))
     {
          auto diagnostic =
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing right parenthesis", source);
          if (!hadLeftParen)
               diagnostic->SubDiagnostics().push_back(std::make_unique<diagnostics::Information>(
                    "hint", "The condition needs to be parenthesised", source));
          this->_diagnostics.get().diagnosticsList.push_back(std::move(diagnostic));
     }
     std::vector<NodePointer> body{};

     if (Match(TokenType::LeftBrace))
          while (!Match(TokenType::RightBrace)) body.emplace_back(ParseStatement(context));
     else
          body.emplace_back(ParseStatement(context));

     source.endPosition = Peek(-1).location.endPosition;
     return std::unique_ptr<WhileStatementNode, ecpps::ast::ASTDeleter>(
          new (context) WhileStatementNode(std::move(condition), std::move(body), source));
}
NodePointer ecpps::ast::AST::ParseForStatement(ASTContext& context)
{
     auto source = this->Peek().location;

     bool hadLeftParen = true;
     if (!Match(TokenType::LeftParenthesis))
     {
          hadLeftParen = false;
          this->_diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing left parenthesis", source));
     }

     auto init = ParseStatement(context);
     auto condition = Peek().type == TokenType::SemiColon ? nullptr : ParseCondition(context);
     if (!Match(TokenType::SemiColon))
     {
          this->_diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                    "Missing semi-colon between the condition and the expression", source));
     }
     auto expression = Peek().type == TokenType::RightParenthesis ? nullptr : ParseExpression(context);

     if (!Match(TokenType::RightParenthesis))
     {
          auto diagnostic =
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing right parenthesis", source);
          if (!hadLeftParen)
               diagnostic->SubDiagnostics().push_back(std::make_unique<diagnostics::Information>(
                    "hint", "The condition needs to be parenthesised", source));
          this->_diagnostics.get().diagnosticsList.push_back(std::move(diagnostic));
     }
     std::vector<NodePointer> body{};

     if (Match(TokenType::LeftBrace))
          while (!Match(TokenType::RightBrace)) body.emplace_back(ParseStatement(context));
     else
          body.emplace_back(ParseStatement(context));

     source.endPosition = Peek(-1).location.endPosition;
     return std::unique_ptr<ForStatementNode, ecpps::ast::ASTDeleter>(new (context) ForStatementNode(
          std::move(init), std::move(condition), std::move(expression), std::move(body), source));
}
NodePointer ecpps::ast::AST::ParseDoWhileStatement(ASTContext& context)
{
     auto source = this->Peek().location;

     std::vector<NodePointer> body{};

     if (Match(TokenType::LeftBrace))
          while (!Match(TokenType::RightBrace)) body.emplace_back(ParseStatement(context));
     else
          body.emplace_back(ParseStatement(context));

     if (Peek().type != TokenType::Keyword || Peek().AsKeyword() != "while")
     {
          this->_diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build(
                    "ill-formed do/while; expected while(condition)", source));
          return nullptr; // TODO: Maybe some error recovery?
     }
     Advance();

     bool hadLeftParen = true;
     if (!Match(TokenType::LeftParenthesis))
     {
          hadLeftParen = false;
          this->_diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing left parenthesis", source));
     }
     auto condition = ParseCondition(context);
     if (!Match(TokenType::RightParenthesis))
     {
          auto diagnostic =
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing right parenthesis", source);
          if (!hadLeftParen)
               diagnostic->SubDiagnostics().push_back(std::make_unique<diagnostics::Information>(
                    "hint", "The condition needs to be parenthesised", source));
          this->_diagnostics.get().diagnosticsList.push_back(std::move(diagnostic));
     }

     if (!Match(TokenType::SemiColon))
     {
          this->_diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.Build("Missing semi-colon", source));
     }

     source.endPosition = Peek(-1).location.endPosition;
     return std::unique_ptr<DoWhileStatementNode, ecpps::ast::ASTDeleter>(
          new (context) DoWhileStatementNode(std::move(condition), std::move(body), source));
}

NodePointer ecpps::ast::AST::ParseExpressionStatement(ASTContext& context)
{
     if (Match(TokenType::SemiColon)) return nullptr;

     const auto diagnosticsBefore = this->_diagnostics.get().diagnosticsList.size();
     auto expression = ParseExpression(context);
     if (expression == nullptr)
     {
          if (this->_diagnostics.get().diagnosticsList.size() == diagnosticsBefore)
               this->_diagnostics.get().diagnosticsList.push_back(
                    std::make_unique<diagnostics::SyntaxError>("Expected an expression", Peek().location));
          return nullptr;
     }

     if (!Match(TokenType::SemiColon))
          this->_diagnostics.get().diagnosticsList.push_back(
               std::make_unique<diagnostics::SyntaxError>("Expected ';'", Peek(-1).location));

     return expression;
}

ecpps::ast::FunctionParameter ecpps::ast::AST::ParseFunctionParameter(ASTContext& context)
{
     // TODO: Attributes
     // TODO: explicit this

     FunctionParameter parameter{};
     auto typeId = ParseTypeId(context);
     if (typeId.value == nullptr) return parameter;
     parameter.type = std::move(typeId.value);

     if (Peek().type == TokenType::Identifier) parameter.name = ParseIdentifier(context);

     if (IsOperatorToken(Peek(), "="))
     {
          Advance();
          parameter.defaultInitialiser = ParseAssignmentExpression(context);
     }
     return parameter;
}

void ecpps::ast::AST::ParseVirtSpecifierSeq(bool& isOverride, bool& isFinal)
{
     while (true)
     {
          const auto& token = Peek();
          if (IsWord(token, "override"))
          {
               if (isOverride)
                    this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                         "Duplicate 'override' virt-specifier", token.location));
               isOverride = true;
          }
          else if (IsWord(token, "final"))
          {
               if (isFinal)
                    this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
                         "Duplicate 'final' virt-specifier", token.location));
               isFinal = true;
          }
          else
               break;
          Advance();
     }
}

void ecpps::ast::AST::SynchroniseStatement(void)
{
     int braceDepth = 0;
     while (!AtEnd())
     {
          const auto type = Peek().type;
          if (type == TokenType::LeftBrace) ++braceDepth;
          else if (type == TokenType::RightBrace)
          {
               if (braceDepth == 0) return;
               --braceDepth;
          }
          else if (type == TokenType::SemiColon && braceDepth == 0)
          {
               Advance();
               return;
          }
          Advance();
     }
}

void ecpps::ast::AST::ParseBlockBody(ASTContext& context, std::vector<NodePointer>& body)
{
     while (!AtEnd() && Peek().type != TokenType::RightBrace)
     {
          const auto before = CurrentPosition();
          const auto diagnosticsBefore = this->_diagnostics.get().diagnosticsList.size();

          auto statement = ParseStatement(context);
          if (statement != nullptr)
          {
               body.push_back(std::move(statement));
               continue;
          }

          if (CurrentPosition() == before || this->_diagnostics.get().diagnosticsList.size() > diagnosticsBefore)
               SynchroniseStatement();
     }

     if (!Match(TokenType::RightBrace))
          this->_diagnostics.get().diagnosticsList.push_back(
               std::make_unique<diagnostics::SyntaxError>("Expected '}'", Peek().location));
}

void ecpps::ast::AST::ParseBodyOrStatement(ASTContext& context, std::vector<NodePointer>& body)
{
     if (Match(TokenType::LeftBrace)) return ParseBlockBody(context, body);
     if (auto statement = ParseStatement(context); statement != nullptr) body.push_back(std::move(statement));
}
