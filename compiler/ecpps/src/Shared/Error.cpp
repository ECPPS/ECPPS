#include "Error.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
     constexpr std::string_view ResetColour = "\x1b[0m";

     [[nodiscard]] std::string_view ColourFor(const ecpps::diagnostics::DiagnosticsLevel level) noexcept
     {
          using ecpps::diagnostics::DiagnosticsLevel;
          switch (level)
          {
          case DiagnosticsLevel::Error: return "\x1b[31m";
          case DiagnosticsLevel::Warning: return "\x1b[33m";
          case DiagnosticsLevel::Information: return "\x1b[36m";
          }
          return {};
     }

     [[nodiscard]] std::string_view LabelFor(const ecpps::diagnostics::DiagnosticsLevel level) noexcept
     {
          using ecpps::diagnostics::DiagnosticsLevel;
          switch (level)
          {
          case DiagnosticsLevel::Error: return "error";
          case DiagnosticsLevel::Warning: return "warning";
          case DiagnosticsLevel::Information: return "note";
          }
          return "diagnostic";
     }

     [[nodiscard]] bool IsUtf8Locale(void)
     {
          const char* locale = std::getenv("LC_ALL");
          if (locale == nullptr || *locale == '\0') locale = std::getenv("LC_CTYPE");
          if (locale == nullptr || *locale == '\0') locale = std::getenv("LANG");
          if (locale == nullptr) return false;

          std::string normalised;
          for (const auto character : std::string_view{locale})
               normalised.push_back(character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                                         : static_cast<char>(character));

          return normalised.find("utf-8") != std::string::npos || normalised.find("utf8") != std::string::npos;
     }

     [[nodiscard]] bool InRange(const std::uint32_t value, const std::uint32_t first, const std::uint32_t last) noexcept
     {
          return first <= value && value <= last;
     }

     struct DecodedCharacter
     {
          std::uint32_t value{};
          std::size_t byteCount{1};
     };

     [[nodiscard]] DecodedCharacter DecodeUtf8(const std::string_view text, const std::size_t offset) noexcept
     {
          const auto first = static_cast<unsigned char>(text[offset]);
          if (first < 0x80) return {.value = first, .byteCount = 1};

          std::size_t byteCount{};
          std::uint32_t value{};
          if ((first & 0xe0) == 0xc0)
          {
               byteCount = 2;
               value = first & 0x1f;
          }
          else if ((first & 0xf0) == 0xe0)
          {
               byteCount = 3;
               value = first & 0x0f;
          }
          else if ((first & 0xf8) == 0xf0)
          {
               byteCount = 4;
               value = first & 0x07;
          }
          else
               return {.value = 0xfffd, .byteCount = 1};

          if (offset + byteCount > text.size()) return {.value = 0xfffd, .byteCount = 1};
          for (std::size_t index = 1; index < byteCount; index++)
          {
               const auto continuation = static_cast<unsigned char>(text[offset + index]);
               if ((continuation & 0xc0) != 0x80) return {0xfffd, 1};
               value = (value << 6) | (continuation & 0x3f);
          }

          if ((byteCount == 2 && value < 0x80) || (byteCount == 3 && value < 0x800) ||
              (byteCount == 4 && value < 0x10000) || value > 0x10ffff || InRange(value, 0xd800, 0xdfff))
               return {.value = 0xfffd, .byteCount = 1};

          return {.value = value, .byteCount = byteCount};
     }

     [[nodiscard]] bool IsCombiningCharacter(const std::uint32_t value) noexcept
     {
          return InRange(value, 0x0300, 0x036f) || InRange(value, 0x0483, 0x0489) || InRange(value, 0x0591, 0x05bd) ||
                 value == 0x05bf || InRange(value, 0x05c1, 0x05c2) || InRange(value, 0x0610, 0x061a) ||
                 InRange(value, 0x064b, 0x065f) || value == 0x0670 || InRange(value, 0x06d6, 0x06ed) ||
                 InRange(value, 0x1ab0, 0x1aff) || InRange(value, 0x1dc0, 0x1dff) || value == 0x200d ||
                 InRange(value, 0x20d0, 0x20ff) || InRange(value, 0xfe00, 0xfe0f) || InRange(value, 0xfe20, 0xfe2f) ||
                 InRange(value, 0x1f3fb, 0x1f3ff) || InRange(value, 0xe0100, 0xe01ef);
     }

     [[nodiscard]] bool IsWideCharacter(const std::uint32_t value) noexcept
     {
          return InRange(value, 0x1100, 0x115f) || value == 0x2329 || value == 0x232a ||
                 (InRange(value, 0x2e80, 0xa4cf) && value != 0x303f) || InRange(value, 0xac00, 0xd7a3) ||
                 InRange(value, 0xf900, 0xfaff) || InRange(value, 0xfe10, 0xfe19) || InRange(value, 0xfe30, 0xfe6f) ||
                 InRange(value, 0xff00, 0xff60) || InRange(value, 0xffe0, 0xffe6) || InRange(value, 0x1f300, 0x1faff) ||
                 InRange(value, 0x20000, 0x3fffd);
     }

     [[nodiscard]] std::size_t DisplayWidth(const std::uint32_t value) noexcept
     {
          if (IsCombiningCharacter(value) || value < 0x20 || InRange(value, 0x7f, 0x9f)) return 0;
          return IsWideCharacter(value) ? 2 : 1;
     }

     struct ExpandedSourceLine
     {
          std::string text;
          std::vector<std::size_t> displayColumnAtByte{};

          [[nodiscard]] std::size_t DisplayColumn(const std::size_t byteOffset) const noexcept
          {
               return this->displayColumnAtByte[std::min(byteOffset, this->displayColumnAtByte.size() - 1)];
          }
     };

     [[nodiscard]] ExpandedSourceLine ExpandSourceLine(const std::string_view line)
     {
          ExpandedSourceLine expanded{};
          expanded.text.reserve(line.size());
          expanded.displayColumnAtByte.resize(line.size() + 1);

          std::size_t displayColumn = 0;
          for (std::size_t byteOffset = 0; byteOffset < line.size();)
          {
               expanded.displayColumnAtByte[byteOffset] = displayColumn;
               if (line[byteOffset] == '\t')
               {
                    const auto tabWidth = 8 - (displayColumn % 8);
                    expanded.text.append(tabWidth, ' ');
                    displayColumn += tabWidth;
                    byteOffset++;
                    expanded.displayColumnAtByte[byteOffset] = displayColumn;
                    continue;
               }

               const auto character = DecodeUtf8(line, byteOffset);
               for (std::size_t index = 1; index < character.byteCount; index++)
                    expanded.displayColumnAtByte[byteOffset + index] = displayColumn;

               expanded.text.append(line.substr(byteOffset, character.byteCount));
               displayColumn += DisplayWidth(character.value);
               byteOffset += character.byteCount;
               expanded.displayColumnAtByte[byteOffset] = displayColumn;
          }

          return expanded;
     }

     class SourceLineCache
     {
     public:
          explicit SourceLineCache(const std::string_view fileName)
          {
               if (!fileName.empty()) this->_file.open(std::string{fileName});
          }

          [[nodiscard]] const std::string* Get(const std::size_t line)
          {
               if (line == 0 || !this->_file.is_open()) return nullptr;

               while (this->_lines.size() < line && !this->_finished)
               {
                    std::string sourceLine;
                    if (!std::getline(this->_file, sourceLine))
                    {
                         this->_finished = true;
                         break;
                    }
                    if (!sourceLine.empty() && sourceLine.back() == '\r') sourceLine.pop_back();
                    this->_lines.push_back(std::move(sourceLine));
               }

               return line <= this->_lines.size() ? &this->_lines[line - 1] : nullptr;
          }

     private:
          std::ifstream _file{};
          std::vector<std::string> _lines{};
          bool _finished{};
     };

     class RendererContext
     {
     public:
          explicit RendererContext(const std::string_view fileName)
              : _fileName(fileName), _sourceLines(fileName), _useUnicode(IsUtf8Locale())
          {
          }

          [[nodiscard]] const std::string& FileName(void) const noexcept
          {
               return this->_fileName;
          }

          [[nodiscard]] bool UseUnicode(void) const noexcept
          {
               return this->_useUnicode;
          }

          [[nodiscard]] const ExpandedSourceLine* GetExpandedLine(const std::size_t line)
          {
               if (const auto found = this->_expandedLines.find(line); found != this->_expandedLines.end())
                    return &found->second;

               const auto* sourceLine = this->_sourceLines.Get(line);
               if (sourceLine == nullptr) return nullptr;

               const auto [insertedLine, inserted] =
                    this->_expandedLines.try_emplace(line, ExpandSourceLine(*sourceLine));
               static_cast<void>(inserted);
               return &insertedLine->second;
          }

          [[nodiscard]] bool HasPrintedLine(const std::size_t line) const
          {
               return this->_printedLines.contains(line);
          }

          [[nodiscard]] bool IsRepeatedSourceLine(const std::size_t line) const
          {
               if (const auto found = this->_sourceLineCounts.find(line); found != this->_sourceLineCounts.end())
                    return found->second > 1;
               return false;
          }

          void SuppressCaretLine(const std::size_t line)
          {
               this->_caretSuppressedLine = line;
          }

          [[nodiscard]] bool IsCaretSuppressed(const std::size_t line) const
          {
               return line != 0 && line == this->_caretSuppressedLine;
          }

          void CountSourceLine(const std::size_t line)
          {
               if (line != 0) this->_sourceLineCounts[line]++;
          }

          void MarkLinePrinted(const std::size_t line)
          {
               this->_printedLines.insert(line);
          }

     private:
          std::string _fileName;
          SourceLineCache _sourceLines;
          std::unordered_map<std::size_t, ExpandedSourceLine> _expandedLines{};
          std::unordered_map<std::size_t, std::size_t> _sourceLineCounts{};
          std::unordered_set<std::size_t> _printedLines{};
          std::size_t _caretSuppressedLine{};
          bool _useUnicode{};
     };

     using TreePath = std::vector<bool>;

     [[nodiscard]] std::string_view RailGlyph(const bool useUnicode) noexcept
     {
          return useUnicode ? "┃" : "|";
     }

     void PrintFramePrefix(const TreePath& path, const std::size_t depth, const bool useUnicode)
     {
          const auto rail = RailGlyph(useUnicode);
          for (std::size_t index = 0; index < depth; index++)
          {
               const auto continues = index < path.size() && path[index];
               if (continues) std::print(stderr, "{}    ", rail);
               else
                    std::print(stderr, "     ");
          }

          std::print(stderr, "{} ", rail);
     }

     void PrintBranchPrefix(const TreePath& path, const std::size_t depth, const bool isLastSibling,
                            const bool useUnicode)
     {
          const auto rail = RailGlyph(useUnicode);
          for (std::size_t index = 0; index + 1 < depth; index++)
          {
               const auto continues = index < path.size() && path[index];
               if (continues) std::print(stderr, "{}    ", rail);
               else
                    std::print(stderr, "     ");
          }

          if (useUnicode) std::print(stderr, "{}━━━━┓ ", isLastSibling ? "┗" : "┣");
          else
               std::print(stderr, "{}--- ", isLastSibling ? "+" : "|");
     }

     [[nodiscard]] std::size_t ColumnForPosition(const ExpandedSourceLine& line, const std::size_t position) noexcept
     {
          if (position == 0) return 0;
          return line.DisplayColumn(position - 1) + 1;
     }

     [[nodiscard]] std::string FormatLocation(const std::string_view fileName, const ecpps::Location& source,
                                              const ExpandedSourceLine* expandedLine)
     {
          std::string location{fileName};
          if (source.line == 0) return location;

          if (!location.empty()) location.push_back(':');
          location += std::to_string(source.line);
          if (source.position != 0)
          {
               location.push_back(':');
               location += std::to_string(expandedLine == nullptr ? source.position
                                                                  : ColumnForPosition(*expandedLine, source.position));
          }
          return location;
     }

     struct CaretRange
     {
          std::size_t startColumn{};
          std::size_t width{};
     };

     [[nodiscard]] std::optional<CaretRange> MakeCaretRange(const ExpandedSourceLine& line,
                                                            const ecpps::Location& source) noexcept
     {
          if (source.position == 0 || source.position - 1 > line.displayColumnAtByte.size() - 1) return std::nullopt;

          const auto startByte = source.position - 1;
          const auto startColumn = line.DisplayColumn(startByte);
          if (source.endPosition < source.position) return CaretRange{startColumn, 1};

          const auto endColumn = line.DisplayColumn(source.endPosition);
          return CaretRange{startColumn, std::max<std::size_t>(1, endColumn - startColumn)};
     }

     [[nodiscard]] std::string_view TrimHorizontalWhitespace(const std::string_view line) noexcept
     {
          constexpr std::string_view whitespace{" \t\r"};
          const auto first = line.find_first_not_of(whitespace);
          if (first == std::string_view::npos) return {};
          return line.substr(first, line.find_last_not_of(whitespace) - first + 1);
     }

     void PrintMessage(const std::string_view message, const std::size_t depth, const TreePath& path,
                       const bool useUnicode)
     {
          std::size_t start = 0;
          bool firstLine = true;
          while (start <= message.size())
          {
               const auto end = message.find('\n', start);
               const auto line =
                    message.substr(start, end == std::string_view::npos ? message.size() - start : end - start);
               if (!firstLine)
               {
                    PrintFramePrefix(path, depth, useUnicode);
               }
               std::print(stderr, "{}", TrimHorizontalWhitespace(line));

               if (end == std::string_view::npos) break;
               std::print(stderr, "\n");
               start = end + 1;
               firstLine = false;
          }
          std::print(stderr, "\n");
     }

     void PrintSourceFrame(const ExpandedSourceLine& line, const ecpps::Location& source,
                           const ecpps::diagnostics::DiagnosticsLevel level, const std::size_t depth,
                           const TreePath& path, RendererContext& context)
     {
          const auto lineNumber = std::to_string(source.line);
          const auto alreadyPrinted = context.HasPrintedLine(source.line);
          if (!alreadyPrinted)
          {
               PrintFramePrefix(path, depth, context.UseUnicode());
               std::print(stderr, "{}| {}\n", lineNumber, line.text);
               context.MarkLinePrinted(source.line);
          }

          if (alreadyPrinted || context.IsRepeatedSourceLine(source.line) || context.IsCaretSuppressed(source.line))
               return;

          const auto range = MakeCaretRange(line, source);
          if (!range) return;

          PrintFramePrefix(path, depth, context.UseUnicode());
          std::print(stderr, "{}| {}{}{}{}\n", std::string(lineNumber.size(), ' '),
                     std::string(range->startColumn, ' '), ColourFor(level), std::string(range->width, '^'),
                     ResetColour);
     }

     void CountSourceLines(const ecpps::diagnostics::DiagnosticsMessage& diagnostic, RendererContext& context)
     {
          context.CountSourceLine(diagnostic->Source().line);
          for (const auto& subDiagnostic : diagnostic->SubDiagnostics()) CountSourceLines(subDiagnostic, context);
     }

     [[nodiscard]] bool ShareSourceLine(const std::vector<ecpps::diagnostics::DiagnosticsMessage>& diagnostics)
     {
          if (diagnostics.size() < 2) return false;
          const auto line = diagnostics.front()->Source().line;
          if (line == 0) return false;
          return std::ranges::all_of(diagnostics,
                                     [line](const auto& diagnostic)
                                     {
                                          return diagnostic->Source().line == line;
                                     });
     }

     enum struct PrefixKind : std::uint8_t
     {
          Root,
          Branch,
          Continuation
     };

     void PrintDiagnosticNode(const ecpps::diagnostics::DiagnosticsMessage& diagnostic, const std::size_t depth,
                              const TreePath& path, const PrefixKind prefixKind, const bool isLastSibling,
                              RendererContext& context)
     {
          const auto level = diagnostic->Level();
          const auto source = diagnostic->Source();
          const auto* expandedLine = source.line == 0 ? nullptr : context.GetExpandedLine(source.line);
          const auto location = FormatLocation(context.FileName(), source, expandedLine);
          const auto colour = ColourFor(level);

          if (prefixKind == PrefixKind::Root) std::print(stderr, "{} ", RailGlyph(context.UseUnicode()));
          else if (prefixKind == PrefixKind::Branch)
               PrintBranchPrefix(path, depth, isLastSibling, context.UseUnicode());
          else
               PrintFramePrefix(path, depth, context.UseUnicode());

          if (location.empty()) std::print(stderr, "{}{}: {}", colour, LabelFor(level), ResetColour);
          else
               std::print(stderr, "{}{} {}: {}", colour, location, LabelFor(level), ResetColour);

          const auto message = diagnostic->Message();
          PrintMessage(message, depth, path, context.UseUnicode());

          if (expandedLine != nullptr) PrintSourceFrame(*expandedLine, source, level, depth, path, context);

          const auto& subDiagnostics = diagnostic->SubDiagnostics();
          if (ShareSourceLine(subDiagnostics))
          {
               for (std::size_t index = 0; index < subDiagnostics.size(); index++)
               {
                    auto subPath = path;
                    if (index == 0)
                    {
                         subPath.push_back(true);
                         PrintDiagnosticNode(subDiagnostics[index], depth + 1, subPath, PrefixKind::Branch, true,
                                             context);
                    }
                    else
                    {
                         subPath.push_back(false);
                         PrintDiagnosticNode(subDiagnostics[index], depth + 1, subPath, PrefixKind::Continuation,
                                             index + 1 == subDiagnostics.size(), context);
                    }
               }
               return;
          }

          for (std::size_t index = 0; index < subDiagnostics.size(); index++)
          {
               if (prefixKind == PrefixKind::Root && subDiagnostics.size() > 1 && index + 1 == subDiagnostics.size())
                    std::print(stderr, "{}\n", RailGlyph(context.UseUnicode()));

               auto subPath = path;
               subPath.push_back(index + 1 < subDiagnostics.size());
               PrintDiagnosticNode(subDiagnostics[index], depth + 1, subPath, PrefixKind::Branch,
                                   index + 1 == subDiagnostics.size(), context);
          }
     }
} // namespace

ecpps::diagnostics::IDiagnosticsBase::~IDiagnosticsBase(void) = default;

std::string ecpps::diagnostics::InternalCompilerError::Message(void) const noexcept
{
     std::string built = this->_message;
     for (const auto& function : this->_trace) built += "\n" + function;
     return built;
}

std::string ecpps::diagnostics::TypeError::Message(void) const noexcept
{
     return this->_message;
}
std::string ecpps::diagnostics::ConstantEvaluationError::Message(void) const noexcept
{
     return this->_message;
}
std::string ecpps::diagnostics::ConstantEvaluationWarning::Message(void) const noexcept
{
     return this->_message;
}

std::string ecpps::diagnostics::UnresolvedSymbolError::Message(void) const noexcept
{
     return "Unresolved symbol " + this->_symbol + ": " + this->_message;
}

std::string ecpps::diagnostics::SyntaxError::Message(void) const noexcept
{
     return this->_message;
}

void ecpps::diagnostics::PrintDiagnostic(const std::string& fileName, const DiagnosticsMessage& diagnostic,
                                         const std::size_t indent, const std::size_t lastPrintedLine)
{
     RendererContext context{fileName};
     if (!diagnostic->SubDiagnostics().empty()) context.SuppressCaretLine(diagnostic->Source().line);
     CountSourceLines(diagnostic, context);
     if (lastPrintedLine != 0) context.MarkLinePrinted(lastPrintedLine);

     const TreePath path(indent, false);
     PrintDiagnosticNode(diagnostic, indent, path, indent == 0 ? PrefixKind::Root : PrefixKind::Branch, true, context);
}
