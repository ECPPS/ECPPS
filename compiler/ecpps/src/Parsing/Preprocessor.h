#pragma once
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "../Shared/Error.h"

namespace ecpps
{
     enum struct PreprocessingTokenType : std::uint_fast8_t
     {
          Identifier,
          Number,
          CharacterLiteral,
          StringLiteral,
          OperatorOrPunctuator
     };
     struct PreprocessingToken
     {
          PreprocessingTokenType type;
          std::string value;
          Location source;

          explicit PreprocessingToken(const PreprocessingTokenType type, std::string value, const Location source)
              : type(type), value(std::move(value)), source(source)
          {
          }
     };
     enum struct MacroReplacementType : bool
     {
          FunctionLike = true,
          ObjectLike = false
     };

     struct MacroReplacement
     {
          std::string name;
          std::optional<std::vector<std::string>> parameters;
          std::string contents;
          bool isVariadic;

          [[nodiscard]] MacroReplacementType Type(void) const noexcept
          {
               return static_cast<MacroReplacementType>(this->parameters != std::nullopt);
          }

          [[nodiscard]] std::vector<PreprocessingToken> ProcessObjectLike(
              const Location& location, const std::vector<ecpps::MacroReplacement>& macros) const;
          [[nodiscard]] std::vector<PreprocessingToken> ProcessFunctionLike(
              const std::vector<std::vector<PreprocessingToken>>& arguments, const Location& location,
              const std::vector<ecpps::MacroReplacement>& macros) const;

          explicit MacroReplacement(std::string name, std::optional<std::vector<std::string>> parameters,
                                    std::string contents, const bool isVariadic)
              : name(std::move(name)), parameters(std::move(parameters)), contents(std::move(contents)),
                isVariadic(isVariadic)
          {
          }
     };
     class Tokeniser;
     class Preprocessor
     {
     public:
          [[nodiscard]] static std::vector<PreprocessingToken> Parse(const std::string& source,
                                                                     std::vector<MacroReplacement>& macros);
          static void Print(const std::vector<PreprocessingToken>& ppTokens);

     private:
          static bool IsDigit(const char ch) { return std::isdigit(ch) != 0; }
          static bool IsCharacterBeginning(const char ch) { return std::isalpha(ch) != 0 || ch == '_'; }
          static bool IsCharacterContinuation(const char ch) { return std::isalnum(ch) != 0 || ch == '_'; }
          static bool IsOperatorOrPunctuator(const std::string& string);
          static bool IsOperatorOrPunctuatorBeginning(char ch);

          friend class Tokeniser;
     };
} // namespace ecpps