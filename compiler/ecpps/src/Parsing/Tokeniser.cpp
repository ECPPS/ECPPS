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
               const auto& in = preprocessorToken.value;

               std::string out{};
               out.reserve(in.size());

               auto hex = [](char c) -> std::uint32_t
               {
                    if (c >= '0' && c <= '9') return static_cast<std::uint32_t>(c - '0');
                    if (c >= 'a' && c <= 'f') return 10 + static_cast<std::uint32_t>(c - 'a');
                    if (c >= 'A' && c <= 'F') return 10 + static_cast<std::uint32_t>(c - 'A');
                    return ~0u;
               };

               for (std::size_t i = 0; i < in.size(); i++)
               {
                    const char c = in[i];
                    if (c != '\\')
                    {
                         out += c;
                         continue;
                    }

                    if (++i >= in.size()) throw std::runtime_error("invalid escape");

                    switch (in[i])
                    {
                    case '\\': out += '\\'; break;
                    case '\'': out += '\''; break;
                    case '"': out += '"'; break;
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'a': out += '\a'; break;
                    case 'f': out += '\f'; break;
                    case 'v': out += '\v'; break;
                    case '0': out += '\0'; break;

                    case 'x':
                    {
                         std::uint32_t value = 0;
                         while (i + 1 < in.size())
                         {
                              std::uint32_t h = hex(in[i + 1]);
                              if (h == ~0u) break;
                              value = (value << 4) | h;
                              i++;
                         }
                         out += static_cast<char>(value);
                         break;
                    }

                    case 'u':
                    {
                         if (i + 4 >= in.size()) throw std::runtime_error("invalid \\u escape");

                         std::uint32_t value = 0;
                         for (std::size_t j = 0; j < 4; j++)
                         {
                              std::uint32_t h = hex(in[++i]);
                              if (h == ~0u) throw std::runtime_error("invalid hex");
                              value = (value << 4) | h;
                         }

                         if (value <= 0x7F) out += static_cast<char>(value);
                         else if (value <= 0x7FF)
                         {
                              out += static_cast<char>(0xC0 | (value >> 6));
                              out += static_cast<char>(0x80 | (value & 0x3F));
                         }
                         else
                         {
                              out += static_cast<char>(0xE0 | (value >> 12));
                              out += static_cast<char>(0x80 | ((value >> 6) & 0x3F));
                              out += static_cast<char>(0x80 | (value & 0x3F));
                         }

                         break;
                    }

                    case 'U':
                    {
                         if (i + 8 >= in.size()) throw std::runtime_error("invalid \\U escape");

                         std::uint32_t value = 0;
                         for (std::size_t j = 0; j < 8; j++)
                         {
                              std::uint32_t h = hex(in[++i]);
                              if (h == ~0u) throw std::runtime_error("invalid hex");
                              value = (value << 4) | h;
                         }

                         if (value <= 0x7F) out += static_cast<char>(value);
                         else if (value <= 0x7FF)
                         {
                              out += static_cast<char>(0xC0 | (value >> 6));
                              out += static_cast<char>(0x80 | (value & 0x3F));
                         }
                         else if (value <= 0xFFFF)
                         {
                              out += static_cast<char>(0xE0 | (value >> 12));
                              out += static_cast<char>(0x80 | ((value >> 6) & 0x3F));
                              out += static_cast<char>(0x80 | (value & 0x3F));
                         }
                         else
                         {
                              out += static_cast<char>(0xF0 | (value >> 18));
                              out += static_cast<char>(0x80 | ((value >> 12) & 0x3F));
                              out += static_cast<char>(0x80 | ((value >> 6) & 0x3F));
                              out += static_cast<char>(0x80 | (value & 0x3F));
                         }

                         break;
                    }

                    default: out += in[i];
                    }
               }

               tokens.emplace_back(TokenType::Literal, StringLiteral{out}, preprocessorToken.source);
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
     constexpr std::string_view Reset = "\x1b[0m";

     if (tokens.empty())
     {
          std::println("<no tokens>");
          return;
     }

     const auto maxLine = std::ranges::max(tokens, {},
                                           [](const auto& token)
                                           {
                                                return token.location.line;
                                           })
                               .location.line;

     const std::size_t lineNumberWidth = ecpps::DigitCount(maxLine);

     const auto formatLiteral = [](const auto& literal) -> std::string
     {
          return std::visit(
               OverloadedVisitor{[](const bool value) -> std::string
                                 {
                                      return value ? "true" : "false";
                                 },
                                 [](const StringLiteral& value) -> std::string
                                 {
                                      return "\"" + value.value + "\"";
                                 },
                                 [](const IntegerLiteral& value) -> std::string
                                 {
                                      return std::to_string(value.value);
                                 },
                                 [](const char value) -> std::string
                                 {
                                      return std::format("'{}'", value);
                                 },
                                 [](const FloatingPointLiteral& value) -> std::string
                                 {
                                      return std::to_string(value.value);
                                 },
                                 [](const UserDefinedLiteral& value) -> std::string
                                 {
                                      return std::visit(
                                                  OverloadedVisitor{
                                                       [](const StringLiteral& literal) -> std::string
                                                       {
                                                            return "\"" + literal.value + "\"";
                                                       },
                                                       [](const IntegerLiteral& literal) -> std::string
                                                       {
                                                            return std::to_string(literal.value);
                                                       },
                                                       [](const char literal) -> std::string
                                                       {
                                                            return std::format("'{}'", literal);
                                                       },
                                                       [](const FloatingPointLiteral& literal) -> std::string
                                                       {
                                                            return std::to_string(literal.value);
                                                       },
                                                  },
                                                  value.value) +
                                             value.name;
                                 },
                                 [](auto&&) -> std::string
                                 {
                                      return "?";
                                 }},
               literal);
     };

     const auto getTokenText = [&formatLiteral](const Token& token) -> std::string
     {
          switch (token.type)
          {
          case TokenType::Identifier:
          case TokenType::Keyword:
          case TokenType::Operator: return std::get<std::string>(token.value);
          case TokenType::Literal: return formatLiteral(token.value);
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
     };

     const auto getTokenColour = [](const TokenType type) -> std::string_view
     {
          switch (type)
          {
          case TokenType::Identifier: return "\x1b[37m";
          case TokenType::Keyword: return "\x1b[35m";
          case TokenType::Literal: return "\x1b[32m";
          case TokenType::Operator: return "\x1b[36m";
          case TokenType::LeftParenthesis:
          case TokenType::RightParenthesis:
          case TokenType::LeftBracket:
          case TokenType::RightBracket:
          case TokenType::LeftBrace:
          case TokenType::RightBrace:
          case TokenType::SemiColon:
          case TokenType::Colon: return "\x1b[37m";

          default: return "\x1b[37m";
          }
     };

     std::size_t currentLine = 0;
     std::size_t currentColumn = 0;

     for (const auto& token : tokens)
     {
          const auto line = token.location.line;
          const auto position = token.location.position;

          if (line != currentLine)
          {
               if (currentLine != 0) std::println("{}", Reset);

               currentLine = line;
               currentColumn = 0;

               std::print("{:>{}}  ", line, lineNumberWidth);
          }

          const auto spaces = position > currentColumn ? position - currentColumn : 0;
          if (spaces != 0) std::print("{}", std::string(spaces, ' '));
          const auto value = getTokenText(token);
          std::print("{}{}{}", getTokenColour(token.type), value, Reset);

          currentColumn = std::max(currentColumn, position + value.size());
     }

     std::println();
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
