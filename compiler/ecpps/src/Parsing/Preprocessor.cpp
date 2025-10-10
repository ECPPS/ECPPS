#include "Preprocessor.h"
#include <algorithm>
#include <cctype>
#include <print>
#include <unordered_set>
#include <Assert.h>
#include "Tokeniser.h"

std::vector<ecpps::PreprocessingToken> ecpps::Preprocessor::Parse(const std::string& source,
                                                                  std::vector<MacroReplacement>& macros)
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
               for (auto& macro : macros)
                    if (macro.name == "__LINE__")
                         macro.contents = std::to_string(location.line);
               continue;
          }
          if (character == '\n')
          {
               location.line++;
               location.position = 0;
               for (auto& macro : macros)
                    if (macro.name == "__LINE__") macro.contents = std::to_string(location.line);
               continue;
          }
          if (std::isspace(character)) continue;

          if (character == '#' && location.position == 1)
          {
               ++sourceIterator;
               while (sourceIterator != source.end() && std::isspace(*sourceIterator)) ++sourceIterator;

               std::string directive;
               while (sourceIterator != source.end() && IsCharacterContinuation(*sourceIterator))
               {
                    directive += *sourceIterator;
                    ++sourceIterator;
                    location.position++;
               }

               if (directive == "include")
               {
                    // Parse header name: <...> or "..."
                    std::string header;
                    while (sourceIterator != source.end() && std::isspace(*sourceIterator)) ++sourceIterator;

                    if (sourceIterator == source.end()) break;

                    char delimiter = *sourceIterator;
                    if (delimiter == '<' || delimiter == '"')
                    {
                         ++sourceIterator;
                         while (sourceIterator != source.end() && *sourceIterator != (delimiter == '<' ? '>' : '"'))
                         {
                              header += *sourceIterator;
                              ++sourceIterator;
                         }
                         if (delimiter == '"') ++sourceIterator;                                   // skip closing '"'
                         if (delimiter == '<' && sourceIterator != source.end()) ++sourceIterator; // skip closing '>'
                    }

                    // Here: handle include (load file tokens recursively)
                    // Example: tokens = Preprocessor::ParseFile(header);
               }
               else if (directive == "define")
               {
                    // Parse macro name
                    while (sourceIterator != source.end() && std::isspace(*sourceIterator)) ++sourceIterator;

                    std::string macroName;
                    if (sourceIterator != source.end() && IsCharacterBeginning(*sourceIterator))
                    {
                         macroName += *sourceIterator++;
                         location.position++;
                         while (sourceIterator != source.end() && IsCharacterContinuation(*sourceIterator))
                         {
                              macroName += *sourceIterator++;
                              location.position++;
                         }
                    }

                    // Check for parameters (function-like)
                    std::optional<std::vector<std::string>> parameters;
                    bool isVariadic = false;
                    if (sourceIterator != source.end() && *sourceIterator == '(')
                    {
                         ++sourceIterator; // skip '('
                         std::vector<std::string> params;
                         std::string currentParam;
                         while (sourceIterator != source.end() && *sourceIterator != ')')
                         {
                              if (*sourceIterator == ',')
                              {
                                   params.push_back(currentParam);
                                   currentParam.clear();
                              }
                              else { currentParam += *sourceIterator; }
                              ++sourceIterator;
                         }
                         if (!currentParam.empty()) params.push_back(currentParam);
                         if (!params.empty() && params.back() == "...")
                         {
                              isVariadic = true;
                              params.pop_back();
                         }
                         parameters = params;
                         if (sourceIterator != source.end()) ++sourceIterator; // skip ')'
                    }

                    // Collect rest of the line as macro contents
                    std::string contents;
                    while (sourceIterator != source.end() && *sourceIterator != '\n' && *sourceIterator != '\r')
                    {
                         contents += *sourceIterator++;
                    }

                    macros.emplace_back(macroName, parameters, contents, isVariadic);
               }
               else if (directive == "ifdef" || directive == "ifndef")
               {
                    bool isIfndef = (directive == "ifndef");

                    // Parse macro name
                    while (sourceIterator != source.end() && std::isspace(*sourceIterator)) ++sourceIterator;
                    std::string macroName;
                    while (sourceIterator != source.end() && IsCharacterContinuation(*sourceIterator))
                    {
                         macroName += *sourceIterator++;
                    }

                    bool conditionMet =
                        isIfndef ? std::none_of(macros.begin(), macros.end(),
                                                [&macroName](const MacroReplacement& m) { return m.name == macroName; })
                                 : std::any_of(macros.begin(), macros.end(),
                                               [&macroName](const MacroReplacement& m) { return m.name == macroName; });

                    // Handle conditional block
                    std::vector<PreprocessingToken> branchTokens;
                    bool inElse = false;
                    std::string builtSource{};
                    while (sourceIterator != source.end())
                    {
                         char c = *sourceIterator;
                         if (c == '#'/* && location.position == 1*/)
                         {
                              // peek directive name
                              auto peekIt = sourceIterator;
                              ++peekIt;
                              while (peekIt != source.end() && std::isspace(*peekIt)) ++peekIt;

                              std::string nextDirective;
                              while (peekIt != source.end() && IsCharacterContinuation(*peekIt))
                              {
                                   nextDirective += *peekIt++;
                              }

                              if (nextDirective == "else")
                              {
                                   runtime_assert(!inElse, "Invalid #else");
                                   inElse = true;
                                   sourceIterator = peekIt;
                              }
                              else if (nextDirective == "endif")
                              {
                                   sourceIterator = peekIt;
                                   break;
                              }
                         }

                         if (conditionMet && !inElse) builtSource += c;

                         ++sourceIterator;
                         location.position++;
                    }
                    const auto parsed = Parse(builtSource, macros);
                    tokens.reserve(tokens.size() + parsed.size());
                    tokens.insert_range(tokens.end(), parsed);

                    // TODO: handle nested #if/#ifdef recursively
               }
               else if (directive == "else" || directive == "endif")
               {
                    // Handled above
               }

               continue; // directive handled
          }

          if (IsCharacterBeginning(character))
          {
               std::string identifier{character};
               while (sourceIterator != source.end() &&
                      std::next(sourceIterator) != source.end() &&
                      (character = *std::next(sourceIterator), IsCharacterContinuation(character)))
               {
                    identifier += character;
                    ++sourceIterator;
               }
               location.endPosition = location.position;
               auto it = std::find_if(macros.begin(), macros.end(),
                                      [&identifier](const MacroReplacement& m) { return m.name == identifier; });

               if (it != macros.end())
               {
                    if (it->Type() == ecpps::MacroReplacementType::FunctionLike)
                    {
                         auto peekIt = std::next(sourceIterator);
                         while (peekIt != source.end() && std::isspace(*peekIt)) ++peekIt;

                         if (it->parameters && peekIt != source.end() && *peekIt == '(')
                         {
                              ++peekIt; // skip '('
                              std::vector<std::vector<PreprocessingToken>> arguments;
                              std::vector<PreprocessingToken> currentArg;
                              int parenLevel = 1;

                              auto argIt = peekIt;
                              for (; argIt != source.end() && parenLevel > 0; ++argIt)
                              {
                                   char c = *argIt;
                                   if (c == '(')
                                   {
                                        parenLevel++;
                                        currentArg.emplace_back(PreprocessingTokenType::OperatorOrPunctuator, "(",
                                                                location);
                                   }
                                   else if (c == ')')
                                   {
                                        parenLevel--;
                                        if (parenLevel > 0)
                                             currentArg.emplace_back(PreprocessingTokenType::OperatorOrPunctuator, ")",
                                                                     location);
                                   }
                                   else if (c == ',' && parenLevel == 1)
                                   {
                                        arguments.push_back(std::move(currentArg));
                                        currentArg.clear();
                                   }
                                   else
                                   {
                                        currentArg.emplace_back(PreprocessingTokenType::Identifier, std::string(1, c),
                                                                location);
                                   }
                              }

                              if (!currentArg.empty()) arguments.push_back(std::move(currentArg));

                              sourceIterator = argIt == source.end() ? std::prev(argIt) : std::prev(argIt);

                              auto expandedTokens = it->ProcessFunctionLike(arguments, location, macros);
                              tokens.insert(tokens.end(), expandedTokens.begin(), expandedTokens.end());
                         }
                    }
                    else 
                    {
                         auto expandedTokens = it->ProcessObjectLike(location, macros);
                         tokens.insert(tokens.end(), expandedTokens.begin(), expandedTokens.end());
                    }
               }
               else
                    tokens.emplace_back(PreprocessingTokenType::Identifier, identifier, location);
          }
          else if (IsDigit(character))
          {
               std::string numeric{character};
               bool seenDot = false;
               while (sourceIterator != source.end())
               {
                    if (std::next(sourceIterator) == source.end()) break;

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
               std::string comment{"//"};
               ++sourceIterator;

               while (++sourceIterator != source.end())
               {
                    character = *sourceIterator;
                    if (character == '\n' || character == '\r') break;
                    comment += character;
               }

               location.endPosition = location.position;
          }
          else if (character == '/' && std::next(sourceIterator) != source.end() && *std::next(sourceIterator) == '*')
          {
               std::string comment{"/*"};
               ++sourceIterator;

               while (++sourceIterator != source.end())
               {
                    character = *sourceIterator;
                    comment += character;

                    if (character == '*' && std::next(sourceIterator) != source.end() &&
                        *std::next(sourceIterator) == '/')
                    {
                         ++sourceIterator;
                         comment += '/';
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
               tokens.emplace_back(PreprocessingTokenType::OperatorOrPunctuator, operatorOrPunctuator, location);
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
          case PreprocessingTokenType::Number: colour = "\x1b[36m"; break;
          case PreprocessingTokenType::OperatorOrPunctuator: colour = "\x1b[35m"; break;
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

static std::string ExpandMacroString(const std::string& contents,
                                     const std::unordered_map<std::string, std::string>& parameterMap,
                                     const std::vector<ecpps::MacroReplacement>& macros)
{
     std::string result{};
     for (std::size_t i = 0; i < contents.size(); ++i)
     {
          if (contents[i] == '#' && i + 1 < contents.size())
          {
               std::string param;
               std::size_t j = i + 1;
               while (j < contents.size() && (std::isalnum(contents[j]) || contents[j] == '_')) param += contents[j++];
               if (parameterMap.contains(param)) result += "\"" + parameterMap.at(param) + "\"";
               i = j - 1;
               continue;
          }

          // token pasting ##
          if (i + 1 < contents.size() && contents[i] == '#' && contents[i + 1] == '#')
          {
               std::string left;
               std::size_t l = result.size();
               while (l > 0 && !std::isspace(result[l - 1])) --l;
               left = result.substr(l);
               result.erase(l);

               std::string right{};
               std::size_t replacement = i + 2;
               while (replacement < contents.size() && !std::isspace(contents[replacement]) &&
                      contents[replacement] != '#')
                    right += contents[replacement++];

               if (parameterMap.contains(right)) right = parameterMap.at(right);

               result += left + right;
               i = replacement - 1;
               continue;
          }

          // parameter replacement
          if (std::isalnum(contents[i]) || contents[i] == '_')
          {
               std::string token;
               std::size_t j = i;
               while (j < contents.size() && (std::isalnum(contents[j]) || contents[j] == '_')) token += contents[j++];

               if (parameterMap.contains(token)) result += parameterMap.at(token);
               else
               {
                    // check if token is another macro (recursive expansion)
                    auto it = std::find_if(macros.begin(), macros.end(),
                                           [&token](const ecpps::MacroReplacement& m) { return m.name == token; });
                    if (it != macros.end() && it->parameters == std::nullopt) // object-like
                    {
                         result += ExpandMacroString(it->contents, parameterMap, macros);
                    }
                    else { result += token; }
               }
               i = j - 1;
               continue;
          }

          result += contents[i];
     }

     return result;
}


static std::vector<ecpps::PreprocessingToken> TokeniseExpandedMacro(const std::string& expanded,
                                                                    const ecpps::Location& location)
{
     std::vector<ecpps::MacroReplacement> macros{};
     auto tokens = ecpps::Preprocessor::Parse(expanded, macros);
     runtime_assert(macros.empty(), "Expansion cannot introduce new macros");
     for (auto& token : tokens) token.source = location;
     return tokens;
}


std::vector<ecpps::PreprocessingToken> ecpps::MacroReplacement::ProcessObjectLike(
    const Location& location, const std::vector<ecpps::MacroReplacement>& macros) const
{
     auto expandedString = ExpandMacroString(contents, {}, macros);
     return TokeniseExpandedMacro(expandedString, location);
}

std::vector<ecpps::PreprocessingToken> ecpps::MacroReplacement::ProcessFunctionLike(
    const std::vector<std::vector<PreprocessingToken>>& arguments, const Location& location,
    const std::vector<ecpps::MacroReplacement>& macros) const
{
     std::unordered_map<std::string, std::string> parameterMap;
     if (parameters)
     {
          for (std::size_t i = 0; i < parameters->size(); i++)
          {
               std::string name = (*parameters)[i];
               auto first = name.find_first_not_of(" \t\n\r");
               auto last = name.find_last_not_of(" \t\n\r");

               if (first != std::string::npos && last != std::string::npos) name = name.substr(first, last - first + 1);
               else
                    name.clear();

               if (i < arguments.size())
               {
                    std::string argStr;
                    for (const auto& token : arguments[i]) argStr += token.value;
                    parameterMap[name] = argStr;
               }
               else parameterMap[(*parameters)[i]] = "";
          }
          if (isVariadic)
          {
               std::string vargs;
               if (arguments.size() > parameters->size())
               {
                    for (std::size_t i = parameters->size(); i < arguments.size(); ++i)
                    {
                         for (const auto& tok : arguments[i]) vargs += tok.value;
                         if (i + 1 < arguments.size()) vargs += ","; 
                    }
               }
               parameterMap["__VA_ARGS__"] = vargs;
          }
     }

     auto expandedString = ExpandMacroString(contents, parameterMap, macros);
     return TokeniseExpandedMacro(expandedString, location);
}