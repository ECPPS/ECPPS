#pragma once
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "SourceMap.h"

namespace ecpps
{
     enum struct PreprocessingTokenType : std::uint_fast8_t
     {
          Identifier,
          Number,
          CharacterLiteral,
          StringLiteral,
          OperatorOrPuncturator
     };
     struct PreprocessingToken
     {
          PreprocessingTokenType type{};
          std::string value{};
          Location source;

          explicit PreprocessingToken(const PreprocessingTokenType type, std::string value, const Location source)
              : type(type), value(std::move(value)), source(source)
          {
          }
     };
     class Tokeniser;
     class Preprocessor
     {
     public:
          [[nodiscard]] static std::vector<PreprocessingToken> Parse(const std::string& source);
          [[nodiscard]] static void Print(const std::vector<PreprocessingToken>& ppTokens);

     private:
          static bool IsDigit(const char ch) { return std::isdigit(ch); }
          static bool IsCharacterBeginning(const char ch) { return std::isalpha(ch) || ch == '_'; }
          static bool IsCharacterContinuation(const char ch) { return std::isalnum(ch) || ch == '_'; }
          static bool IsOperatorOrPunctuator(const std::string& string);
          static bool IsOperatorOrPunctuatorBeginning(char ch);

          friend class Tokeniser;
     };
} // namespace ecpps