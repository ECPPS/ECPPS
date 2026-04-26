#include "Preprocessor.h"
#include <FileSystem/SourceScanner.h>
#include <RuntimeAssert.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <print>
#include <unordered_map>
#include <unordered_set>
std::vector<ecpps::PreprocessingToken> ecpps::Preprocessor::Parse(const std::string& source,
                                                                  std::vector<MacroReplacement>& macros,
                                                                  const std::string& fileName,
                                                                  std::set<std::filesystem::path>& includedFiles,
                                                                  const std::vector<std::string>& includeDirectories)
{
     std::vector<ecpps::PreprocessingToken> tokens{};
     Location location{1, 0, 0};
     auto Advance = [&](auto& it)
     {
          const char c = *it;
          ++it;

          if (c == '\r')
          {
               if (it != source.end() && *it == '\n') ++it;
               ++location.line;
               location.position = 0;
          }
          else if (c == '\n')
          {
               ++location.line;
               location.position = 0;
          }
          else
               location.position++;
     };

     for (auto sourceIterator = source.begin(); sourceIterator != source.end(); ++sourceIterator)
     {
          auto character = *sourceIterator;

          location.position++;
          if (character == '\r' || character == '\n')
          {
               if (character == '\r' && std::next(sourceIterator) != source.end() && *std::next(sourceIterator) == '\n')
                    ++sourceIterator;
               location.line++;
               location.position = 0;
               for (auto& macro : macros)
                    if (macro.name == "__LINE__") macro.contents = std::to_string(location.line);
               continue;
          }
          if (std::isspace(character) != 0) continue;

          if (character == '#' && location.position == 1)
          {
               auto directiveIt = std::next(sourceIterator);
               location.position++;
               while (directiveIt != source.end() && (*directiveIt == ' ' || *directiveIt == '\t'))
               {
                    ++directiveIt;
                    location.position++;
               }

               std::string directive;
               while (directiveIt != source.end() && IsCharacterContinuation(*directiveIt))
               {
                    directive += *directiveIt++;
                    location.position++;
               }

               sourceIterator = directiveIt;
               if (directive.empty()) continue;
               else if (directive == "pragma")
               {
                    std::string pragmaContent;
                    while (sourceIterator != source.end() && (*sourceIterator == ' ' || *sourceIterator == '\t'))
                         ++sourceIterator;

                    while (sourceIterator != source.end() && *sourceIterator != '\n' && *sourceIterator != '\r')
                    {
                         pragmaContent += *sourceIterator;
                         Advance(sourceIterator);
                    }
                    if (pragmaContent == "once")
                    {
                         const auto canonicalPath = canonical(std::filesystem::path(fileName));
                         if (includedFiles.contains(canonicalPath)) return tokens;
                         includedFiles.insert(canonical(std::filesystem::path(fileName)));
                    }
                    else
                         std::println("Warning: Unrecognized pragma '{}'", pragmaContent);
               }
               else if (directive == "include")
               {
                    std::string header;
                    while (sourceIterator != source.end() && (*sourceIterator == ' ' || *sourceIterator == '\t'))
                         ++sourceIterator;

                    if (sourceIterator == source.end()) break;

                    char delimiter = *sourceIterator++;
                    char closing = (delimiter == '<') ? '>' : '"';

                    if (delimiter == '<' || delimiter == '"')
                    {
                         while (sourceIterator != source.end() && *sourceIterator != closing)
                         {
                              header += *sourceIterator++;
                         }
                         if (sourceIterator != source.end()) ++sourceIterator; // skip closing delimiter
                    }

                    std::filesystem::path resolvedPath = ecpps::fs::GetSourceScanner().ResolveInclude(
                        fileName, header,
                        (delimiter == '"') ? ecpps::fs::IncludeType::Local : ecpps::fs::IncludeType::System);
                    if (std::ranges::find(includedFiles, resolvedPath) == includedFiles.end())
                    {
                         const auto& includedSource = ecpps::fs::GetSourceScanner().GetFileContents(resolvedPath);

                         tokens.append_range(
                             Parse(includedSource, macros, resolvedPath.string(), includedFiles, includeDirectories));
                    }
               }
               else if (directive == "define")
               {
                    while (sourceIterator != source.end() && (std::isspace(*sourceIterator) != 0)) ++sourceIterator;

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
                              else
                                   currentParam += *sourceIterator;
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

                    std::string contents;
                    while (sourceIterator != source.end() && *sourceIterator != '\n' && *sourceIterator != '\r')
                    {
                         contents += *sourceIterator;
                         Advance(sourceIterator);
                    }
                    auto last = contents.find_last_not_of(" \t");
                    if (last != std::string::npos) contents.erase(last + 1);
                    else
                         contents.clear();

                    macros.emplace_back(macroName, parameters, contents, isVariadic);
               }
               else if (directive == "ifdef" || directive == "ifndef")
               {
                    bool isIfndef = (directive == "ifndef");

                    // Parse macro name
                    while (sourceIterator != source.end() && (std::isspace(*sourceIterator) != 0)) ++sourceIterator;
                    std::string macroName{};
                    while (sourceIterator != source.end() && IsCharacterContinuation(*sourceIterator))
                         macroName += *sourceIterator++;

                    bool conditionMet = isIfndef ? std::ranges::none_of(macros, [&macroName](const MacroReplacement& m)
                                                                        { return m.name == macroName; })
                                                 : std::ranges::any_of(macros, [&macroName](const MacroReplacement& m)
                                                                       { return m.name == macroName; });

                    std::vector<PreprocessingToken> branchTokens;
                    bool inElse = false;
                    std::string builtSource{};
                    const auto previousLine = location.line;
                    bool wasAnyBranchTaken = conditionMet;
                    while (sourceIterator != source.end())
                    {
                         char c = *sourceIterator;
                         if (c == '#')
                         {
                              auto peekIt = sourceIterator;
                              Advance(peekIt);
                              while (peekIt != source.end() && (std::isspace(*peekIt) != 0)) Advance(peekIt);

                              std::string nextDirective;
                              while (peekIt != source.end() && IsCharacterContinuation(*peekIt))
                              {
                                   nextDirective += *peekIt;
                                   Advance(peekIt);
                              }

                              if (nextDirective == "else")
                              {
                                   inElse = true;
                                   sourceIterator = peekIt;
                                   Advance(sourceIterator);
                                   continue;
                              }
                              if (nextDirective == "endif")
                              {
                                   sourceIterator = peekIt;
                                   break;
                              }
                              if (nextDirective == "elif")
                              {
                                   inElse = false;
                                   conditionMet = false;

                                   while (sourceIterator != source.end() && (std::isspace(*sourceIterator) != 0))
                                        Advance(sourceIterator);

                                   std::string elifCondition;
                                   while (sourceIterator != source.end() && IsCharacterContinuation(*sourceIterator))
                                        elifCondition += *sourceIterator++;

                                   if (!elifCondition.empty())
                                   {
                                        auto it =
                                            std::ranges::find_if(macros, [&elifCondition](const MacroReplacement& m)
                                                                 { return m.name == elifCondition; });
                                        conditionMet = (it != macros.end());
                                        if (conditionMet) wasAnyBranchTaken = true;
                                   }
                                   Advance(sourceIterator);
                                   continue;
                              }
                              if (nextDirective == "elifdef" || nextDirective == "elifndef")
                              {
                                   inElse = false;
                                   conditionMet = false;

                                   bool isElifndef = (nextDirective == "elifndef");

                                   while (sourceIterator != source.end() && (std::isspace(*sourceIterator) != 0))
                                        Advance(sourceIterator);

                                   std::string elifMacroName;
                                   while (sourceIterator != source.end() && IsCharacterContinuation(*sourceIterator))
                                        elifMacroName += *sourceIterator++;

                                   if (!elifMacroName.empty())
                                   {
                                        auto it =
                                            std::ranges::find_if(macros, [&elifMacroName](const MacroReplacement& m)
                                                                 { return m.name == elifMacroName; });
                                        conditionMet = isElifndef ? (it == macros.end()) : (it != macros.end());
                                        if (conditionMet) wasAnyBranchTaken = true;
                                   }
                                   Advance(sourceIterator);
                                   continue;
                              }
                         }

                         if (conditionMet && !inElse) builtSource += c;
                         else if (!wasAnyBranchTaken && inElse)
                              builtSource += c;
                         Advance(sourceIterator);
                    }
                    auto parsed = Parse(builtSource, macros, fileName, includedFiles, includeDirectories);
                    for (auto& token : parsed) token.source.line += previousLine - 1;

                    tokens.reserve(tokens.size() + parsed.size());
                    tokens.insert_range(tokens.end(), parsed);

                    // TODO: handle nested #if/#ifdef recursively
               }
               else if (directive == "undef")
               {
                    while (sourceIterator != source.end() && (std::isspace(*sourceIterator) != 0)) ++sourceIterator;

                    std::string macroName;
                    while (sourceIterator != source.end() && IsCharacterContinuation(*sourceIterator))
                    {
                         macroName += *sourceIterator++;
                         location.position++;
                    }

                    auto it = std::ranges::find_if(macros, [&macroName](const MacroReplacement& m)
                                                   { return m.name == macroName; });
                    if (it != macros.end()) macros.erase(it);
               }
               else if (directive == "error")
               {
                    std::string errorMessage;
                    while (sourceIterator != source.end() && *sourceIterator != '\n' && *sourceIterator != '\r')
                    {
                         errorMessage += *sourceIterator;
                         Advance(sourceIterator);
                    }
                    throw std::runtime_error("preprocessor error: " + errorMessage);
               }
               else
                    throw std::runtime_error(std::format("unknown preprocessor directive: {}", directive));

               if (sourceIterator != source.begin()) --sourceIterator;
               location.position = 0;
               continue;
          }

          if (IsCharacterBeginning(character))
          {
               std::string identifier{character};
               while (sourceIterator != source.end() && std::next(sourceIterator) != source.end() &&
                      (character = *std::next(sourceIterator), IsCharacterContinuation(character)))
               {
                    identifier += character;
                    ++sourceIterator;
               }
               location.endPosition = location.position;
               auto it = std::ranges::find_if(macros, [&identifier](const MacroReplacement& m)
                                              { return m.name == identifier; });

               if (it != macros.end())
               {
                    if (it->Type() == ecpps::MacroReplacementType::FunctionLike)
                    {
                         auto peekIt = std::next(sourceIterator);
                         while (peekIt != source.end() && (*peekIt == ' ' || *peekIt == '\t')) Advance(peekIt);

                         if (it->parameters && peekIt != source.end() && *peekIt == '(')
                         {
                              ++peekIt; // skip '('
                              std::vector<std::vector<PreprocessingToken>> arguments;
                              int parenLevel = 1;
                              std::vector<std::string> rawArgs;
                              std::string currentRawArg;

                              auto argIt = peekIt;
                              while (argIt != source.end())
                              {
                                   char c = *argIt;
                                   if (c == '(')
                                   {
                                        parenLevel++;
                                        currentRawArg += c;
                                   }
                                   else if (c == ')')
                                   {
                                        parenLevel--;
                                        if (parenLevel == 0)
                                        {
                                             rawArgs.push_back(currentRawArg);
                                             sourceIterator = argIt;
                                             break;
                                        }
                                        currentRawArg += c;
                                   }
                                   else if (c == ',' && parenLevel == 1)
                                   {
                                        rawArgs.push_back(currentRawArg);
                                        currentRawArg.clear();
                                   }
                                   else
                                        currentRawArg += c;
                                   ++argIt;
                              }
                              for (const auto& rawArg : rawArgs)
                              {
                                   std::vector<MacroReplacement> macrosCopy = macros;
                                   auto argTokens = Preprocessor::Parse(rawArg, macrosCopy, "", includedFiles);
                                   arguments.push_back(std::move(argTokens));
                              }

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
               if (suffixIt != source.end() && ((std::isalpha(*suffixIt) != 0) || *suffixIt == '_'))
               {
                    std::string suffix;
                    auto it = suffixIt;

                    while (it != source.end() && ((std::isalnum(*it) != 0) || *it == '_'))
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
               location.line++;

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
                        *std::next(sourceIterator, static_cast<std::streamsize>(delimiter.size())) == '"')
                    {
                         literal += ')';
                         for (std::size_t i = 0; i < delimiter.size(); i++) literal += *++sourceIterator;
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
               while (sourceIterator != source.end() && std::next(sourceIterator) != source.end() &&
                      IsOperatorOrPunctuator(operatorOrPunctuator + *std::next(sourceIterator)))
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
     const auto maxLine = ppTokens.back().source.line;
     std::size_t maxLineWidth = ecpps::DigitCount(maxLine) + 1uz;

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

          std::print("{:<{}}{}{}{}", token.source.line, maxLineWidth, spaces, colour, token.value);
     }
     std::println("\x1b[0m");
}

bool ecpps::Preprocessor::IsOperatorOrPunctuator([[maybe_unused]] const std::string& string)
{
     static std::unordered_set<std::string> OperatorsAndPunctuators{
         "{",  "}",  "[", "]", "(",  ")",  ";",   ":",  "...", "?",  "::", ".",   ".*",  "->", "->*", "~",  "!",
         "+",  "-",  "*", "/", "%",  "^",  "&",   "|",  "=",   "+=", "-=", "*=",  "/=",  "%=", "^=",  "&=", "|=",
         "==", "!=", "<", ">", "<=", ">=", "<=>", "&&", "||",  "<<", ">>", "<<=", ">>=", "++", "--",  ","};

     return OperatorsAndPunctuators.contains(string);
     // return false;
}

bool ecpps::Preprocessor::IsOperatorOrPunctuatorBeginning(char ch)
{
     static std::unordered_set<char> OperatorCharacters = {'{', '}', '[', ']', '(', ')', '.', '-', '~', '!', ';', ':',
                                                           '+', '?', '*', '/', '%', '^', '&', '|', '<', '>', '=', ','};

     return OperatorCharacters.contains(ch);
}

static std::string ExpandMacroString(const std::string& contents,
                                     const std::unordered_map<std::string, std::string>& parameterMap,
                                     const std::unordered_map<std::string, std::string>& rawParameterMap)
{
     std::string result{};
     for (std::size_t i = 0; i < contents.size(); i++)
     {
          if (i + 1 < contents.size() && contents[i] == '#' && contents[i + 1] == '#')
          {
               std::size_t l = result.size();
               while (l > 0 && std::isspace(static_cast<unsigned char>(result[l - 1]))) --l;
               std::string left = result.substr(l);
               result.erase(l);

               std::size_t r = i + 2;
               while (r < contents.size() && std::isspace(static_cast<unsigned char>(contents[r]))) ++r;
               std::string right;
               while (r < contents.size() &&
                      (std::isalnum(static_cast<unsigned char>(contents[r])) || contents[r] == '_'))
                    right += contents[r++];

               if (rawParameterMap.contains(right)) right = rawParameterMap.at(right);

               result += left + right;
               i = r - 1;
               continue;
          }

          if (contents[i] == '#' && i + 1 < contents.size())
          {
               std::size_t j = i + 1;
               while (j < contents.size() && std::isspace(static_cast<unsigned char>(contents[j]))) ++j;
               std::string param;
               while (j < contents.size() &&
                      (std::isalnum(static_cast<unsigned char>(contents[j])) || contents[j] == '_'))
                    param += contents[j++];
               if (rawParameterMap.contains(param))
               {
                    result += '"' + rawParameterMap.at(param) + '"';
                    i = j - 1;
                    continue;
               }
          }

          if ((std::isalnum(contents[i]) != 0) || contents[i] == '_')
          {
               std::string token;
               std::size_t j = i;
               while (j < contents.size() && ((std::isalnum(contents[j]) != 0) || contents[j] == '_'))
                    token += contents[j++];

               result += parameterMap.contains(token) ? parameterMap.at(token) : token;
               i = j - 1;
               continue;
          }

          result += contents[i];
     }

     return result;
}

static std::vector<ecpps::PreprocessingToken> TokeniseExpandedMacro(const std::string& expanded,
                                                                    const ecpps::Location& location,
                                                                    const std::vector<ecpps::MacroReplacement>& macros)
{
     std::vector<ecpps::MacroReplacement> macrosCopy = macros;
     std::set<std::filesystem::path> includedFiles;
     auto tokens = ecpps::Preprocessor::Parse(expanded, macrosCopy, "", includedFiles);
     for (auto& token : tokens) token.source = location;
     return tokens;
}

std::vector<ecpps::PreprocessingToken> ecpps::MacroReplacement::ProcessObjectLike(
    const Location& location, const std::vector<ecpps::MacroReplacement>& macros) const
{
     return TokeniseExpandedMacro(ExpandMacroString(contents, {}, {}), location, macros);
}

std::vector<ecpps::PreprocessingToken> ecpps::MacroReplacement::ProcessFunctionLike(
    const std::vector<std::vector<PreprocessingToken>>& arguments, const Location& location,
    const std::vector<ecpps::MacroReplacement>& macros) const
{
     std::unordered_map<std::string, std::string> parameterMap;
     std::unordered_map<std::string, std::string> rawParameterMap;

     if (parameters)
     {
          for (std::size_t i = 0; i < parameters->size(); i++)
          {
               std::string parameterName = (*parameters)[i];
               auto first = parameterName.find_first_not_of(" \t\n\r");
               auto last = parameterName.find_last_not_of(" \t\n\r");

               if (first != std::string::npos && last != std::string::npos)
                    parameterName = parameterName.substr(first, last - first + 1);
               else
                    parameterName.clear();

               if (i < arguments.size())
               {
                    std::string argStr;
                    for (const auto& token : arguments[i]) argStr += token.value;

                    rawParameterMap[parameterName] = argStr;

                    std::vector<ecpps::MacroReplacement> macrosCopy = macros;
                    std::set<std::filesystem::path> includedFiles;
                    auto expandedTokens = ecpps::Preprocessor::Parse(argStr, macrosCopy, "", includedFiles);
                    std::string expandedArg;
                    for (const auto& tok : expandedTokens) expandedArg += tok.value;

                    parameterMap[parameterName] = expandedArg;
               }
               else
               {
                    parameterMap[parameterName] = "";
                    rawParameterMap[parameterName] = "";
               }
          }
          if (isVariadic)
          {
               std::string vargs;
               if (arguments.size() > parameters->size())
               {
                    for (std::size_t i = parameters->size(); i < arguments.size(); i++)
                    {
                         for (const auto& tok : arguments[i]) vargs += tok.value;
                         if (i + 1 < arguments.size()) vargs += ",";
                    }
               }
               parameterMap["__VA_ARGS__"] = vargs;
          }
     }

     auto expandedString = ExpandMacroString(contents, parameterMap, rawParameterMap);
     return TokeniseExpandedMacro(expandedString, location, macros);
}
