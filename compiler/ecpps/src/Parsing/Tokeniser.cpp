#include "Tokeniser.h"
#include <print>
#include <string>
#include <unordered_set>
#include "Preprocessor.h"

std::vector<ecpps::Token> ecpps::Tokeniser::Tokenise(const std::vector<PreprocessingToken>& ppTokens)
{
     std::vector<Token> tokens{};

     for (const auto& preprocessorToken : ppTokens)
     {
          switch (preprocessorToken.type)
          {
          case PreprocessingTokenType::Identifier:
          {
               if (preprocessorToken.value == "false")
                    tokens.emplace_back(TokenType::Literal, false, preprocessorToken.source);
               else if (preprocessorToken.value == "true")
                    tokens.emplace_back(TokenType::Literal, true, preprocessorToken.source);
               else if (IsKeyword(preprocessorToken.value))
                    tokens.emplace_back(TokenType::Keyword, preprocessorToken.value, preprocessorToken.source);
               else
                    tokens.emplace_back(TokenType::Identifier, preprocessorToken.value, preprocessorToken.source);
          }
          break;
          case PreprocessingTokenType::CharacterLiteral:
          {
               tokens.emplace_back(TokenType::Literal, preprocessorToken.value[0], preprocessorToken.source);
          }
          break;
          case PreprocessingTokenType::StringLiteral:
          {
               tokens.emplace_back(TokenType::Literal, StringLiteral{preprocessorToken.value},
                                   preprocessorToken.source);
          }
          break;
          case PreprocessingTokenType::Number:
          {
               const auto& str = preprocessorToken.value;

               std::size_t index{};
               bool isFloat = false;

               while (index < str.size() &&
                      (Preprocessor::IsDigit(str[index]) || str[index] == '.' || str[index] == 'e' ||
                       str[index] == 'E' || str[index] == '+' || str[index] == '-'))
               {
                    if (str[index] == '.' || str[index] == 'e' || str[index] == 'E') isFloat = true;
                    index++;
               }

               const auto numericPart = str.substr(0, index);
               const auto suffix = str.substr(index);

               if (!suffix.empty())
               {
                    if (isFloat)
                    {
                         if (auto val = ParseFloat(numericPart))
                              tokens.emplace_back(
                                  TokenType::Literal,
                                  UserDefinedLiteral{.value = FloatingPointLiteral{*val}, .name = suffix},
                                  preprocessorToken.source);
                    }
                    else
                    {
                         if (auto val = ParseInteger(numericPart))
                              tokens.emplace_back(TokenType::Literal,
                                                  UserDefinedLiteral{.value = IntegerLiteral{*val}, .name = suffix},
                                                  preprocessorToken.source);
                    }
               }
               else
               {
                    if (isFloat)
                    {
                         if (auto val = ParseFloat(numericPart))
                              tokens.emplace_back(TokenType::Literal, FloatingPointLiteral{*val},
                                                  preprocessorToken.source);
                    }
                    else
                    {
                         if (auto val = ParseInteger(numericPart))
                              tokens.emplace_back(TokenType::Literal, IntegerLiteral{*val}, preprocessorToken.source);
                    }
               }
          }
          break;
          case PreprocessingTokenType::OperatorOrPunctuator:
          {
               if (preprocessorToken.value == "(")
                    tokens.emplace_back(TokenType::LeftParenthesis, std::monostate{}, preprocessorToken.source);
               else if (preprocessorToken.value == ")")
                    tokens.emplace_back(TokenType::RightParenthesis, std::monostate{}, preprocessorToken.source);
               else if (preprocessorToken.value == "[")
                    tokens.emplace_back(TokenType::LeftBracket, std::monostate{}, preprocessorToken.source);
               else if (preprocessorToken.value == "]")
                    tokens.emplace_back(TokenType::RightBracket, std::monostate{}, preprocessorToken.source);
               else if (preprocessorToken.value == "{")
                    tokens.emplace_back(TokenType::LeftBrace, std::monostate{}, preprocessorToken.source);
               else if (preprocessorToken.value == "}")
                    tokens.emplace_back(TokenType::RightBrace, std::monostate{}, preprocessorToken.source);
               else if (preprocessorToken.value == ";")
                    tokens.emplace_back(TokenType::SemiColon, std::monostate{}, preprocessorToken.source);
               else if (preprocessorToken.value == ":")
                    tokens.emplace_back(TokenType::Colon, std::monostate{}, preprocessorToken.source);
               else
                    tokens.emplace_back(TokenType::Operator, preprocessorToken.value, preprocessorToken.source);
          }
          break;
          }
     }

     return tokens;
}

void ecpps::Tokeniser::Print(const std::vector<ecpps::Token>& tokens)
{
     using std::string_literals::operator""s;
     Location previous{0, 0, 0};

     for (const auto& token : tokens)
     {
          if (token.location.line != previous.line)
          {
               std::println();
               previous.line = token.location.line;
               previous.position = 0;
               previous.endPosition = 0;
          }
          std::string colour{};
          std::string value{};
          switch (token.type)
          {
          case TokenType::Identifier:
          {
               colour = "\x1b[37m";
               value = std::get<std::string>(token.value);
          }
          break;
          case TokenType::Keyword:
          {
               colour = "\x1b[35m";
               value = std::get<std::string>(token.value);
          }
          break;
          case TokenType::Literal:
          {
               colour = "\x1b[32m";
               value = std::visit(
                   OverloadedVisitor{
                       [](const bool& boolean) -> std::string { return boolean ? "true" : "false"; },
                       [](const StringLiteral& literal) -> std::string { return "\"" + literal.value + "\""; },
                       [](const IntegerLiteral& literal) -> std::string { return std::to_string(literal.value); },
                       [](const char literal) -> std::string { return "'"s + literal + "'"; },
                       [](const FloatingPointLiteral& literal) -> std::string { return std::to_string(literal.value); },
                       [](const UserDefinedLiteral& udl) -> std::string
                       {
                            return std::visit(
                                       OverloadedVisitor{
                                           [](const StringLiteral& literal) -> std::string
                                           { return "\"" + literal.value + "\""; },
                                           [](const IntegerLiteral& literal) -> std::string
                                           { return std::to_string(literal.value); },
                                           [](const char& literal) -> std::string { return "'"s + literal + "'"; },
                                           [](const FloatingPointLiteral& literal) -> std::string
                                           { return std::to_string(literal.value); },
                                       },
                                       udl.value) +
                                   udl.name;
                       },
                       [](auto&&) -> std::string { return "?"; }},
                   token.value);
          }
          break;
          case TokenType::Operator:
          {
               colour = "\x1b[36m";
               value = std::get<std::string>(token.value);
          }
          break;
          case TokenType::LeftParenthesis:
          {
               colour = "\x1b[37m";
               value = "(";
          }
          break;
          case TokenType::RightParenthesis:
          {
               colour = "\x1b[37m";
               value = ")";
          }
          break;
          case TokenType::LeftBracket:
          {
               colour = "\x1b[37m";
               value = "[";
          }
          break;
          case TokenType::RightBracket:
          {
               colour = "\x1b[37m";
               value = "]";
          }
          break;
          case TokenType::LeftBrace:
          {
               colour = "\x1b[37m";
               value = "{";
          }
          break;
          case TokenType::RightBrace:
          {
               colour = "\x1b[37m";
               value = "}";
          }
          break;
          case TokenType::SemiColon:
          {
               colour = "\x1b[37m";
               value = ";";
          }
          break;
          case TokenType::Colon:
          {
               colour = "\x1b[37m";
               value = ":";
          }
          break;
          default: colour = "\x1b[38m";
          }
          const std::string spaces(std::max(token.location.position - previous.endPosition, 1UZ) - 1, ' ');
          previous = token.location;

          std::print("{}{}{}", spaces, colour, value);
     }
     std::println("\x1b[0m");
}

bool ecpps::Tokeniser::IsKeyword(const std::string& identifier)
{
     static std::unordered_set<std::string> Keywords = {
         "alignas",       "alignof",     "asm",       "auto",      "bool",         "break",
         "case",          "catch",       "char",      "char8_t",   "char16_t",     "char32_t",
         "class",         "concept",     "const",     "consteval", "constexpr",    "constinit",
         "const_cast",    "continue",    "co_await",  "co_return", "co_yield",     "decltype",
         "default",       "delete",      "do",        "double",    "dynamic_cast", "else",
         "enum",          "explicit",    "export",    "extern",    "false",        "float",
         "for",           "friend",      "goto",      "if",        "inline",       "int",
         "long",          "mutable",     "namespace", "new",       "noexcept",     "nullptr",
         "operator",      "private",     "protected", "public",    "register",     "reinterpret_cast",
         "requires",      "return",      "short",     "signed",    "sizeof",       "static",
         "static_assert", "static_cast", "struct",    "switch",    "template",     "this",
         "thread_local",  "throw",       "true",      "try",       "typedef",      "typeid",
         "typename",      "union",       "unsigned",  "using",     "virtual",      "void",
         "volatile",      "wchar_t",     "while"};

     return Keywords.contains(identifier);
}
