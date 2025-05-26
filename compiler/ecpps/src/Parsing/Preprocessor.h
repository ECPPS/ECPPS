#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include "SourceMap.h"
#include <vector>
#include <cctype>

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
     class Preprocessor
     {
     public:
          [[nodiscard]] static std::vector<PreprocessingToken> Parse(const std::string& source);
     private:
          static bool IsDigit(const char ch) { return std::isdigit(ch); }
          static bool IsCharacterBeginning(const char ch) { return std::isalpha(ch) || ch == '_'; }
          static bool IsCharacterContinuation(const char ch) { return std::isalnum(ch) || ch == '_'; }
     };
} // namespace ecpps