#pragma once
#include <cstdint>
#include <string>
#include <utility>
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
     class Preprocessor
     {
     public:
     private:
     };
} // namespace ecpps