#define CATCH_CONFIG_MAIN
#include <Parsing/AST.h>
#include <Parsing/ASTs/General.h>
#include <Parsing/ASTs/Type.h>
#include <catch_amalgamated.hpp>

namespace
{
     const ecpps::Location Source{0, 0, 0};

     class Fixture
     {
     public:
          ecpps::Diagnostics diagnostics{};
          ecpps::ast::ASTContext allocator{64uz * 1024};

          std::vector<ecpps::ast::NodePointer> Parse(std::vector<ecpps::Token> tokens)
          {
               return ecpps::ast::AST{std::move(tokens), this->diagnostics}.Parse(this->allocator);
          }
     };

     ecpps::Token Keyword(std::string value)
     {
          return ecpps::Token{ecpps::TokenType::Keyword, std::move(value), Source};
     }

     ecpps::Token Identifier(std::string value)
     {
          return ecpps::Token{ecpps::TokenType::Identifier, std::move(value), Source};
     }

     ecpps::Token Operator(std::string value)
     {
          return ecpps::Token{ecpps::TokenType::Operator, std::move(value), Source};
     }

     ecpps::Token Punctuation(const ecpps::TokenType type)
     {
          return ecpps::Token{type, std::monostate{}, Source};
     }

     ecpps::Token Integer(const unsigned long long value)
     {
          return ecpps::Token{ecpps::TokenType::Literal, ecpps::IntegerLiteral{value}, Source};
     }

     std::vector<ecpps::Token> ZeroArgumentFunction(std::string name, std::vector<ecpps::Token> expression)
     {
          std::vector<ecpps::Token> tokens{Keyword("int"),
                                           Identifier(std::move(name)),
                                           Punctuation(ecpps::TokenType::LeftParenthesis),
                                           Punctuation(ecpps::TokenType::RightParenthesis),
                                           Punctuation(ecpps::TokenType::LeftBrace),
                                           Keyword("return")};
          tokens.insert(tokens.end(), std::make_move_iterator(expression.begin()),
                        std::make_move_iterator(expression.end()));
          tokens.push_back(Punctuation(ecpps::TokenType::SemiColon));
          tokens.push_back(Punctuation(ecpps::TokenType::RightBrace));
          return tokens;
     }

     const ecpps::ast::FunctionDefinitionNode* GetFunction(const std::vector<ecpps::ast::NodePointer>& nodes)
     {
          if (nodes.size() != 1) return nullptr;
          return dynamic_cast<const ecpps::ast::FunctionDefinitionNode*>(nodes.front().get());
     }

     const ecpps::ast::ReturnNode* GetReturn(const ecpps::ast::FunctionDefinitionNode& function)
     {
          if (function.Body().Size() != 1) return nullptr;
          return dynamic_cast<const ecpps::ast::ReturnNode*>(function.Body().Front().get());
     }
} // namespace

TEST_CASE("AST - Function definition preserves its signature and return expression", "[unit][parsing][ast]")
{
     Fixture fixture{};
     std::vector<ecpps::Token> tokens{Keyword("int"),
                                      Identifier("add"),
                                      Punctuation(ecpps::TokenType::LeftParenthesis),
                                      Keyword("int"),
                                      Identifier("lhs"),
                                      Operator(","),
                                      Keyword("int"),
                                      Identifier("rhs"),
                                      Punctuation(ecpps::TokenType::RightParenthesis),
                                      Punctuation(ecpps::TokenType::LeftBrace),
                                      Keyword("return"),
                                      Identifier("lhs"),
                                      Operator("+"),
                                      Identifier("rhs"),
                                      Punctuation(ecpps::TokenType::SemiColon),
                                      Punctuation(ecpps::TokenType::RightBrace)};

     auto nodes = fixture.Parse(std::move(tokens));
     const auto* function = GetFunction(nodes);
     REQUIRE(function != nullptr);
     const auto* functionName = dynamic_cast<const ecpps::ast::IdentifierNode*>(function->Signature().name.get());
     REQUIRE(functionName != nullptr);
     REQUIRE(functionName->Value() == "add");
     REQUIRE(function->Signature().parameters.parameters.size() == 2);

     const auto* firstParameter =
          dynamic_cast<const ecpps::ast::IdentifierNode*>(function->Signature().parameters.parameters[0].name.get());
     const auto* secondParameter =
          dynamic_cast<const ecpps::ast::IdentifierNode*>(function->Signature().parameters.parameters[1].name.get());
     REQUIRE(firstParameter != nullptr);
     REQUIRE(secondParameter != nullptr);
     REQUIRE(firstParameter->Value() == "lhs");
     REQUIRE(secondParameter->Value() == "rhs");

     const auto* returnNode = GetReturn(*function);
     REQUIRE(returnNode != nullptr);
     const auto* addition = dynamic_cast<const ecpps::ast::BinaryOperatorNode*>(returnNode->Value().get());
     REQUIRE(addition != nullptr);
     REQUIRE(addition->Value() == ecpps::ast::Operator::Plus);
     const auto* lhs = dynamic_cast<const ecpps::ast::IdentifierNode*>(addition->Left().get());
     const auto* rhs = dynamic_cast<const ecpps::ast::IdentifierNode*>(addition->Right().get());
     REQUIRE(lhs != nullptr);
     REQUIRE(rhs != nullptr);
     REQUIRE(lhs->Value() == "lhs");
     REQUIRE(rhs->Value() == "rhs");
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("AST - Multiplicative expressions bind more tightly than addition", "[unit][parsing][ast]")
{
     Fixture fixture{};
     auto nodes = fixture.Parse(
          ZeroArgumentFunction("value", {Integer(1), Operator("+"), Integer(2), Operator("*"), Integer(3)}));

     const auto* function = GetFunction(nodes);
     REQUIRE(function != nullptr);
     const auto* returnNode = GetReturn(*function);
     REQUIRE(returnNode != nullptr);
     const auto* addition = dynamic_cast<const ecpps::ast::BinaryOperatorNode*>(returnNode->Value().get());
     REQUIRE(addition != nullptr);
     REQUIRE(addition->Value() == ecpps::ast::Operator::Plus);
     const auto* multiplication = dynamic_cast<const ecpps::ast::BinaryOperatorNode*>(addition->Right().get());
     REQUIRE(multiplication != nullptr);
     REQUIRE(multiplication->Value() == ecpps::ast::Operator::Asterisk);
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("AST - Subtraction groups from left to right", "[unit][parsing][ast]")
{
     Fixture fixture{};
     auto nodes = fixture.Parse(
          ZeroArgumentFunction("value", {Integer(9), Operator("-"), Integer(4), Operator("-"), Integer(1)}));

     const auto* function = GetFunction(nodes);
     REQUIRE(function != nullptr);
     const auto* returnNode = GetReturn(*function);
     REQUIRE(returnNode != nullptr);
     const auto* outerSubtraction = dynamic_cast<const ecpps::ast::BinaryOperatorNode*>(returnNode->Value().get());
     REQUIRE(outerSubtraction != nullptr);
     REQUIRE(outerSubtraction->Value() == ecpps::ast::Operator::Minus);
     const auto* innerSubtraction = dynamic_cast<const ecpps::ast::BinaryOperatorNode*>(outerSubtraction->Left().get());
     REQUIRE(innerSubtraction != nullptr);
     REQUIRE(innerSubtraction->Value() == ecpps::ast::Operator::Minus);
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("AST - If/else retains its condition and both bodies", "[unit][parsing][ast]")
{
     Fixture fixture{};
     std::vector<ecpps::Token> tokens{Keyword("int"),
                                      Identifier("choose"),
                                      Punctuation(ecpps::TokenType::LeftParenthesis),
                                      Keyword("bool"),
                                      Identifier("condition"),
                                      Punctuation(ecpps::TokenType::RightParenthesis),
                                      Punctuation(ecpps::TokenType::LeftBrace),
                                      Keyword("if"),
                                      Punctuation(ecpps::TokenType::LeftParenthesis),
                                      Identifier("condition"),
                                      Punctuation(ecpps::TokenType::RightParenthesis),
                                      Keyword("return"),
                                      Integer(1),
                                      Punctuation(ecpps::TokenType::SemiColon),
                                      Keyword("else"),
                                      Keyword("return"),
                                      Integer(0),
                                      Punctuation(ecpps::TokenType::SemiColon),
                                      Punctuation(ecpps::TokenType::RightBrace)};

     auto nodes = fixture.Parse(std::move(tokens));
     const auto* function = GetFunction(nodes);
     REQUIRE(function != nullptr);
     REQUIRE(function->Body().Size() == 1);
     const auto* ifStatement = dynamic_cast<const ecpps::ast::IfStatementNode*>(function->Body().Front().get());
     REQUIRE(ifStatement != nullptr);
     const auto* condition = dynamic_cast<const ecpps::ast::IdentifierNode*>(ifStatement->Condition().get());
     REQUIRE(condition != nullptr);
     REQUIRE(condition->Value() == "condition");
     REQUIRE(ifStatement->Body().size() == 1);
     REQUIRE(ifStatement->ElseBody().size() == 1);

     const auto* thenReturn = dynamic_cast<const ecpps::ast::ReturnNode*>(ifStatement->Body().front().get());
     const auto* elseReturn = dynamic_cast<const ecpps::ast::ReturnNode*>(ifStatement->ElseBody().front().get());
     REQUIRE(thenReturn != nullptr);
     REQUIRE(elseReturn != nullptr);
     const auto* thenValue = dynamic_cast<const ecpps::ast::IntegerLiteralNode*>(thenReturn->Value().get());
     const auto* elseValue = dynamic_cast<const ecpps::ast::IntegerLiteralNode*>(elseReturn->Value().get());
     REQUIRE(thenValue != nullptr);
     REQUIRE(elseValue != nullptr);
     REQUIRE(thenValue->Value() == 1);
     REQUIRE(elseValue->Value() == 0);
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("AST - While retains its condition and continue statement", "[unit][parsing][ast]")
{
     Fixture fixture{};
     std::vector<ecpps::Token> tokens{Keyword("void"),
                                      Identifier("repeat"),
                                      Punctuation(ecpps::TokenType::LeftParenthesis),
                                      Keyword("bool"),
                                      Identifier("condition"),
                                      Punctuation(ecpps::TokenType::RightParenthesis),
                                      Punctuation(ecpps::TokenType::LeftBrace),
                                      Keyword("while"),
                                      Punctuation(ecpps::TokenType::LeftParenthesis),
                                      Identifier("condition"),
                                      Punctuation(ecpps::TokenType::RightParenthesis),
                                      Punctuation(ecpps::TokenType::LeftBrace),
                                      Keyword("continue"),
                                      Punctuation(ecpps::TokenType::SemiColon),
                                      Punctuation(ecpps::TokenType::RightBrace),
                                      Punctuation(ecpps::TokenType::RightBrace)};

     auto nodes = fixture.Parse(std::move(tokens));
     const auto* function = GetFunction(nodes);
     REQUIRE(function != nullptr);
     REQUIRE(function->Body().Size() == 1);
     const auto* whileStatement = dynamic_cast<const ecpps::ast::WhileStatementNode*>(function->Body().Front().get());
     REQUIRE(whileStatement != nullptr);
     const auto* condition = dynamic_cast<const ecpps::ast::IdentifierNode*>(whileStatement->Condition().get());
     REQUIRE(condition != nullptr);
     REQUIRE(condition->Value() == "condition");
     REQUIRE(whileStatement->Body().size() == 1);
     REQUIRE(dynamic_cast<const ecpps::ast::ContinueNode*>(whileStatement->Body().front().get()) != nullptr);
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("AST - Initialized variable retains its type, name, and value", "[unit][parsing][ast]")
{
     Fixture fixture{};
     std::vector<ecpps::Token> tokens{Keyword("int"), Identifier("count"), Operator("="), Integer(5),
                                      Punctuation(ecpps::TokenType::SemiColon)};

     auto nodes = fixture.Parse(std::move(tokens));
     REQUIRE(nodes.size() == 1);
     const auto* declaration = dynamic_cast<const ecpps::ast::VariableDeclarationNode*>(nodes.front().get());
     REQUIRE(declaration != nullptr);
     const auto* type = dynamic_cast<const ecpps::ast::BasicType*>(declaration->Type().get());
     REQUIRE(type != nullptr);
     REQUIRE(type->Value() == "int");
     REQUIRE(declaration->Declarators().size() == 1);
     const auto& declarator = declaration->Declarators().front();
     const auto* name = dynamic_cast<const ecpps::ast::IdentifierNode*>(declarator.name.get());
     REQUIRE(name != nullptr);
     REQUIRE(name->Value() == "count");
     REQUIRE(declarator.initialiserType == ecpps::ast::InitialisationType::EqualInitialiser);
     const auto* initialiser = dynamic_cast<const ecpps::ast::IntegerLiteralNode*>(declarator.initialiser.get());
     REQUIRE(initialiser != nullptr);
     REQUIRE(initialiser->Value() == 5);
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("AST - Function declaration retains its signature", "[unit][parsing][ast]")
{
     Fixture fixture{};
     std::vector<ecpps::Token> tokens{Keyword("int"),
                                      Identifier("add"),
                                      Punctuation(ecpps::TokenType::LeftParenthesis),
                                      Keyword("int"),
                                      Identifier("lhs"),
                                      Operator(","),
                                      Keyword("int"),
                                      Identifier("rhs"),
                                      Punctuation(ecpps::TokenType::RightParenthesis),
                                      Punctuation(ecpps::TokenType::SemiColon)};

     auto nodes = fixture.Parse(std::move(tokens));
     REQUIRE(nodes.size() == 1);
     const auto* declaration = dynamic_cast<const ecpps::ast::FunctionDeclarationNode*>(nodes.front().get());
     REQUIRE(declaration != nullptr);
     const auto& signature = declaration->Signature();
     const auto* returnType = dynamic_cast<const ecpps::ast::BasicType*>(signature.type.get());
     const auto* name = dynamic_cast<const ecpps::ast::IdentifierNode*>(signature.name.get());
     REQUIRE(returnType != nullptr);
     REQUIRE(returnType->Value() == "int");
     REQUIRE(name != nullptr);
     REQUIRE(name->Value() == "add");
     REQUIRE(signature.parameters.parameters.size() == 2);

     const auto& lhsParameter = signature.parameters.parameters[0];
     const auto& rhsParameter = signature.parameters.parameters[1];
     const auto* lhsType = dynamic_cast<const ecpps::ast::BasicType*>(lhsParameter.type.get());
     const auto* rhsType = dynamic_cast<const ecpps::ast::BasicType*>(rhsParameter.type.get());
     const auto* lhsName = dynamic_cast<const ecpps::ast::IdentifierNode*>(lhsParameter.name.get());
     const auto* rhsName = dynamic_cast<const ecpps::ast::IdentifierNode*>(rhsParameter.name.get());
     REQUIRE(lhsType != nullptr);
     REQUIRE(rhsType != nullptr);
     REQUIRE(lhsType->Value() == "int");
     REQUIRE(rhsType->Value() == "int");
     REQUIRE(lhsName != nullptr);
     REQUIRE(rhsName != nullptr);
     REQUIRE(lhsName->Value() == "lhs");
     REQUIRE(rhsName->Value() == "rhs");
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}
