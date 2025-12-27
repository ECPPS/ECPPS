#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "Preprocessor.h"

namespace ecpps
{
     template <typename... TVariants> struct OverloadedVisitor : TVariants...
     {
          using TVariants::operator()...;
     };
     template <typename... TVariants> OverloadedVisitor(TVariants...) -> OverloadedVisitor<TVariants...>;

     enum struct TokenType : std::uint_fast8_t
     {
          /// <summary>
          /// any identifier
          /// </summary>
          Identifier,
          /// <summary>
          /// any keyword
          /// </summary>
          Keyword,
          /// <summary>
          /// literals
          /// </summary>
          Literal,
          /// <summary>
          /// operators
          /// </summary>
          Operator,
          /// <summary>
          /// (
          /// </summary>
          LeftParenthesis,
          /// <summary>
          /// )
          /// </summary>
          RightParenthesis,
          /// <summary>
          /// [
          /// </summary>
          LeftBracket,
          /// <summary>
          /// ]
          /// </summary>
          RightBracket,
          /// <summary>
          /// {
          /// </summary>
          LeftBrace,
          /// <summary>
          /// }
          /// </summary>
          RightBrace,
          /// <summary>
          /// ;
          /// </summary>
          SemiColon,
          /// <summary>
          /// :
          /// </summary>
          Colon
     };
     struct StringLiteral
     {
          std::string value{};
     };
     struct IntegerLiteral
     {
          std::uintmax_t value{};
     };
     struct FloatingPointLiteral
     {
          long double value{};
     };
     struct UserDefinedLiteral
     {
          using Value = std::variant<StringLiteral, IntegerLiteral, FloatingPointLiteral>;

          Value value{};
          std::string name{};
     };

     struct Token
     {
          using TokenValue = std::variant<std::monostate, std::string, bool, StringLiteral, char, IntegerLiteral,
                                          FloatingPointLiteral, UserDefinedLiteral>;

          TokenType type;
          TokenValue value;
          Location location;

          explicit Token(const TokenType type, TokenValue value, const Location location)
              : type(type), value(std::move(value)), location(location)
          {
          }
     };

     class Tokeniser
     {
     public:
          explicit Tokeniser(void) = delete;

          static std::vector<Token> Tokenise(const std::vector<PreprocessingToken>& ppTokens);
          static void Print(const std::vector<Token>& tokens);

     private:
          static bool IsKeyword(const std::string& identifier);

          static std::optional<std::uintmax_t> ParseInteger(const std::string& str)
          {
               std::size_t idx = 0;
               try
               {
                    auto val = std::stoull(str, &idx, 0);
                    if (idx == str.size()) return val;
               }
               catch (...) // NOLINT(bugprone-empty-catch)
               {
               }
               return std::nullopt;
          }

          static std::optional<long double> ParseFloat(const std::string& str)
          {
               std::size_t idx = 0;
               try
               {
                    auto val = std::stold(str, &idx);
                    if (idx == str.size()) return val;
               }
               catch (...) // NOLINT(bugprone-empty-catch)
               {
               }
               return std::nullopt;
          }
     };
} // namespace ecpps