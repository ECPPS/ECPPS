#include "Preprocessor.h"
#include <cctype>
#include <unordered_set>
#include <print>

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

               location.endPosition = location.position;
               tokens.emplace_back(PreprocessingTokenType::Number, numeric, location);
          }
          else if (character == '#') // preprocessing
          {
          }
          else if (IsOperatorOrPunctuatorBeginning(character))
          {
               std::string operatorOrPunctuator{ character };
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
         "==", "!=", "<", ">", "<=", ">=", "<=>", "&&", "||",  "<<", ">>", "<<=", ">>=", "++", "--",  "," };

     return false;
}

bool ecpps::Preprocessor::IsOperatorOrPunctuatorBeginning(char ch)
{
     static std::unordered_set<char> OperatorCharacters = { '{', '}', '[', ']', '(', ')', '.', '-', '~', '!', ';', ':',
                                                           '+', '?', '*', '/', '%', '^', '&', '|', '<', '>', '=', ',' };

     return OperatorCharacters.contains(ch);
}
