#include "AST.h"
#include <Assert.h>
#include <format>
#include <unordered_set>
#include "ASTs\Type.h"

using ecpps::ast::NodePointer;
static std::unordered_set<std::string> SimpleTypes = {"char",     "char8_t", "char16_t", "char32_t", "wchar_t",
                                                      "bool",     "short",   "int",      "long",     "signed",
                                                      "unsigned", "float",   "double",   "void"};

std::vector<NodePointer> ecpps::ast::AST::Parse(void)
{
     std::vector<NodePointer> nodes{};

     while (!this->AtEnd()) // declaration-seq
     {
          auto node = this->ParseDeclaration();
          if (node != nullptr) nodes.push_back(std::move(node));
     }

     return nodes;
}

std::unique_ptr<ecpps::ast::IdentifierNode> ecpps::ast::AST::ParseIdentifier(void)
{
     if (Peek().type != TokenType::Identifier) return nullptr;
     const auto& identifier = Peek();
     Advance();
     runtime_assert(std::holds_alternative<std::string>(identifier.value), "Identifier was not an identifier");
     return std::make_unique<IdentifierNode>(std::get<std::string>(identifier.value), identifier.location);
}

NodePointer ecpps::ast::AST::ParseSimpleTypeSpecifier(void)
{
     auto source = Peek().location;

     if (Peek().type == TokenType::Keyword && SimpleTypes.contains(std::get<std::string>(Peek().value)))
     {
          source.endPosition = Peek().location.endPosition;
          Advance();
          return std::make_unique<BasicType>(std::get<std::string>(Peek(-1).value), source);
     }
     const auto& peek = Peek();
     this->_diagnostics.get().diagnosticsList.push_back(std::make_unique<diagnostics::SyntaxError>(
         std::format("Expected a simple-type-specifier, found "), peek.location));
     Advance();
     return nullptr; // TODO: Error
}

NodePointer ecpps::ast::AST::ParseDeclaration(void)
{
     // TODO: special-declaration

     return ParseNameDeclaration();
}

NodePointer ecpps::ast::AST::ParseBlockDeclaration(void)
{
     Location source = Peek().location;

     if (Match(TokenType::SemiColon)) return nullptr; // empty-declaration
     if (Peek().type == TokenType::Keyword)
     {
          runtime_assert(std::holds_alternative<std::string>(Peek().value), "Keyword was not an identifier");
          if (std::get<std::string>(Peek().value) == "namespace")
          {
               Advance();
               auto name = ParseIdentifier();
               if (name == nullptr) return nullptr; // TODO: Error
               if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "=")
               {
                    Advance();
                    SBOVector<std::unique_ptr<IdentifierNode>> aliased{};
                    while (!AtEnd())
                    {
                         auto nested = ParseIdentifier();
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
                    return std::make_unique<NamespaceAliasNode>(std::move(name), std::move(aliased), source);
               }

               // TODO: Nested names [ namespace A::B ]
          }
     }

     return ParseSimpleDeclaration();
}

NodePointer ecpps::ast::AST::ParseNameDeclaration(void)
{
     if (Match(TokenType::SemiColon)) return nullptr; // empty-declaration
     return ParseFunctionDefinition();
}

NodePointer ecpps::ast::AST::ParseFunctionDefinition(void)
{
     SBOVector<std::unique_ptr<AttributeNode>> attributes{};
     bool isFriend = false;
     bool isInline = false;
     bool isExtern = false;
     std::optional<std::string> externOptional = std::nullopt;
     while (Peek().type == TokenType::LeftBracket)
     {
          auto attributeSource = Peek().location;
          Advance();
          if (Match(TokenType::LeftBracket))
          {
               if (Match(TokenType::Identifier))
               {
                    std::string name = std::get<std::string>(Peek(-1).value);
                    SBOVector<Token> arguments{};

                    attributeSource.endPosition = Peek(-1).location.endPosition;
                    attributes.EmplaceBack(std::make_unique<AttributeNode>(name, arguments, attributeSource));
               }

               if (!Match(TokenType::RightBracket) || !Match(TokenType::RightBracket))
               {
                    this->_diagnostics.get().diagnosticsList.push_back(
                        std::make_unique<diagnostics::SyntaxError>("Expected ]]", Peek().location));
               }
          }
          else
          {
               Retreat();
               break;
          }
     }

     while (Peek().type == TokenType::Keyword)
     {
          const auto& keyword = std::get<std::string>(Peek().value);
          if (keyword == "extern")
          {
               Advance();
               isExtern = true;
               if (Peek().type == TokenType::Literal && std::holds_alternative<StringLiteral>(Peek().value))
               {
                    const auto& literal = std::get<StringLiteral>(Peek().value);
                    Advance();
                    externOptional = literal.value;
               }
          }
          else
               break;
     }

     auto source = Peek().location;
     auto type = ParseSimpleTypeSpecifier();
     if (type == nullptr) return (Advance(), nullptr);
     abi::CallingConventionName callingConvention = abi::ABI::Current().DefaultCallingConventionName();

     if (Peek().type == TokenType::Keyword)
     {
          const auto& keyword = std::get<std::string>(Peek().value);
          if (keyword == "__mscall")
          {
               Advance();
               callingConvention = abi::CallingConventionName::Microsoftx64;
          }
     }
     auto name = ParseIdentifier();

     FunctionSignature signature{
         std::move(type),  false, false, ConstantExpressionSpecifier::None, std::move(attributes), std::move(name),
         callingConvention}; // TODO: Allow id-expression

     signature.isExtern = isExtern;
     signature.externOptional = externOptional;

     if (!Match(TokenType::LeftParenthesis))
     {
          return nullptr; // TODO: Error
     }
     while (!Match(TokenType::RightParenthesis))
     {
          auto param = ParseFunctionParameter();
          signature.parameters.parameters.push_back(std::move(param));
          if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == ",") Advance();
          else
          {
               if (Peek().type != TokenType::RightParenthesis) return nullptr; // TODO: Error
          }
     }
     if (!Match(TokenType::LeftBrace))
     {
          if (Match(TokenType::SemiColon))
          {
               source.endPosition = Peek(-1).location.endPosition;
               return std::make_unique<FunctionDeclarationNode>(std::move(signature), source);
          }
          return nullptr; // TODO: Error
     }

     SBOVector<NodePointer> body{};
     while (!Match(TokenType::RightBrace))
     {
          auto statement = ParseStatement();
          if (statement == nullptr) continue;
          body.Push(std::move(statement));
     }

     source.endPosition = Peek(-1).location.endPosition;
     return std::make_unique<FunctionDefinitionNode>(std::move(signature), std::move(body), source);
}

bool ecpps::ast::AST::IsDeclarationStart()
{
     size_t i = 0;
     bool hasQualifier = false;
     bool hasSignedness = false;
     bool hasLong = false;
     bool hasType = false;

     while (true)
     {
          const auto& tok = Peek(i);
          if (tok.type != TokenType::Keyword) break;

          const std::string& kw = std::get<std::string>(tok.value);

          if (kw == "const" || kw == "volatile" || kw == "extern" || kw == "static" || kw == "inline" || kw == "auto")
          {
               hasQualifier = true;
          }
          else if (kw == "signed" || kw == "unsigned")
          {
               if (hasSignedness) break;
               hasSignedness = true;
          }
          else if (kw == "short" || kw == "long") { hasLong = true; }
          else if (kw == "int" || kw == "char" || kw == "float" || kw == "double")
          {
               hasType = true;
               i++;
               break;
          }
          else
               break;

          i++;
     }

     if (hasType || hasLong) return true;

     const auto& nextTok = Peek(i);
     if (nextTok.type == TokenType::Operator && std::get<std::string>(nextTok.value) == "*") return true;

     return false;
}

std::string CombineTypeWords(const std::vector<std::string>& words)
{
     std::string result;
     for (auto& w : words)
     {
          if (!result.empty()) result += " ";
          result += w;
     }
     return result;
}

NodePointer ecpps::ast::AST::ParseSimpleDeclaration(void)
{
     Location source = Peek().location;

     //
     // decl-specifier-seq
     //

     // decl-specifier
     bool isFriend = false;
     bool isTypedef = false;
     bool isConstexpr = false;
     bool isConsteval = false;
     bool isConstinit = false;
     bool isInline = false;
     // storage-class-specifier
     bool isStatic = false;
     bool isThreadLocal = false;
     bool isExtern = false;
     bool isMutable = false;
     std::optional<NodePointer> optExplicitSpecifier = std::nullopt;

     NodePointer typeSpecifier = nullptr;
     SBOVector<QualifiedType::Section> sections;

     bool isConst = false;
     bool isVolatile = false;

     while (true)
     {
          if (Peek().type == TokenType::Keyword)
          {
               auto& keyword = std::get<std::string>(Peek().value);
               if (keyword == "friend")
               {
                    if (isFriend)
                         ; // TODO: error: duplicate 'friend'
                    isFriend = true;
               }
               else if (keyword == "typedef")
               {
                    if (isTypedef)
                         ; // TODO: error: duplicate 'typedef'
                    isTypedef = true;
               }
               else if (keyword == "constexpr")
               {
                    if (isConstexpr)
                         ; // TODO: error: duplicate 'constexpr'
                    isConstexpr = true;
               }
               else if (keyword == "consteval")
               {
                    if (isConsteval)
                         ; // TODO: error: duplicate 'consteval'
                    isConsteval = true;
               }
               else if (keyword == "constinit")
               {
                    if (isConstinit)
                         ; // TODO: error: duplicate 'constinit'
                    isConstinit = true;
               }
               else if (keyword == "inline")
               {
                    if (isInline)
                         ; // TODO: error: duplicate 'inline'
                    isInline = true;
               }
               else if (keyword == "static")
               {
                    if (isStatic)
                         ; // TODO: error: duplicate 'static'
                    isStatic = true;
               }
               else if (keyword == "thread_local")
               {
                    if (isThreadLocal)
                         ; // TODO: error: duplicate 'thread_local'
                    isThreadLocal = true;
               }
               else if (keyword == "extern")
               {
                    if (isExtern)
                         ; // TODO: error: duplicate 'extern'
                    isExtern = true;
               }
               else if (keyword == "mutable")
               {
                    if (isMutable)
                         ; // TODO: error: duplicate 'mutable'
                    isMutable = true;
               }
               else if (keyword == "explicit") // explicit-specifier
               {
                    if (optExplicitSpecifier.has_value())
                         ; // TODO: error: duplicate 'explicit'
                    optExplicitSpecifier = ParseLogicalOrExpression();
               }
               else if (keyword == "const") // cv-qualifier
               {
                    if (isConst)
                         ; // TODO: error: duplicate 'const'
                    isConst = true;
               }
               else if (keyword == "volatile") // cv-qualifier
               {
                    if (isVolatile)
                         ; // TODO: error: duplicate 'volatile'
                    isVolatile = true;
               }
               else if (keyword == "struct" || keyword == "class" || keyword == "union") // elaborated-type-specifier
               {
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
                    NodePointer base = std::make_unique<BasicType>(combinedType, source);

                    // Wrap it in PointerType layers
                    for (std::size_t i = 0; i < pointerLevel; ++i)
                    {
                         base = std::make_unique<PointerType>(std::move(base), source);
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
               while (true)
               {
                    NodePointer part =
                        std::make_unique<BasicType>(std::get<std::string>(Peek().value), Peek().location);
                    Advance();

                    bool isTemplate = false;
                    if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "<")
                    {
                         // TODO: Implement
                         part = std::make_unique<QualifiedType>(SBOVector<QualifiedType::Section>{}, std::move(part),
                                                                Peek().location);
                         isTemplate = true;
                    }

                    if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "::")
                    {
                         Advance();
                         sections.EmplaceBack(std::move(part), isTemplate);
                    }
                    else
                    {
                         typeSpecifier =
                             std::make_unique<QualifiedType>(std::move(sections), std::move(part), Peek().location);
                         break;
                    }
               }
               break;
          }
          else
               break;
     }

     if (typeSpecifier == nullptr)
     {
          // TODO: error: expected a type specifier
          return nullptr;
     }

     using VariableDeclarator = VariableDeclarationNode::Declarator;

     std::vector<VariableDeclarator> declarators{};

     do {
          NodePointer id = ParseIdExpression();
          if (!id) return nullptr;

          NodePointer initialiser = nullptr;
          if (Peek().type == TokenType::Operator &&
              std::get<std::string>(Peek().value) == "=") // brace-or-equal-initialiser
          {
               Advance();
               initialiser = ParseLogicalOrExpression();
               if (!initialiser) return nullptr;
          }

          declarators.emplace_back(std::move(id), std::move(initialiser));
     } while ((Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == ",") && (Advance(), true));

     if (!Match(TokenType::SemiColon))
     {
          // TODO: error: expected ';'
          return nullptr;
     }

     return std::make_unique<VariableDeclarationNode>(
         std::move(typeSpecifier), std::move(declarators),
         /*flags*/
         VariableDeclarationNode::Flags{isTypedef, isFriend, isConstexpr, isConsteval, isConstinit, isInline, isStatic,
                                        isThreadLocal, isExtern, isMutable, isConst, isVolatile},
         std::move(optExplicitSpecifier), source);
}

NodePointer ecpps::ast::AST::TryParseDeclSpecifier(void) { return NodePointer(); }

NodePointer ecpps::ast::AST::TryParseDefiningTypeSpecifier(void) { return NodePointer(); }

NodePointer ecpps::ast::AST::ParseInitDeclarator(void) { return NodePointer(); }

NodePointer ecpps::ast::AST::TryParseDeclarator(void) { return NodePointer(); }

NodePointer ecpps::ast::AST::TryParsePtrDeclarator(void) { return NodePointer(); }

NodePointer ecpps::ast::AST::TryParseNoPtrDeclarator(void) { return NodePointer(); }

NodePointer ecpps::ast::AST::ParseInitialiser(void) { return NodePointer(); }

NodePointer ecpps::ast::AST::ParsePrimaryExpression(void)
{
     if (this->AtEnd()) return nullptr;

     const auto currentToken = this->Peek();

     switch (currentToken.type)
     {
     case TokenType::Literal:
     {
          this->Advance();
          return std::visit(
              OverloadedVisitor{[&currentToken](const bool boolean) -> NodePointer
                                { return std::make_unique<BooleanLiteralNode>(boolean, currentToken.location); },
                                [&currentToken](const StringLiteral& literal) -> NodePointer
                                { return std::make_unique<StringLiteralNode>(literal.value, currentToken.location); },
                                [&currentToken](const IntegerLiteral& literal) -> NodePointer
                                { return std::make_unique<IntegerLiteralNode>(literal.value, currentToken.location); },
                                [&currentToken](const char literal) -> NodePointer
                                { return std::make_unique<CharacterLiteralNode>(literal, currentToken.location); },
                                [&currentToken](auto&&) -> NodePointer { return nullptr; }},
              currentToken.value);
     }
     break;
     case TokenType::Keyword:
     {
          const auto& keyword = std::get<std::string>(currentToken.value);
          if (keyword == "this")
          {
               this->Advance();
               return std::make_unique<ThisNode>(currentToken.location);
          }
     }
     break;
     case TokenType::LeftParenthesis: // ( expression )
     {
          this->Advance();
          auto expression = this->ParseExpression();
          if (!Match(TokenType::RightParenthesis))
          {
               // TODO: Error
               return nullptr;
          }
          return expression;
     }
     case TokenType::Identifier:
     {
          return ParseIdExpression();
     }
     break;
     }

     // TODO: Error
     return nullptr;
}

NodePointer ecpps::ast::AST::ParseIdExpression(void)
{
     if (Peek().type != TokenType::Identifier) return nullptr;

     const auto source = Peek().location;
     std::vector<NodePointer> parts;

     bool expectIdentifier = true;
     bool sawTemplateKeyword = false;

     while (true)
     {
          if (!AtEnd() && Peek().type == TokenType::Keyword && std::get<std::string>(Peek().value) == "template")
          {
               sawTemplateKeyword = true;
               Advance();
          }

          if (AtEnd() || Peek().type != TokenType::Identifier) break;

          auto currentToken = Peek();
          const auto& identifierName = std::get<std::string>(currentToken.value);
          Advance();

          if (!AtEnd() && Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "<")
          {
               Advance();
               std::vector<NodePointer> templateArguments;
               while (true)
               {
                    auto parsed = ParseSimpleTypeSpecifier();
                    if (!parsed) return nullptr;
                    templateArguments.push_back(std::move(parsed));

                    if (AtEnd()) return nullptr;

                    if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == ",")
                    {
                         Advance();
                         continue;
                    }

                    if (Peek().type == TokenType::Operator)
                    {
                         const auto op = std::get<std::string>(Peek().value);
                         if (op == ">")
                         {
                              Advance();
                              break;
                         }
                         if (op == ">>")
                         {
                              // collapse >> to >
                              Peek().value = std::string(">");
                              Advance();
                              break;
                         }
                    }

                    return nullptr;
               }

               // TODO: Implement
               throw nullptr;
               // parts.push_back(std::make_unique<TemplateIdNode>(identifierName, std::move(templateArguments),
               //                                                  sawTemplateKeyword, currentToken.location));
          }
          else
          {
               parts.push_back(std::make_unique<IdentifierNode>(identifierName, currentToken.location));

               if (sawTemplateKeyword)
               {
                    // TODO: Implement
                    throw nullptr;
               }
          }

          sawTemplateKeyword = false;

          if (AtEnd()) break;

          if (Peek().type == TokenType::Colon && Peek(1).type == TokenType::Colon)
          {
               Advance();
               Advance();
               continue;
          }
          const auto& next = Peek();

          break;
     }

     if (parts.empty()) return nullptr;
     if (parts.size() == 1) return std::move(parts.front());
     return std::make_unique<QualifiedIdNode>(std::move(parts), source);
}

NodePointer ecpps::ast::AST::ParsePostfixExpresssion(void)
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

     if (currentToken.type == TokenType::Keyword)
     {
          if (SimpleTypes.contains(std::get<std::string>(currentToken.value)))
          {
               this->Advance();
               if (Peek().type == TokenType::Keyword)
               {
                    std::string built = std::get<std::string>(currentToken.value);
                    while (Match(TokenType::Keyword))
                    {
                         if (SimpleTypes.contains(std::get<std::string>(Peek(-1).value)))
                              built += " " + std::get<std::string>(Peek(-1).value);
                         else
                         {
                              // TODO: Error
                              return nullptr;
                         }
                    }
                    source.endPosition = Peek(-1).location.endPosition;
                    if (Match(TokenType::LeftParenthesis))
                    {
                         if (!Match(TokenType::RightParenthesis))
                         {
                              // TODO: Parse initialiser
                         }
                    }

                    // TODO: Error
                    // "Cannot perform an explicit type conversion using a functional notation on `" + built + "`, as it
                    // is not a simple-type-specifier as mandated in [expr.type.conv]. See [expr.post.general] and
                    // [dcl.type.general] for the grammar."
                    return nullptr;
               }
               Retreat();
          }
     }

     auto expression = ParsePrimaryExpression();
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
                    expression = std::make_unique<UnaryOperatorNode>(operatorId, std::move(expression),
                                                                     UnaryOperatorType::Postfix, source);
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
                    expression = std::make_unique<BinaryOperatorNode>(std::move(expression), operatorId,
                                                                      ParseIdExpression(), source);
                    continue;
               }
          }
          if (currentToken.type == TokenType::LeftBracket)
          {
               Advance();
               // TODO: Implement

               continue;
          }
          if (currentToken.type == TokenType::LeftParenthesis)
          {
               Advance();
               SBOVector<NodePointer> argumentList{}; // TODO: Initialiser lists; for now expressions only
               while (!AtEnd())
               {
                    if (Match(TokenType::RightParenthesis)) break;
                    argumentList.Push(ParseExpression());
                    if (AtEnd() || Match(TokenType::RightParenthesis)) break;
                    const auto& token = Peek();
                    if (token.type == TokenType::Operator && std::get<std::string>(token.value) == ",") continue;

                    this->_diagnostics.get().diagnosticsList.push_back(
                        diagnostics::DiagnosticsBuilder<diagnostics::SyntaxError>{}.build(
                            "Expected a comma in function argument list", token.location));
                    return nullptr;
               }

               source.endPosition = currentToken.location.endPosition;
               expression = std::make_unique<CallOperatorNode>(std::move(expression), std::move(argumentList), source);

               continue;
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseUnaryExpression(void)
{
     const auto currentToken = this->Peek();
     if (currentToken.type == TokenType::Operator)
     {
          if (std::get<std::string>(currentToken.value) == "++" || std::get<std::string>(currentToken.value) == "--")
          {
               Advance();
               auto source = currentToken.location;
               auto expression = ParseCastExpression();
               source.endPosition = Peek().location.endPosition;
               const auto operatorId =
                   std::get<std::string>(currentToken.value) == "++" ? Operator::Increment : Operator::Decrement;
               return std::make_unique<UnaryOperatorNode>(operatorId, std::move(expression), UnaryOperatorType::Prefix,
                                                          source);
          }
     }
     if (currentToken.type == TokenType::Operator)
     {
          if (std::get<std::string>(currentToken.value) == "+" || std::get<std::string>(currentToken.value) == "-")
          {
               Advance();
               auto source = currentToken.location;
               auto expression = ParseCastExpression();
               source.endPosition = Peek().location.endPosition;
               const auto operatorId =
                   std::get<std::string>(currentToken.value) == "+" ? Operator::Plus : Operator::Minus;
               return std::make_unique<UnaryOperatorNode>(operatorId, std::move(expression), UnaryOperatorType::Prefix,
                                                          source);
          }
          if (std::get<std::string>(currentToken.value) == "*" || std::get<std::string>(currentToken.value) == "&")
          {
               Advance();
               auto source = currentToken.location;
               auto expression = ParseCastExpression();
               source.endPosition = Peek().location.endPosition;
               const auto operatorId =
                   std::get<std::string>(currentToken.value) == "*" ? Operator::Asterisk : Operator::Ampersand;
               return std::make_unique<UnaryOperatorNode>(operatorId, std::move(expression), UnaryOperatorType::Prefix,
                                                          source);
          }
          if (std::get<std::string>(currentToken.value) == "~" || std::get<std::string>(currentToken.value) == "!")
          {
               Advance();
               auto source = currentToken.location;
               auto expression = ParseCastExpression();
               source.endPosition = Peek().location.endPosition;
               const auto operatorId =
                   std::get<std::string>(currentToken.value) == "!" ? Operator::Exclamation : Operator::Tilde;
               return std::make_unique<UnaryOperatorNode>(operatorId, std::move(expression), UnaryOperatorType::Prefix,
                                                          source);
          }
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

     return ParsePostfixExpresssion();
}

NodePointer ecpps::ast::AST::ParseCastExpression(void)
{
     const auto currentToken = this->Peek();
     if (currentToken.type == TokenType::LeftParenthesis)
     {
          // TODO: (type-id) cast-expression
     }

     return ParseUnaryExpression();
}

NodePointer ecpps::ast::AST::ParsePmExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseCastExpression();
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
                    expression = std::make_unique<BinaryOperatorNode>(std::move(expression), operatorId,
                                                                      ParseCastExpression(), source);
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseMultiplicativeExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParsePmExpression();
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

                    expression = std::make_unique<BinaryOperatorNode>(std::move(expression), operatorId,
                                                                      ParsePmExpression(), source);
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseAdditiveExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseMultiplicativeExpression();
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
                    expression = std::make_unique<BinaryOperatorNode>(std::move(expression), operatorId,
                                                                      ParseMultiplicativeExpression(), source);
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseShiftExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseAdditiveExpression();
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
                    expression = std::make_unique<BinaryOperatorNode>(std::move(expression), operatorId,
                                                                      ParseAdditiveExpression(), source);
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseCompareExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseShiftExpression();
     while (true)
     {
          currentToken = this->Peek();
          if (currentToken.type == TokenType::Operator)
          {
               if (std::get<std::string>(currentToken.value) == "<=>")
               {
                    Advance();
                    source.endPosition = currentToken.location.endPosition;
                    expression = std::make_unique<BinaryOperatorNode>(std::move(expression), Operator::Spaceship,
                                                                      ParseShiftExpression(), source);
                    continue;
               }
          }

          break;
     }
     return expression;
}

NodePointer ecpps::ast::AST::ParseRelationalExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseCompareExpression();
     return expression;
}

NodePointer ecpps::ast::AST::ParseEqualityExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseRelationalExpression();
     return expression;
}

NodePointer ecpps::ast::AST::ParseBinaryAndExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseEqualityExpression();
     return expression;
}

NodePointer ecpps::ast::AST::ParseBinaryExclusiveOrExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseBinaryAndExpression();
     return expression;
}

NodePointer ecpps::ast::AST::ParseBinaryInclusiveOrExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseBinaryExclusiveOrExpression();
     return expression;
}

NodePointer ecpps::ast::AST::ParseLogicalAndExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseBinaryInclusiveOrExpression();
     return expression;
}

NodePointer ecpps::ast::AST::ParseLogicalOrExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseLogicalAndExpression();
     return expression;
}

NodePointer ecpps::ast::AST::ParseConditionalExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseLogicalOrExpression();
     return expression;
}

NodePointer ecpps::ast::AST::ParseAssignmentExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseConditionalExpression();
     return expression;
}

NodePointer ecpps::ast::AST::ParseExpression(void)
{
     auto currentToken = this->Peek();
     auto source = currentToken.location;

     auto expression = ParseAssignmentExpression();
     return expression;
}

NodePointer ecpps::ast::AST::ParseStatement(void)
{
     auto source = this->Peek().location;
     if (Peek().type == TokenType::Keyword && std::get<std::string>(Peek().value) == "return")
     {
          Advance();
          if (Match(TokenType::SemiColon)) return std::make_unique<ReturnNode>(nullptr, source);
          auto value = ParseExpression();
          source.endPosition = Peek().location.endPosition;
          if (!Match(TokenType::SemiColon))
          {
               // TODO: Error
               return nullptr;
          }
          return std::make_unique<ReturnNode>(std::move(value), source);
     }
     if (IsDeclarationStart()) return ParseDeclarationStatement();
     return ParseExpressionStatement();
}

NodePointer ecpps::ast::AST::ParseExpressionStatement(void)
{
     if (Match(TokenType::SemiColon)) return nullptr;

     auto expression = ParseExpression();

     if (!Match(TokenType::SemiColon))
     {
          // TODO: Error
          return nullptr;
     }

     return expression;
}

ecpps::ast::FunctionParameter ecpps::ast::AST::ParseFunctionParameter(void)
{
     // TODO: Attributes
     // TODO: explicit this
     auto type = ParseSimpleTypeSpecifier(); // TODO: More complex types
     FunctionParameter parameter{};
     if (type == nullptr) return parameter;
     parameter.type = std::move(type);
     if (Peek().type != TokenType::Identifier) return parameter;
     parameter.name = ParseIdentifier();
     if (Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "=")
     {
          Advance();
          parameter.defaultInitialiser = ParseAssignmentExpression();
     }
     return parameter;
}
