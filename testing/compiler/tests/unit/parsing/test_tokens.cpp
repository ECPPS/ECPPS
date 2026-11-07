// NOLINTBEGIN(bugprone-chained-comparison)
#define CATCH_CONFIG_MAIN
#include <Parsing/Tokeniser.h>
#include <catch_amalgamated.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using SignedSize = decltype(0z);

static ecpps::StringPool& TestStringPool(void)
{
     static ecpps::StringPool pool{};
     return pool;
}

static std::vector<ecpps::Token> Tokenise(const std::vector<ecpps::PreprocessingToken>& ppTokens)
{
     return ecpps::Tokeniser::Tokenise(ppTokens, TestStringPool());
}

static ecpps::PreprocessingToken ConstructIdentifier(const std::string_view name)
{
     return ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::Identifier, TestStringPool().Intern(name),
                                      ecpps::Location{0, 0, 0}};
}
static ecpps::PreprocessingToken ConstructOperator(const std::string_view name)
{
     return ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::OperatorOrPunctuator,
                                      TestStringPool().Intern(name), ecpps::Location{0, 0, 0}};
}
static ecpps::PreprocessingToken ConstructNumber(const std::string_view value)
{
     return ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::Number, TestStringPool().Intern(value),
                                      ecpps::Location{0, 0, 0}};
}
static ecpps::PreprocessingToken ConstructString(const std::string_view value)
{
     return ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::StringLiteral, TestStringPool().Intern(value),
                                      ecpps::Location{0, 0, 0}};
}

TEST_CASE("Tokeniser - Identifiers", "[parsing][tokeniser]")
{
     SECTION("Mapping identifiers")
     {
          std::vector<ecpps::PreprocessingToken> ppTokens = {
               ConstructIdentifier("variable"), ConstructIdentifier("_private"), ConstructIdentifier("count123"),
               ConstructIdentifier("__internal")};
          auto tokens = Tokenise(ppTokens);
          SignedSize identifierCount = std::ranges::count_if(tokens,
                                                             [](const auto& token)
                                                             {
                                                                  return token.type == ecpps::TokenType::Identifier;
                                                             });
          REQUIRE(ppTokens.size() == 4);
          REQUIRE(static_cast<std::size_t>(identifierCount) == ppTokens.size());
          REQUIRE(tokens[0].AsIdentifier() == "variable");
          REQUIRE(tokens[1].AsIdentifier() == "_private");
          REQUIRE(tokens[2].AsIdentifier() == "count123");
          REQUIRE(tokens[3].AsIdentifier() == "__internal");
     }

     SECTION("Separating identifiers")
     {
          std::vector<ecpps::PreprocessingToken> ppTokens = {
               ConstructIdentifier("variable123"), ConstructIdentifier("count"), ConstructOperator("+"),
               ConstructIdentifier("_private"),    ConstructOperator("-"),       ConstructIdentifier("__internal")};
          auto tokens = Tokenise(ppTokens);
          SignedSize identifierCount = std::ranges::count_if(tokens,
                                                             [](const auto& token)
                                                             {
                                                                  return token.type == ecpps::TokenType::Identifier;
                                                             });
          REQUIRE(ppTokens.size() == 6);
          REQUIRE(identifierCount == 4);
          REQUIRE(tokens[0].AsIdentifier() == "variable123");
          REQUIRE(tokens[1].AsIdentifier() == "count");
          REQUIRE(tokens[2].AsOperator() == "+");
          REQUIRE(tokens[3].AsIdentifier() == "_private");
          REQUIRE(tokens[4].AsOperator() == "-");
          REQUIRE(tokens[5].AsIdentifier() == "__internal");
     }
}

TEST_CASE("Tokeniser - Keywords", "[parsing][tokeniser]")
{
     std::vector<ecpps::PreprocessingToken> ppTokens = {ConstructIdentifier("if"), ConstructIdentifier("else"),
                                                        ConstructIdentifier("while"), ConstructIdentifier("for")};
     auto tokens = Tokenise(ppTokens);
     SignedSize keywordCount = std::ranges::count_if(tokens,
                                                     [](const auto& token)
                                                     {
                                                          return token.type == ecpps::TokenType::Keyword;
                                                     });
     REQUIRE(ppTokens.size() == 4);
     REQUIRE(static_cast<std::size_t>(keywordCount) == ppTokens.size());
     REQUIRE(tokens[0].AsKeyword() == "if");
     REQUIRE(tokens[1].AsKeyword() == "else");
     REQUIRE(tokens[2].AsKeyword() == "while");
     REQUIRE(tokens[3].AsKeyword() == "for");
}

TEST_CASE("Tokeniser - Operators", "[parsing][tokeniser]")
{
     SECTION("Mapping")
     {
          std::vector<ecpps::PreprocessingToken> ppTokens = {ConstructOperator("+"), ConstructOperator("-"),
                                                             ConstructOperator("*"), ConstructOperator("/")};
          auto tokens = Tokenise(ppTokens);
          SignedSize operatorCount = std::ranges::count_if(tokens,
                                                           [](const auto& token)
                                                           {
                                                                return token.type == ecpps::TokenType::Operator;
                                                           });
          REQUIRE(ppTokens.size() == 4);
          REQUIRE(static_cast<std::size_t>(operatorCount) == ppTokens.size());
          REQUIRE(tokens[0].AsOperator() == "+");
          REQUIRE(tokens[1].AsOperator() == "-");
          REQUIRE(tokens[2].AsOperator() == "*");
          REQUIRE(tokens[3].AsOperator() == "/");
     }

     SECTION("Separation")
     {

          std::vector<ecpps::PreprocessingToken> ppTokens = {ConstructOperator("+"), ConstructIdentifier("A"),
                                                             ConstructOperator("*"), ConstructIdentifier("B"),
                                                             ConstructOperator("/")};
          auto tokens = Tokenise(ppTokens);
          SignedSize operatorCount = std::ranges::count_if(tokens,
                                                           [](const auto& token)
                                                           {
                                                                return token.type == ecpps::TokenType::Operator;
                                                           });
          REQUIRE(ppTokens.size() == 5);
          REQUIRE(operatorCount == 3);
          REQUIRE(tokens[0].AsOperator() == "+");
          REQUIRE(tokens[1].AsIdentifier() == "A");
          REQUIRE(tokens[2].AsOperator() == "*");
          REQUIRE(tokens[3].AsIdentifier() == "B");
          REQUIRE(tokens[4].AsOperator() == "/");
     }
}
TEST_CASE("Tokeniser - Punctuators", "[parsing][tokeniser]")
{
     SECTION("Mapping")
     {
          std::vector<ecpps::PreprocessingToken> ppTokens = {ConstructOperator(";"), ConstructOperator(","),
                                                             ConstructOperator("."), ConstructOperator("->")};
          auto tokens = Tokenise(ppTokens);
          SignedSize semiColonCount = std::ranges::count_if(tokens,
                                                            [](const auto& token)
                                                            {
                                                                 return token.type == ecpps::TokenType::SemiColon;
                                                            });
          SignedSize commaCount =
               std::ranges::count_if(tokens,
                                     [](const auto& token)
                                     {
                                          return token.type == ecpps::TokenType::Operator && token.AsOperator() == ",";
                                     });
          REQUIRE(ppTokens.size() == 4);
          REQUIRE(semiColonCount == 1);
          REQUIRE(commaCount == 1);
          REQUIRE(tokens[1].AsOperator() == ",");
          REQUIRE(tokens[2].AsOperator() == ".");
          REQUIRE(tokens[3].AsOperator() == "->");
     }

     SECTION("Separation")
     {
          std::vector<ecpps::PreprocessingToken> ppTokens = {ConstructOperator(";"), ConstructIdentifier("A"),
                                                             ConstructOperator(","), ConstructIdentifier("B"),
                                                             ConstructOperator(".")};
          auto tokens = Tokenise(ppTokens);
          SignedSize semiColonCount = std::ranges::count_if(tokens,
                                                            [](const auto& token)
                                                            {
                                                                 return token.type == ecpps::TokenType::SemiColon;
                                                            });
          SignedSize commaCount =
               std::ranges::count_if(tokens,
                                     [](const auto& token)
                                     {
                                          return token.type == ecpps::TokenType::Operator && token.AsOperator() == ",";
                                     });
          REQUIRE(ppTokens.size() == 5);
          REQUIRE(semiColonCount == 1);
          REQUIRE(commaCount == 1);
          REQUIRE(tokens[0].type == ecpps::TokenType::SemiColon);
          REQUIRE(tokens[1].AsIdentifier() == "A");
          REQUIRE(tokens[2].AsOperator() == ",");
          REQUIRE(tokens[3].AsIdentifier() == "B");
          REQUIRE(tokens[4].AsOperator() == ".");
     }

     SECTION("Multi-character punctuators")
     {
          std::vector<ecpps::PreprocessingToken> ppTokens = {ConstructOperator("->"), ConstructIdentifier("A"),
                                                             ConstructOperator("::"), ConstructIdentifier("B"),
                                                             ConstructOperator("...")};
          auto tokens = Tokenise(ppTokens);
          SignedSize arrowCount =
               std::ranges::count_if(tokens,
                                     [](const auto& token)
                                     {
                                          return token.type == ecpps::TokenType::Operator && token.AsOperator() == "->";
                                     });
          SignedSize scopeResolutionCount =
               std::ranges::count_if(tokens,
                                     [](const auto& token)
                                     {
                                          return token.type == ecpps::TokenType::Operator && token.AsOperator() == "::";
                                     });
          SignedSize ellipsisCount = std::ranges::count_if(tokens,
                                                           [](const auto& token)
                                                           {
                                                                return token.type == ecpps::TokenType::Operator &&
                                                                       token.AsOperator() == "...";
                                                           });
          REQUIRE(ppTokens.size() == 5);
          REQUIRE(arrowCount == 1);
          REQUIRE(scopeResolutionCount == 1);
          REQUIRE(ellipsisCount == 1);
          REQUIRE(tokens[0].AsOperator() == "->");
          REQUIRE(tokens[1].AsIdentifier() == "A");
          REQUIRE(tokens[2].AsOperator() == "::");
          REQUIRE(tokens[3].AsIdentifier() == "B");
          REQUIRE(tokens[4].AsOperator() == "...");
     }

     SECTION("Parentheses and brackets")
     {
          std::vector<ecpps::PreprocessingToken> ppTokens = {ConstructOperator("("), ConstructIdentifier("A"),
                                                             ConstructOperator(")"), ConstructIdentifier("B"),
                                                             ConstructOperator("[")};
          auto tokens = Tokenise(ppTokens);
          SignedSize leftParenCount = std::ranges::count_if(tokens,
                                                            [](const auto& token)
                                                            {
                                                                 return token.type == ecpps::TokenType::LeftParenthesis;
                                                            });
          SignedSize rightParenCount =
               std::ranges::count_if(tokens,
                                     [](const auto& token)
                                     {
                                          return token.type == ecpps::TokenType::RightParenthesis;
                                     });
          SignedSize leftBracketCount = std::ranges::count_if(tokens,
                                                              [](const auto& token)
                                                              {
                                                                   return token.type == ecpps::TokenType::LeftBracket;
                                                              });
          REQUIRE(ppTokens.size() == 5);
          REQUIRE(leftParenCount == 1);
          REQUIRE(rightParenCount == 1);
          REQUIRE(leftBracketCount == 1);
          REQUIRE(tokens[0].type == ecpps::TokenType::LeftParenthesis);
          REQUIRE(tokens[1].AsIdentifier() == "A");
          REQUIRE(tokens[2].type == ecpps::TokenType::RightParenthesis);
          REQUIRE(tokens[3].AsIdentifier() == "B");
          REQUIRE(tokens[4].type == ecpps::TokenType::LeftBracket);
     }
}

TEST_CASE("Tokeniser - Interns repeated preprocessing and parser text", "[parsing][tokeniser][interning]")
{
     std::vector<ecpps::MacroReplacement> macros{};
     macros.emplace_back("ALIAS", std::nullopt, "shared", false);
     ecpps::Preprocessor preprocessor{TestStringPool()};
     const auto ppTokens = preprocessor.Parse("shared ALIAS \"text\" \"text\"", macros, "");

     REQUIRE(ppTokens.size() == 4);
     REQUIRE(ppTokens[0].value.View().data() == ppTokens[1].value.View().data());

     const auto tokens = ecpps::Tokeniser::Tokenise(ppTokens, TestStringPool());
     REQUIRE(tokens[0].AsIdentifier().View().data() == tokens[1].AsIdentifier().View().data());
     REQUIRE(std::get<ecpps::StringLiteral>(tokens[2].value).value.View().data() ==
             std::get<ecpps::StringLiteral>(tokens[3].value).value.View().data());
}

TEST_CASE("Literals", "[parsing][tokeniser]")
{
     std::vector<ecpps::PreprocessingToken> ppTokens = {
          ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::Number, TestStringPool().Intern("42"),
                                    ecpps::Location{0, 0, 0}},
          ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::CharacterLiteral, TestStringPool().Intern("a"),
                                    ecpps::Location{0, 0, 0}},
          ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::StringLiteral,
                                    TestStringPool().Intern("Hello, World!"), ecpps::Location{0, 0, 0}}};
     auto tokens = Tokenise(ppTokens);
     SignedSize numberCount =
          std::ranges::count_if(tokens,
                                [](const auto& token)
                                {
                                     return token.type == ecpps::TokenType::Literal &&
                                            std::holds_alternative<ecpps::IntegerLiteral>(token.value);
                                });
     SignedSize charLiteralCount = std::ranges::count_if(tokens,
                                                         [](const auto& token)
                                                         {
                                                              return token.type == ecpps::TokenType::Literal &&
                                                                     std::holds_alternative<char>(token.value);
                                                         });
     SignedSize stringLiteralCount =
          std::ranges::count_if(tokens,
                                [](const auto& token)
                                {
                                     return token.type == ecpps::TokenType::Literal &&
                                            std::holds_alternative<ecpps::StringLiteral>(token.value);
                                });
     REQUIRE(ppTokens.size() == 3);
     REQUIRE(numberCount == 1);
     REQUIRE(charLiteralCount == 1);
     REQUIRE(stringLiteralCount == 1);
     REQUIRE(tokens[0].IsLiteral());
     REQUIRE(tokens[1].IsLiteral());
     REQUIRE(tokens[2].IsLiteral());
}

TEST_CASE("Tokeniser - Boolean identifiers become boolean literals", "[unit][parsing][tokeniser]")
{
     const std::vector<ecpps::PreprocessingToken> ppTokens = {ConstructIdentifier("true"),
                                                              ConstructIdentifier("false")};

     const auto tokens = Tokenise(ppTokens);

     REQUIRE(tokens.size() == 2);
     REQUIRE(tokens[0].type == ecpps::TokenType::Literal);
     REQUIRE(std::holds_alternative<bool>(tokens[0].value));
     REQUIRE(std::get<bool>(tokens[0].value));
     REQUIRE(tokens[1].type == ecpps::TokenType::Literal);
     REQUIRE(std::holds_alternative<bool>(tokens[1].value));
     REQUIRE_FALSE(std::get<bool>(tokens[1].value));
}

TEST_CASE("Tokeniser - Integer and exponent literals retain parsed values", "[unit][parsing][tokeniser]")
{
     const std::vector<ecpps::PreprocessingToken> ppTokens = {ConstructNumber("42"), ConstructNumber("1.5e-2")};

     const auto tokens = Tokenise(ppTokens);

     REQUIRE(tokens.size() == 2);
     REQUIRE(tokens[0].type == ecpps::TokenType::Literal);
     REQUIRE(std::holds_alternative<ecpps::IntegerLiteral>(tokens[0].value));
     REQUIRE(std::get<ecpps::IntegerLiteral>(tokens[0].value).value == 42);
     REQUIRE(tokens[1].type == ecpps::TokenType::Literal);
     REQUIRE(std::holds_alternative<ecpps::FloatingPointLiteral>(tokens[1].value));
     REQUIRE(std::abs(std::get<ecpps::FloatingPointLiteral>(tokens[1].value).value - 0.015L) < 0.000001L);
}

TEST_CASE("Tokeniser - String escapes decode to their character values", "[unit][parsing][tokeniser]")
{
     const std::vector<ecpps::PreprocessingToken> ppTokens = {ConstructString(R"(line\n\x21\u03A9)")};

     const auto tokens = Tokenise(ppTokens);

     REQUIRE(tokens.size() == 1);
     REQUIRE(tokens[0].type == ecpps::TokenType::Literal);
     REQUIRE(std::holds_alternative<ecpps::StringLiteral>(tokens[0].value));
     REQUIRE(std::get<ecpps::StringLiteral>(tokens[0].value).value == std::string{"line\n!\xCE\xA9"});
}
// NOLINTEND(bugprone-chained-comparison)
