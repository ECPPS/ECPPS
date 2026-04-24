// NOLINTBEGIN(bugprone-chained-comparison)
#define CATCH_CONFIG_MAIN
#include <Parsing/Tokeniser.h>
#include <catch_amalgamated.hpp>
#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

using SignedSize = decltype(0z);

static ecpps::PreprocessingToken ConstructIdentifier(const std::string& name)
{
     return ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::Identifier, name, ecpps::Location{0, 0, 0}};
}
static ecpps::PreprocessingToken ConstructOperator(const std::string& name)
{
     return ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::OperatorOrPunctuator, name,
                                      ecpps::Location{0, 0, 0}};
}

TEST_CASE("Tokeniser - Identifiers", "[parsing][tokeniser]")
{
     SECTION("Mapping identifiers")
     {
          std::vector<ecpps::PreprocessingToken> ppTokens = {
              ConstructIdentifier("variable"), ConstructIdentifier("_private"), ConstructIdentifier("count123"),
              ConstructIdentifier("__internal")};
          auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          SignedSize identifierCount = std::ranges::count_if(tokens, [](const auto& token)
                                                              { return token.type == ecpps::TokenType::Identifier; });
          REQUIRE(ppTokens.size() == 4);
          REQUIRE(identifierCount == ppTokens.size());
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
          auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          SignedSize identifierCount = std::ranges::count_if(tokens, [](const auto& token)
                                                              { return token.type == ecpps::TokenType::Identifier; });
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
     auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
     SignedSize keywordCount =
         std::ranges::count_if(tokens, [](const auto& token) { return token.type == ecpps::TokenType::Keyword; });
     REQUIRE(ppTokens.size() == 4);
     REQUIRE(keywordCount == ppTokens.size());
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
          auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          SignedSize operatorCount =
              std::ranges::count_if(tokens, [](const auto& token) { return token.type == ecpps::TokenType::Operator; });
          REQUIRE(ppTokens.size() == 4);
          REQUIRE(operatorCount == ppTokens.size());
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
          auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          SignedSize operatorCount =
              std::ranges::count_if(tokens, [](const auto& token) { return token.type == ecpps::TokenType::Operator; });
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
          auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          SignedSize semiColonCount = std::ranges::count_if(tokens, [](const auto& token)
                                                             { return token.type == ecpps::TokenType::SemiColon; });
          SignedSize commaCount =
              std::ranges::count_if(tokens, [](const auto& token)
                                    { return token.type == ecpps::TokenType::Operator && token.AsOperator() == ","; });
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
          auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          SignedSize semiColonCount = std::ranges::count_if(tokens, [](const auto& token)
                                                             { return token.type == ecpps::TokenType::SemiColon; });
          SignedSize commaCount =
              std::ranges::count_if(tokens, [](const auto& token)
                                    { return token.type == ecpps::TokenType::Operator && token.AsOperator() == ","; });
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
          auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          SignedSize arrowCount =
              std::ranges::count_if(tokens, [](const auto& token)
                                    { return token.type == ecpps::TokenType::Operator && token.AsOperator() == "->"; });
          SignedSize scopeResolutionCount =
              std::ranges::count_if(tokens, [](const auto& token)
                                    { return token.type == ecpps::TokenType::Operator && token.AsOperator() == "::"; });
          SignedSize ellipsisCount = std::ranges::count_if(
              tokens, [](const auto& token)
              { return token.type == ecpps::TokenType::Operator && token.AsOperator() == "..."; });
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
          auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          SignedSize leftParenCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::TokenType::LeftParenthesis; });
          SignedSize rightParenCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::TokenType::RightParenthesis; });
          SignedSize leftBracketCount = std::ranges::count_if(tokens, [](const auto& token)
                                                               { return token.type == ecpps::TokenType::LeftBracket; });
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

TEST_CASE("Literals", "[parsing][tokeniser]")
{
     std::vector<ecpps::PreprocessingToken> ppTokens = {
         ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::Number, "42", ecpps::Location{0, 0, 0}},
         ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::CharacterLiteral, "a", ecpps::Location{0, 0, 0}},
         ecpps::PreprocessingToken{ecpps::PreprocessingTokenType::StringLiteral, "Hello, World!",
                                   ecpps::Location{0, 0, 0}}};
     auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
     SignedSize numberCount =
         std::ranges::count_if(tokens,
                               [](const auto& token)
                               {
                                    return token.type == ecpps::TokenType::Literal &&
                                           std::holds_alternative<ecpps::IntegerLiteral>(token.value);
                               });
     SignedSize charLiteralCount = std::ranges::count_if(
         tokens, [](const auto& token)
         { return token.type == ecpps::TokenType::Literal && std::holds_alternative<char>(token.value); });
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
// NOLINTEND(bugprone-chained-comparison)
