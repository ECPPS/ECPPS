#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "Preprocessor.h"
#include "SourceMap.h"

namespace ecpps
{
     enum struct TokenType : std::uint_fast8_t
     {
          Identifier,
          Keyword,
          Literal,
          OperatorOrPunctuator
     };
     struct StringLiteral
     {
          std::string value{};
     };
     struct NumericLiteral
     {
          long double value{};
     };
     struct UserDefinedStringLiteral
     {
          StringLiteral value{};
          std::string name{};
     };
     struct UserDefinedNumericLiteral
     {
          NumericLiteral value{};
          std::string name{};
     };
     struct Token
     {
          using TokenValue = std::variant<std::string, StringLiteral, NumericLiteral, UserDefinedStringLiteral,
                                          UserDefinedNumericLiteral>;

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

     private:
     };
} // namespace ecpps