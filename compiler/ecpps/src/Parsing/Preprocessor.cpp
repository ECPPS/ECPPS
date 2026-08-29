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

static std::string ReadEscapedLiteral(std::string::const_iterator& it, const std::string::const_iterator& end,
                                      char delimiter)
{
     std::string result;
     bool escaped = false;

     while (++it != end)
     {
          char character = *it;

          if (escaped)
          {
               switch (character)
               {
               case 'n': result += '\n'; break;
               case 'r': result += '\r'; break;
               case 't': result += '\t'; break;
               case '\\': result += '\\'; break;
               case '"': result += '"'; break;
               case '\'': result += '\''; break;
               default: result += character; break;
               }

               escaped = false;
               continue;
          }

          if (character == '\\')
          {
               escaped = true;
               continue;
          }

          if (character == delimiter) return result;

          if (character == '\n' || character == '\r')
          {
               // TODO: error
               break;
          }

          result += character;
     }

     return result;
}

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
               if (directive == "pragma")
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

                    try
                    {

                         std::filesystem::path resolvedPath = ecpps::fs::GetSourceScanner().ResolveInclude(
                              fileName, header,
                              (delimiter == '"') ? ecpps::fs::IncludeType::Local : ecpps::fs::IncludeType::System);
                         if (std::ranges::find(includedFiles, resolvedPath) == includedFiles.end())
                         {
                              const auto& includedSource = ecpps::fs::GetSourceScanner().GetFileContents(resolvedPath);

                              tokens.append_range(Parse(includedSource, macros, resolvedPath.string(), includedFiles,
                                                        includeDirectories));
                         }
                    }
                    catch (const fs::FileNotFoundException& fileNotFound)
                    {
                         this->diagnostics.push_back(std::make_unique<diagnostics::SyntaxError>(
                              std::format("Unable to include file '{}'", fileNotFound.name), location));
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
                    while (sourceIterator != source.end() && (std::isspace(*sourceIterator) != 0)) ++sourceIterator;

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

                    bool wasConditionMet = isIfndef ? std::ranges::none_of(macros,
                                                                           [&macroName](const MacroReplacement& m)
                                                                           {
                                                                                return m.name == macroName;
                                                                           })
                                                    : std::ranges::any_of(macros,
                                                                          [&macroName](const MacroReplacement& m)
                                                                          {
                                                                               return m.name == macroName;
                                                                          });

                    std::vector<PreprocessingToken> branchTokens;
                    std::string builtSource{};
                    const auto previousLine = location.line;
                    bool wasAnyBranchTaken = wasConditionMet;
                    bool isCurrentBranch = wasConditionMet;

                    while (sourceIterator != source.end())
                    {
                         char c = *sourceIterator;
                         if (c == '#')
                         {
                              wasConditionMet = false;
                              auto peekIt = sourceIterator;
                              Advance(peekIt);
                              while (peekIt != source.end() && (std::isspace(*peekIt) != 0)) Advance(peekIt);

                              std::string nextDirective;
                              while (peekIt != source.end() && IsCharacterContinuation(*peekIt))
                              {
                                   nextDirective += *peekIt;
                                   Advance(peekIt);
                              }

                              bool isDirective =
                                   (nextDirective == "else" || nextDirective == "endif" || nextDirective == "elif" ||
                                    nextDirective == "elifdef" || nextDirective == "elifndef");
                              if (isDirective)
                              {

                                   if (nextDirective == "else")
                                   {
                                        wasConditionMet = true;
                                        sourceIterator = peekIt;
                                        Advance(sourceIterator);
                                   }
                                   else if (nextDirective == "endif")
                                   {
                                        sourceIterator = peekIt;
                                        break;
                                   }
                                   else if (nextDirective == "elif")
                                   {
                                        while (peekIt != source.end() && (std::isspace(*peekIt) != 0)) Advance(peekIt);

                                        std::string elifCondition;
                                        while (peekIt != source.end() && IsCharacterContinuation(*peekIt))
                                             elifCondition += *peekIt++;

                                        if (!elifCondition.empty())
                                        {
                                             auto it = std::ranges::find_if(macros,
                                                                            [&elifCondition](const MacroReplacement& m)
                                                                            {
                                                                                 return m.name == elifCondition;
                                                                            });
                                             wasConditionMet = (it != macros.end());
                                        }
                                        sourceIterator = peekIt;
                                        Advance(sourceIterator);
                                   }
                                   else if (nextDirective == "elifdef" || nextDirective == "elifndef")
                                   {
                                        wasConditionMet = false;
                                        bool isElifndef = (nextDirective == "elifndef");

                                        while (peekIt != source.end() && (std::isspace(*peekIt) != 0)) Advance(peekIt);

                                        std::string elifMacroName;
                                        while (peekIt != source.end() && IsCharacterContinuation(*peekIt))
                                             elifMacroName += *peekIt++;

                                        if (!elifMacroName.empty())
                                        {
                                             auto it = std::ranges::find_if(macros,
                                                                            [&elifMacroName](const MacroReplacement& m)
                                                                            {
                                                                                 return m.name == elifMacroName;
                                                                            });
                                             wasConditionMet = isElifndef ? (it == macros.end()) : (it != macros.end());
                                        }
                                        sourceIterator = peekIt;
                                        Advance(sourceIterator);
                                   }

                                   if (wasAnyBranchTaken) isCurrentBranch = false;
                                   else if (wasConditionMet)
                                   {
                                        isCurrentBranch = true;
                                        wasAnyBranchTaken = true;
                                   }
                                   continue;
                              }
                         }

                         if (isCurrentBranch) builtSource += c;
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

                    auto it = std::ranges::find_if(macros,
                                                   [&macroName](const MacroReplacement& m)
                                                   {
                                                        return m.name == macroName;
                                                   });
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
               static const std::unordered_set<std::string> stringPrefixes = {"u8",  "u",  "U",  "L", "R",
                                                                              "u8R", "uR", "UR", "LR"};

               auto peekNext = std::next(sourceIterator);
               bool isStringPrefix = stringPrefixes.contains(identifier) && peekNext != source.end() &&
                                     (*peekNext == '"' || *peekNext == '\'');

               if (isStringPrefix)
               {
                    bool isRaw = identifier.back() == 'R';
                    ++sourceIterator;
                    char quoteChar = *sourceIterator;
                    bool isChar = (quoteChar == '\'');

                    if (isRaw)
                    {
                         ++sourceIterator;
                         std::string rawDelimiter;
                         while (sourceIterator != source.end() && *sourceIterator != '(')
                              rawDelimiter += *sourceIterator++;

                         if (sourceIterator == source.end()) break;
                         ++sourceIterator; // skip '('

                         std::string rawContent;
                         while (sourceIterator != source.end())
                         {
                              if (*sourceIterator == ')')
                              {
                                   auto checkIt = std::next(sourceIterator);
                                   bool matches = true;
                                   auto tempIt = checkIt;
                                   for (char dc : rawDelimiter)
                                   {
                                        if (tempIt == source.end() || *tempIt != dc)
                                        {
                                             matches = false;
                                             break;
                                        }
                                        ++tempIt;
                                   }
                                   if (matches && tempIt != source.end() && *tempIt == '"')
                                   {
                                        sourceIterator = tempIt; // leave sourceIterator on closing "
                                        break;
                                   }
                              }
                              rawContent += *sourceIterator++;
                         }

                         location.endPosition = location.position;
                         tokens.emplace_back(PreprocessingTokenType::StringLiteral, rawContent, location);
                    }
                    else
                    {
                         std::string literal = ReadEscapedLiteral(sourceIterator, source.end(), quoteChar);
                         location.endPosition = location.position;
                         tokens.emplace_back(isChar ? PreprocessingTokenType::CharacterLiteral
                                                    : PreprocessingTokenType::StringLiteral,
                                             literal, location);
                    }
               }
               else
               {

                    location.endPosition = location.position;
                    auto it = std::ranges::find_if(macros,
                                                   [&identifier](const MacroReplacement& m)
                                                   {
                                                        return m.name == identifier;
                                                   });

                    if (it != macros.end())
                    {
                         if (it->Type() == ecpps::MacroReplacementType::FunctionLike)
                         {
                              auto peekIt = std::next(sourceIterator);
                              while (peekIt != source.end() && (*peekIt == ' ' || *peekIt == '\t')) ++peekIt;

                              if (it->parameters && peekIt != source.end() && *peekIt == '(')
                              {
                                   ++peekIt; // skip '('
                                   std::size_t parenLevel = 1;
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

                                   std::vector<std::vector<PreprocessingToken>> arguments;
                                   arguments.reserve(rawArgs.size());
                                   for (const auto& rawArg : rawArgs)
                                   {
                                        std::vector<MacroReplacement> macrosCopy = macros;
                                        arguments.push_back(Preprocessor::Parse(rawArg, macrosCopy, ""));
                                   }

                                   auto expandedTokens = it->ProcessFunctionLike(arguments, rawArgs, location, macros);
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
               location.position += identifier.size() - 1;
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

               std::string literal = ReadEscapedLiteral(sourceIterator, source.end(), delimiter);

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
          else
          {
               this->diagnostics.push_back(std::make_unique<diagnostics::SyntaxError>(
                    std::format("Invalid character '{:x}'",
                                static_cast<std::uint32_t>(static_cast<unsigned char>(character))),
                    location));
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
}

bool ecpps::Preprocessor::IsOperatorOrPunctuatorBeginning(char ch)
{
     static std::unordered_set<char> OperatorCharacters = {'{', '}', '[', ']', '(', ')', '.', '-', '~', '!', ';', ':',
                                                           '+', '?', '*', '/', '%', '^', '&', '|', '<', '>', '=', ','};

     return OperatorCharacters.contains(ch);
}

static std::string StringifyArg(const std::string& raw)
{
     std::string collapsed;
     bool lastWasSpace = true;
     for (const auto c : raw)
     {
          if (std::isspace(static_cast<unsigned char>(c)))
          {
               if (!lastWasSpace) collapsed += ' ';
               lastWasSpace = true;
          }
          else
          {
               collapsed += static_cast<char>(c);
               lastWasSpace = false;
          }
     }
     if (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();

     std::string escaped;
     escaped.reserve(collapsed.size());
     for (char character : collapsed)
     {
          if (character == '\\' || character == '"') escaped += '\\';
          escaped += character;
     }
     return escaped;
}

static std::string ExpandMacroString(const std::string& contents,
                                     const std::unordered_map<std::string, std::string>& parameterMap,
                                     const std::unordered_map<std::string, std::string>& rawParameterMap)
{
     std::string result{};
     result.reserve(contents.size());

     for (std::size_t i = 0; i < contents.size(); i++)
     {
          if (i + 1 < contents.size() && contents[i] == '#' && contents[i + 1] == '#')
          {
               while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back()))) result.pop_back();

               i += 2;
               while (i < contents.size() && std::isspace(static_cast<unsigned char>(contents[i]))) ++i;

               std::string rhs;
               while (i < contents.size() &&
                      (std::isalnum(static_cast<unsigned char>(contents[i])) || contents[i] == '_'))
                    rhs += contents[i++];

               auto it = rawParameterMap.find(rhs);
               result += (it != rawParameterMap.end()) ? it->second : rhs;
               i--;
               continue;
          }

          if (contents[i] == '#' && i + 1 < contents.size())
          {
               std::size_t j = i + 1;
               while (j < contents.size() && std::isspace(static_cast<unsigned char>(contents[j]))) j++;
               std::string param;
               while (j < contents.size() &&
                      (std::isalnum(static_cast<unsigned char>(contents[j])) || contents[j] == '_'))
                    param += contents[j++];
               auto it = rawParameterMap.find(param);
               if (it != rawParameterMap.end())
               {
                    result += '"';
                    result += StringifyArg(it->second);
                    result += '"';
                    i = j - 1;
                    continue;
               }
               result += contents[i];
               continue;
          }

          if (std::isalpha(static_cast<unsigned char>(contents[i])) || contents[i] == '_')
          {
               std::string token;
               std::size_t j = i;
               while (j < contents.size() &&
                      (std::isalnum(static_cast<unsigned char>(contents[j])) || contents[j] == '_'))
                    token += contents[j++];

               auto it = parameterMap.find(token);
               result += (it != parameterMap.end()) ? it->second : token;
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
     auto tokens = ecpps::Preprocessor{}.Parse(expanded, macrosCopy, "", includedFiles);
     for (std::size_t i = 0; i < tokens.size(); i++)
     {
          tokens[i].source.line = location.line;
          tokens[i].source.position = location.position + i;
          tokens[i].source.endPosition = location.position + i;
     }
     return tokens;
}

std::vector<ecpps::PreprocessingToken> ecpps::MacroReplacement::ProcessObjectLike(
     const Location& location, const std::vector<ecpps::MacroReplacement>& macros) const
{
     return TokeniseExpandedMacro(ExpandMacroString(contents, {}, {}), location, macros);
}

std::vector<ecpps::PreprocessingToken> ecpps::MacroReplacement::ProcessFunctionLike(
     const std::vector<std::vector<PreprocessingToken>>& arguments, const std::vector<std::string>& rawArgs,
     const Location& location, const std::vector<ecpps::MacroReplacement>& macros) const
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
               if (first != std::string::npos) parameterName = parameterName.substr(first, last - first + 1);
               else
                    parameterName.clear();

               if (i < rawArgs.size())
               {
                    // std::string argStr;
                    // for (const auto& token : arguments[i]) argStr += token.value;

                    // rawParameterMap[parameterName] = argStr;

                    // std::vector<ecpps::MacroReplacement> macrosCopy = macros;
                    // std::set<std::filesystem::path> includedFiles;
                    // auto expandedTokens = ecpps::Preprocessor::Parse(argStr, macrosCopy, "", includedFiles);
                    // std::string expandedArg;
                    // for (const auto& tok : expandedTokens) expandedArg += tok.value;

                    // parameterMap[parameterName] = expandedArg;
                    const std::string& raw = rawArgs[i];
                    auto rf = raw.find_first_not_of(" \t\n\r");
                    auto rl = raw.find_last_not_of(" \t\n\r");
                    rawParameterMap[parameterName] =
                         (rf != std::string::npos) ? raw.substr(rf, rl - rf + 1) : std::string{};
               }
               else
                    rawParameterMap[parameterName] = {};

               if (i < arguments.size())
               {
                    std::vector<MacroReplacement> macrosCopy = macros;
                    auto expanded = Preprocessor{}.Parse(rawParameterMap[parameterName], macrosCopy, "");

                    std::string joined;
                    for (std::size_t t = 0; t < expanded.size(); ++t)
                    {
                         if (t > 0) joined += ' ';
                         joined += expanded[t].value;
                    }
                    parameterMap[parameterName] = std::move(joined);
               }
               else
                    parameterMap[parameterName] = {};
          }
          if (isVariadic)
          {
               std::string vargs;
               for (std::size_t i = parameters->size(); i < rawArgs.size(); ++i)
               {
                    if (i > parameters->size()) vargs += ',';
                    vargs += rawArgs[i];
               }
               parameterMap["__VA_ARGS__"] = vargs;
               rawParameterMap["__VA_ARGS__"] = vargs;
          }
     }

     auto expandedString = ExpandMacroString(contents, parameterMap, rawParameterMap);
     return TokeniseExpandedMacro(expandedString, location, macros);
}
