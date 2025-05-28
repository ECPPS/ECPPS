#include "Preprocessor.h"
#include <cctype>
#include <print>
#include <unordered_set>
#include "Tokeniser.h"

std::vector<ecpps::PreprocessingToken> ecpps::Preprocessor::Parse(const std::string& source)
{
     std::vector<ecpps::PreprocessingToken> tokens{};
     Location location{0, 0, 0};

     for (auto sourceIterator = source.begin(); sourceIterator != source.end(); ++sourceIterator)
     {
          auto character = *sourceIterator;

          location.position++;
          if (character == '\r')
          {
               if (sourceIterator != source.end() && *std::next(sourceIterator) == '\n') ++sourceIterator;
               location.line++;
               location.position = 0;
               continue;
          }
          if (character == '\n')
          {
               location.line++;
               location.position = 0;
               continue;
          }
          if (std::isspace(character)) continue;

          if (IsCharacterBeginning(character))
          {
               std::string identifier{character};
               while (sourceIterator != source.end() &&
                      (character = *std::next(sourceIterator), IsCharacterContinuation(character)))
               {
                    identifier += character;
                    ++sourceIterator;
               }
               location.endPosition = location.position;
               tokens.emplace_back(PreprocessingTokenType::Identifier, identifier, location);
          }
          else if (IsDigit(character))
          {
               std::string numeric{character};
               bool seenDot = false;
               while (sourceIterator != source.end())
               {
                    auto peek = *std::next(sourceIterator);
                    if (peek == '\'')
                    {
                         auto nextPeekIt = std::next(sourceIterator, 2);
                         if (nextPeekIt == source.end() || !IsDigit(*nextPeekIt)) break;
                         ++sourceIterator;
                         continue;
                    }
                    if (peek == '.' && !seenDot)
                    {
                         seenDot = true;
                         ++sourceIterator;
                         numeric += '.';
                         continue;
                    }
                    if (!IsDigit(peek)) break;

                    ++sourceIterator;
                    numeric += peek;
               }

               auto suffixIt = std::next(sourceIterator);
               if (suffixIt != source.end() && (std::isalpha(*suffixIt) || *suffixIt == '_'))
               {
                    std::string suffix;
                    auto it = suffixIt;

                    while (it != source.end() && (std::isalnum(*it) || *it == '_'))
                    {
                         suffix += *it;
                         ++it;
                    }

                    if (!suffix.empty())
                    {
                         numeric += suffix;
                         std::advance(sourceIterator, suffix.length());
                    }
               }

               location.endPosition = location.position;
               tokens.emplace_back(PreprocessingTokenType::Number, numeric, location);
          }
          else if (character == '"' || character == '\'')
          {
               const bool isChar = character == '\'';
               const char delimiter = character;

               std::string literal{};
               bool escaped = false;

               while (++sourceIterator != source.end())
               {
                    character = *sourceIterator;
                    literal += character;

                    if (escaped)
                    {
                         escaped = false;
                         continue;
                    }

                    if (character == '\\')
                    {
                         escaped = true;
                         continue;
                    }

                    if (character == delimiter)
                    {
                         literal.pop_back();
                         break;
                    }

                    if (character == '\n' || character == '\r')
                    {
                         // TODO: Error
                         break;
                    }
               }

               location.endPosition = location.position;
               tokens.emplace_back(isChar ? PreprocessingTokenType::CharacterLiteral
                                          : PreprocessingTokenType::StringLiteral,
                                   literal, location);
          }
          else if (character == '/' && std::next(sourceIterator) != source.end() && *std::next(sourceIterator) == '/')
          {
               std::string comment{ "//" };
               ++sourceIterator;

               while (++sourceIterator != source.end())
               {
                    character = *sourceIterator;
                    if (character == '\n' || character == '\r') break;
                    comment += character;
               }

               location.endPosition = location.position;
          }
          else if(character == '/' && std::next(sourceIterator) != source.end() && *std::next(sourceIterator) == '*')
          {
               std::string comment{ "/*" };
               ++sourceIterator;

               bool closed = false;
               while (++sourceIterator != source.end())
               {
                    character = *sourceIterator;
                    comment += character;

                    if (character == '*' && std::next(sourceIterator) != source.end() &&
                        *std::next(sourceIterator) == '/')
                    {
                         ++sourceIterator;
                         comment += '/';
                         closed = true;
                         break;
                    }
               }

               location.endPosition = location.position;
          }
          else if (*sourceIterator == 'R' && std::next(sourceIterator) != source.end() &&
                   *std::next(sourceIterator) == '"')
          {
               // Raw string literal
               ++sourceIterator; // Skip 'R'
               std::string literal{"R\""};
               ++sourceIterator; // Skip opening "

               std::string delimiter;
               while (sourceIterator != source.end() && *sourceIterator != '(')
               {
                    delimiter += *sourceIterator;
                    literal += *sourceIterator;
                    ++sourceIterator;
               }

               if (sourceIterator == source.end()) break;

               literal += '(';
               ++sourceIterator;

               while (sourceIterator != source.end())
               {
                    if (*sourceIterator == ')' &&
                        std::string_view{&*std::next(sourceIterator), delimiter.size()} == delimiter &&
                        *std::next(sourceIterator, delimiter.size()) == '"')
                    {
                         literal += ')';
                         for (std::size_t i = 0; i < delimiter.size(); ++i) literal += *++sourceIterator;
                         literal += *++sourceIterator;
                         break;
                    }

                    literal += *sourceIterator;
                    ++sourceIterator;
               }

               location.endPosition = location.position;
               tokens.emplace_back(PreprocessingTokenType::StringLiteral, literal, location);
          }
          else if (character == '#') // preprocessing
          {
          }
          else if (IsOperatorOrPunctuatorBeginning(character))
          {
               std::string operatorOrPunctuator{character};
               while (sourceIterator != source.end() && IsOperatorOrPunctuator(operatorOrPunctuator + *sourceIterator))
               {
                    ++sourceIterator;
                    operatorOrPunctuator += *sourceIterator;
               }

               location.endPosition = location.position;
               tokens.emplace_back(PreprocessingTokenType::OperatorOrPuncturator, operatorOrPunctuator, location);
          }
     }

     return tokens;
}

void ecpps::Preprocessor::Print(const std::vector<PreprocessingToken>& ppTokens)
{
     Location previous{0, 0, 0};

     for (const auto& token : ppTokens)
     {
          if (token.source.line != previous.line)
          {
               std::println();
               previous.line = token.source.line;
               previous.position = 0;
               previous.endPosition = 0;
          }
          std::string colour{};
          switch (token.type)
          {
          case PreprocessingTokenType::Identifier: colour = "\x1b[37m"; break;
          case PreprocessingTokenType::CharacterLiteral: colour = "\x1b[31m"; break;
          case PreprocessingTokenType::StringLiteral: colour = "\x1b[32m"; break;
          case PreprocessingTokenType::OperatorOrPuncturator: colour = "\x1b[35m"; break;
          }
          const std::string spaces(token.source.position - previous.endPosition - 1, ' ');
          previous = token.source;

          std::print("{}{}{}", spaces, colour, token.value);
     }
     std::println("\x1b[0m");
}

bool ecpps::Preprocessor::IsOperatorOrPunctuator(const std::string& string)
{
     static std::unordered_set<std::string> OperatorsAndPunctuators{
         "{",  "}",  "[", "]", "(",  ")",  ";",   ":",  "...", "?",  "::", ".",   ".*",  "->", "->*", "~",  "!",
         "+",  "-",  "*", "/", "%",  "^",  "&",   "|",  "=",   "+=", "-=", "*=",  "/=",  "%=", "^=",  "&=", "|=",
         "==", "!=", "<", ">", "<=", ">=", "<=>", "&&", "||",  "<<", ">>", "<<=", ">>=", "++", "--",  ","};

     return false;
}

bool ecpps::Preprocessor::IsOperatorOrPunctuatorBeginning(char ch)
{
     static std::unordered_set<char> OperatorCharacters = {'{', '}', '[', ']', '(', ')', '.', '-', '~', '!', ';', ':',
                                                           '+', '?', '*', '/', '%', '^', '&', '|', '<', '>', '=', ','};

     return OperatorCharacters.contains(ch);
}
