#include "Error.h"
#include <fstream>
#include <print>
#include <string>

std::string ecpps::diagnostics::InternalCompilerError::Message(void) const noexcept
{
     std::string built = this->_message;
     for (const auto& function : this->_trace) built += "\n" + function;
     return built;
}

std::string ecpps::diagnostics::TypeError::Message(void) const noexcept { return this->_message; }

std::string ecpps::diagnostics::UnresolvedSymbolError::Message(void) const noexcept
{
     return "Unresolved symbol " + this->_symbol + ": " + this->_message;
}

std::string ecpps::diagnostics::SyntaxError::Message(void) const noexcept { return this->_message; }

void ecpps::diagnostics::PrintDiagnostic(const std::string& fileName, const DiagnosticsMessage& diagnostic, int indent)
{
     std::string colour{};

     switch (diagnostic->Level())
     {
     case DiagnosticsLevel::Error: colour = "\x1b[31m"; break;
     case DiagnosticsLevel::Warning: colour = "\x1b[33m"; break;
     case DiagnosticsLevel::Information: colour = "\x1b[36m"; break;
     }
     std::print("{}", colour);

     std::string location{};
     if (!fileName.empty() || diagnostic->Source().line != 0)
          location = fileName + ":" + std::to_string(diagnostic->Source().line) + ":" +
                     std::to_string(diagnostic->Source().position) + " ";
     std::println("{}{}[{}]\x1b[0m {} : {}", std::string(indent * 5, ' '), location, ToString(diagnostic->Level()),
                  diagnostic->Name(), diagnostic->Message());

     if (!fileName.empty() && diagnostic->Source().line > 0)
     {
          std::ifstream file(fileName);
          if (file)
          {
               std::string line;
               int currentLine = 0;
               while (std::getline(file, line))
               {
                    ++currentLine;
                    if (currentLine == diagnostic->Source().line)
                    {
                         std::string expandedLine;
                         std::vector<int> positionMap(line.size() + 1, 0);

                         int pos = 0;
                         for (size_t i = 0; i < line.size(); ++i)
                         {
                              positionMap[i] = pos;
                              if (line[i] == '\t')
                              {
                                   expandedLine.append(5, ' ');
                                   pos += 5;
                              }
                              else
                              {
                                   expandedLine += line[i];
                                   ++pos;
                              }
                         }
                         positionMap[line.size()] = pos; // For end position handling

                         int startPos = positionMap.size() > (diagnostic->Source().position)
                                            ? positionMap.at(diagnostic->Source().position)
                                            : diagnostic->Source().position;
                         int endPos = positionMap.size() > (diagnostic->Source().endPosition)
                                          ? positionMap.at(diagnostic->Source().endPosition)
                                          : diagnostic->Source().endPosition;

                         std::println("{} {}", std::string(indent * 5, ' '), expandedLine);
                         std::println("{}   {}{}{}\x1b[0m", std::string(indent * 5, ' '), "   ",
                                      std::string(startPos, ' '), colour, std::string(endPos - startPos + 2, '^'));
                         break;
                    }
               }
          }
     }

     for (const auto& subDiagnostic : diagnostic->SubDiagnostics())
          PrintDiagnostic(fileName, subDiagnostic, indent + 1);
}
