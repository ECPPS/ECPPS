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

     return ParseBlockDeclaration();
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

     return ParseNameDeclaration();
}

NodePointer ecpps::ast::AST::ParseNameDeclaration(void) { return ParseFunctionDefinition(); } //

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

     FunctionSignature signature{std::move(type),
                                 false,
                                 false,
                                 ConstantExpressionSpecifier::None, std::move(attributes),
                                 std::move(name),
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
          const auto keyword = std::get<std::string>(currentToken.value);
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
     auto& currentToken = this->Peek();
     if (currentToken.type != TokenType::Identifier) return nullptr;

     auto source = currentToken.location;
     Advance();

     if (!AtEnd() && Peek().type == TokenType::Operator && std::get<std::string>(Peek().value) == "<")
     {
          Advance();
          std::vector<NodePointer> templateArguments;
          while (true)
          {
               // Parse an individual type for the template argument
               auto parsed = ParseSimpleTypeSpecifier();
               templateArguments.push_back(std::move(parsed));

               // If the next token is a comma, advance and continue parsing
               if (Peek().type != TokenType::Operator || std::get<std::string>(Peek().value) != ",") { Advance(); }
               else if (Peek().type != TokenType::Operator && std::get<std::string>(Peek().value) == ">")
               {
                    Advance(); // Consume the '>'
                    break;
               }
               else if (Peek().type != TokenType::Operator && std::get<std::string>(Peek().value) == ">>")
               {
                    Peek().value = ">";
                    break;
               }
               else
               {
                    // TODO: Error
                    return nullptr;
               }
          }
          // TODO: Return simple-template-id
          return nullptr;
     }

     return std::make_unique<IdentifierNode>(std::get<std::string>(currentToken.value), currentToken.location);
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
