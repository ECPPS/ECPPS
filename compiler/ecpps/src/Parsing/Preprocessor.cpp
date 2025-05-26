#include "Preprocessor.h"
#include <cctype>

std::vector<ecpps::PreprocessingToken> ecpps::Preprocessor::Parse(const std::string& source)
{
     std::vector<ecpps::PreprocessingToken> tokens{};
     Location location{0, 0};

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

          if (IsCharacterBeginning(character))
          {
               std::string identifier{character};
               while (sourceIterator != source.end() &&
                      (character = *std::next(sourceIterator), IsCharacterContinuation(character)))
               {
                    identifier += character;
                    ++sourceIterator;
               }
               tokens.emplace_back(PreprocessingTokenType::Identifier, identifier, location);
          }
          else if (IsDigit(character))
          {
               std::string numeric{ character };
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

               tokens.emplace_back(PreprocessingTokenType::Number, numeric, location);
          }
          else if (character == '#') // preprocessing
          {

          }
     }

     return tokens;
}
