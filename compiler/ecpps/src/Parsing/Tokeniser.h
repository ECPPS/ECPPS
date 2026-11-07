#pragma once
#include <RuntimeAssert.h>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
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
#define LIFT(expression)                                                                                               \
     [](auto&&... args) noexcept(noexcept(expression(std::forward<decltype(args)>(args)...))) -> decltype(auto)        \
          requires requires { expression(std::forward<decltype(args)>(args)...); }                                     \
     {                                                                                                                 \
          return expression(std::forward<decltype(args)>(args)...);                                                    \
     }

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
          InternedString value{};
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
          InternedString name{};
     };

     struct Token
     {
          using TokenValue = std::variant<std::monostate, InternedString, bool, StringLiteral, char, IntegerLiteral,
                                          FloatingPointLiteral, UserDefinedLiteral>;

          TokenType type;
          TokenValue value;
          Location location;

          explicit Token(const TokenType type, TokenValue value, const Location location)
              : type(type), value(std::move(value)), location(location)
          {
          }

          [[nodiscard]] InternedString AsIdentifier(void) const
          {
               runtime_assert(this->type == TokenType::Identifier, "Token is not an identifier");
               return std::get<InternedString>(this->value);
          }

          [[nodiscard]] InternedString AsKeyword(void) const
          {
               runtime_assert(this->type == TokenType::Keyword, "Token is not a keyword");
               return std::get<InternedString>(this->value);
          }

          [[nodiscard]] InternedString AsOperator(void) const
          {
               runtime_assert(this->type == TokenType::Operator, "Token is not an operator");
               return std::get<InternedString>(this->value);
          }
          [[nodiscard]] bool IsLiteral(void) const
          {
               return this->type == TokenType::Literal;
          }

          [[nodiscard]] std::optional<InternedString> TryIdentifier(void) const
          {
               if (this->type != TokenType::Identifier) return std::nullopt;
               return std::get<InternedString>(this->value);
          }

          [[nodiscard]] std::optional<InternedString> TryKeyword(void) const
          {
               if (this->type != TokenType::Keyword) return std::nullopt;
               return std::get<InternedString>(this->value);
          }

          [[nodiscard]] std::optional<InternedString> TryOperator(void) const
          {
               if (this->type != TokenType::Operator) return std::nullopt;
               return std::get<InternedString>(this->value);
          }

          [[nodiscard]] std::string ToString(void) const
          {
               switch (this->type)
               {
               case TokenType::Identifier: return std::string(std::get<InternedString>(this->value).View());
               case TokenType::Keyword: return std::string(std::get<InternedString>(this->value).View());
               case TokenType::Operator: return std::string(std::get<InternedString>(this->value).View());
               case TokenType::Literal:
                    return std::visit(
                         OverloadedVisitor{[](const std::monostate&) -> std::string
                                           {
                                                return std::string{};
                                           },
                                           [](const InternedString str)
                                           {
                                                return std::string(str.View());
                                           },
                                           [](const bool b) -> std::string
                                           {
                                                return b ? "true" : "false";
                                           },
                                           [](const StringLiteral& strLit) -> std::string
                                           {
                                                return std::string(strLit.value.View());
                                           },
                                           [](const char c)
                                           {
                                                return std::string(1, c);
                                           },
                                           [](const IntegerLiteral& intLit) -> std::string
                                           {
                                                return std::to_string(intLit.value);
                                           },
                                           [](const FloatingPointLiteral& floatLit) -> std::string
                                           {
                                                return std::to_string(floatLit.value);
                                           },
                                           [](const UserDefinedLiteral& udLit) -> std::string
                                           {
                                                return std::visit(
                                                     OverloadedVisitor{
                                                          [](const StringLiteral& strLit) -> std::string
                                                          {
                                                               return std::format("\"{}\"", strLit.value.View());
                                                          },
                                                          [](const IntegerLiteral& intLit) -> std::string
                                                          {
                                                               return std::to_string(intLit.value);
                                                          },
                                                          [](const FloatingPointLiteral& floatLit) -> std::string
                                                          {
                                                               return std::to_string(floatLit.value);
                                                          },
                                                          [](auto&&) -> std::string
                                                          {
                                                               runtime_assert(false,
                                                                              "Invalid user-defined literal value");
                                                               std::terminate();
                                                          }},
                                                     udLit.value);
                                           }},
                         this->value);
               case TokenType::LeftParenthesis: return "(";
               case TokenType::RightParenthesis: return ")";
               case TokenType::LeftBracket: return "[";
               case TokenType::RightBracket: return "]";
               case TokenType::LeftBrace: return "{";
               case TokenType::RightBrace: return "}";
               case TokenType::SemiColon: return ";";
               case TokenType::Colon: return ":";
               default: return "?";
               }
          }
     };

     class Tokeniser
     {
     public:
          explicit Tokeniser(void) = delete;

          static std::vector<Token> Tokenise(const std::vector<PreprocessingToken>& ppTokens, StringPool& stringPool);
          static void Print(const std::vector<Token>& tokens);

     private:
          static bool IsKeyword(std::string_view identifier);

          static std::optional<std::uintmax_t> ParseInteger(const std::string_view str)
          {
               std::size_t idx = 0;
               try
               {
                    auto val = std::stoull(std::string{str}, &idx, 0);
                    if (idx == str.size()) return val;
               }
               catch (...) // NOLINT(bugprone-empty-catch)
               {
               }
               return std::nullopt;
          }

          static std::optional<long double> ParseFloat(const std::string_view str)
          {
               std::size_t idx = 0;
               try
               {
                    auto val = std::stold(std::string{str}, &idx);
                    if (idx == str.size()) return val;
               }
               catch (...) // NOLINT(bugprone-empty-catch)
               {
               }
               return std::nullopt;
          }
     };
} // namespace ecpps
