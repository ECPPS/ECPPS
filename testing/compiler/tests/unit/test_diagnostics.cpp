#define CATCH_CONFIG_MAIN
#include <Shared/Error.h>

#include <catch_amalgamated.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
     [[nodiscard]] int FileDescriptor(std::FILE* file)
     {
#ifdef _WIN32
          return _fileno(file);
#else
          return ::fileno(file);
#endif
     }

     [[nodiscard]] int DuplicateDescriptor(const int descriptor)
     {
#ifdef _WIN32
          return _dup(descriptor);
#else
          return ::dup(descriptor);
#endif
     }

     [[nodiscard]] int DuplicateDescriptorTo(const int source, const int destination)
     {
#ifdef _WIN32
          return _dup2(source, destination);
#else
          return ::dup2(source, destination);
#endif
     }

     void CloseDescriptor(const int descriptor)
     {
#ifdef _WIN32
          _close(descriptor);
#else
          ::close(descriptor);
#endif
     }

     class StderrCapture
     {
     public:
          StderrCapture(void)
          {
               this->_capture = std::tmpfile();
               if (this->_capture == nullptr) throw std::runtime_error{"Could not create a temporary output file"};

               std::fflush(stderr);
               this->_stderrDescriptor = FileDescriptor(stderr);
               this->_savedDescriptor = DuplicateDescriptor(this->_stderrDescriptor);
               if (this->_savedDescriptor < 0)
               {
                    std::fclose(this->_capture);
                    this->_capture = nullptr;
                    throw std::runtime_error{"Could not duplicate stderr"};
               }

               if (DuplicateDescriptorTo(FileDescriptor(this->_capture), this->_stderrDescriptor) < 0)
               {
                    CloseDescriptor(this->_savedDescriptor);
                    this->_savedDescriptor = -1;
                    std::fclose(this->_capture);
                    this->_capture = nullptr;
                    throw std::runtime_error{"Could not capture stderr"};
               }
          }

          StderrCapture(const StderrCapture&) = delete;
          StderrCapture& operator=(const StderrCapture&) = delete;

          ~StderrCapture(void)
          {
               this->Restore();
               if (this->_capture != nullptr) std::fclose(this->_capture);
          }

          [[nodiscard]] std::string Read(void)
          {
               std::fflush(stderr);
               if (std::fseek(this->_capture, 0, SEEK_END) != 0)
                    throw std::runtime_error{"Could not seek captured stderr"};

               const auto size = std::ftell(this->_capture);
               if (size < 0 || std::fseek(this->_capture, 0, SEEK_SET) != 0)
                    throw std::runtime_error{"Could not read captured stderr"};

               std::string output(static_cast<std::size_t>(size), '\0');
               if (!output.empty() && std::fread(output.data(), 1, output.size(), this->_capture) != output.size())
                    throw std::runtime_error{"Could not read captured stderr"};
               return output;
          }

     private:
          void Restore(void) noexcept
          {
               if (this->_savedDescriptor < 0) return;

               std::fflush(stderr);
               static_cast<void>(DuplicateDescriptorTo(this->_savedDescriptor, this->_stderrDescriptor));
               CloseDescriptor(this->_savedDescriptor);
               this->_savedDescriptor = -1;
          }

          std::FILE* _capture{};
          int _stderrDescriptor{-1};
          int _savedDescriptor{-1};
     };

     class TemporarySourceFile
     {
     public:
          explicit TemporarySourceFile(const std::string_view contents)
          {
               static std::atomic_uint64_t sequence{};
               const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
               this->_path =
                    std::filesystem::temp_directory_path() / ("ecpps-diagnostics-" + std::to_string(timestamp) + "-" +
                                                              std::to_string(sequence.fetch_add(1)) + ".cpp");

               std::ofstream file{this->_path, std::ios::binary};
               if (!file.is_open()) throw std::runtime_error{"Could not create a temporary source file"};
               file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
               if (!file) throw std::runtime_error{"Could not write a temporary source file"};
          }

          TemporarySourceFile(const TemporarySourceFile&) = delete;
          TemporarySourceFile& operator=(const TemporarySourceFile&) = delete;

          ~TemporarySourceFile(void)
          {
               std::error_code error;
               std::filesystem::remove(this->_path, error);
          }

          [[nodiscard]] const std::filesystem::path& Path(void) const noexcept
          {
               return this->_path;
          }

     private:
          std::filesystem::path _path{};
     };

     class ScopedEnvironmentVariable
     {
     public:
          ScopedEnvironmentVariable(std::string name, std::string value) : _name(std::move(name))
          {
               if (const char* existing = std::getenv(this->_name.c_str()); existing != nullptr)
                    this->_previous = existing;
               this->Set(value.c_str());
          }

          ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
          ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

          ~ScopedEnvironmentVariable(void)
          {
               this->Set(this->_previous ? this->_previous->c_str() : nullptr);
          }

     private:
          void Set(const char* value) const noexcept
          {
#ifdef _WIN32
               _putenv_s(this->_name.c_str(), value == nullptr ? "" : value);
#else
               if (value == nullptr) unsetenv(this->_name.c_str());
               else
                    setenv(this->_name.c_str(), value, 1);
#endif
          }

          std::string _name;
          std::optional<std::string> _previous{};
     };

     [[nodiscard]] std::string Render(const std::filesystem::path& path,
                                      const ecpps::diagnostics::DiagnosticsMessage& diagnostic)
     {
          StderrCapture capture;
          ecpps::diagnostics::PrintDiagnostic(path.string(), diagnostic);
          return capture.Read();
     }

     [[nodiscard]] std::size_t CountOccurrences(const std::string_view text, const std::string_view needle)
     {
          std::size_t count = 0;
          std::size_t position = 0;
          while ((position = text.find(needle, position)) != std::string_view::npos)
          {
               ++count;
               position += needle.size();
          }
          return count;
     }

     [[nodiscard]] std::string StripAnsi(const std::string_view text)
     {
          std::string output;
          output.reserve(text.size());
          bool inEscapeSequence = false;
          bool sawControlSequenceIntroducer = false;
          for (const char character : text)
          {
               if (!inEscapeSequence && character == '\x1b')
               {
                    inEscapeSequence = true;
                    sawControlSequenceIntroducer = false;
                    continue;
               }
               if (inEscapeSequence)
               {
                    if (!sawControlSequenceIntroducer) sawControlSequenceIntroducer = character == '[';
                    else if (character >= '@' && character <= '~')
                         inEscapeSequence = false;
                    continue;
               }
               output.push_back(character);
          }
          return output;
     }
} // namespace

TEST_CASE("Diagnostics omit carets for shared source lines", "[unit][diagnostics]")
{
     TemporarySourceFile source{"    return add(1);\n"};
     auto error = std::make_unique<ecpps::diagnostics::TypeError>("  bad call  ", ecpps::Location{1, 12, 14});
     error->SubDiagnostics().push_back(std::make_unique<ecpps::diagnostics::Information>(
          "candidate", "  Candidate: add(int)  ", ecpps::Location{1, 12, 14}));
     ecpps::diagnostics::DiagnosticsMessage diagnostic{std::move(error)};

     const auto output = Render(source.Path(), diagnostic);
     const auto readable = StripAnsi(output);

     CHECK(readable.find(source.Path().string() + ":1:12 error: bad call") != std::string::npos);
     CHECK(readable.find(source.Path().string() + ":1:12 note: Candidate: add(int)") != std::string::npos);
     CHECK(readable.find("type-error") == std::string::npos);
     CHECK(readable.find("candidate :") == std::string::npos);
     CHECK(CountOccurrences(output, "1| ") == 1);
     CHECK(CountOccurrences(output, "^") == 0);
     CHECK(output.find("\x1b[31m" + source.Path().string() + ":1:12 error: \x1b[0m") != std::string::npos);
     CHECK(output.find("\x1b[36m" + source.Path().string() + ":1:12 note: \x1b[0m") != std::string::npos);
}

TEST_CASE("Diagnostics merge sibling branches and preserve declaration carets", "[unit][diagnostics]")
{
     ScopedEnvironmentVariable locale{"LC_ALL", "C.UTF-8"};
     TemporarySourceFile source{"// Example\n"
                                "int add(int);\n"
                                "int add(int, int);\n"
                                "int add(int, int, int*);\n"
                                "\n"
                                "int square(int num)\n"
                                "{\n"
                                "     return add(1, 2, 3);\n"
                                "}\n"};

     auto error = std::make_unique<ecpps::diagnostics::UnresolvedSymbolError>("add", "Unresolved function add",
                                                                              ecpps::Location{8, 13, 16});

     auto oneArgument = std::make_unique<ecpps::diagnostics::Information>("candidate", "Candidate: add(int) -> int",
                                                                          ecpps::Location{2, 2, 14});
     oneArgument->SubDiagnostics().push_back(std::make_unique<ecpps::diagnostics::Information>(
          "note", "Expected 1 argument, got 3", ecpps::Location{8, 13, 16}));
     oneArgument->SubDiagnostics().push_back(std::make_unique<ecpps::diagnostics::Information>(
          "note", "Argument 2 has no corresponding parameter", ecpps::Location{8, 20, 21}));
     oneArgument->SubDiagnostics().push_back(std::make_unique<ecpps::diagnostics::Information>(
          "note", "Argument 3 has no corresponding parameter", ecpps::Location{8, 23, 24}));
     error->SubDiagnostics().push_back(std::move(oneArgument));

     auto twoArguments = std::make_unique<ecpps::diagnostics::Information>(
          "candidate", "Candidate: add(int, int) -> int", ecpps::Location{3, 1, 19});
     twoArguments->SubDiagnostics().push_back(std::make_unique<ecpps::diagnostics::Information>(
          "note", "Expected 2 arguments, got 3", ecpps::Location{8, 13, 16}));
     twoArguments->SubDiagnostics().push_back(std::make_unique<ecpps::diagnostics::Information>(
          "note", "Argument 3 has no corresponding parameter", ecpps::Location{8, 23, 24}));
     error->SubDiagnostics().push_back(std::move(twoArguments));

     auto threeArguments = std::make_unique<ecpps::diagnostics::Information>(
          "candidate", "Candidate: add(int, int, int*) -> int", ecpps::Location{4, 1, 25});
     threeArguments->SubDiagnostics().push_back(std::make_unique<ecpps::diagnostics::Information>(
          "note", "Argument 3 of type 'int' cannot convert to parameter '<unnamed>' of type 'int*'",
          ecpps::Location{8, 23, 26}));
     error->SubDiagnostics().push_back(std::move(threeArguments));

     const auto output = StripAnsi(Render(source.Path(), ecpps::diagnostics::DiagnosticsMessage{std::move(error)}));
     const auto path = source.Path().string();
     const std::string expected =
          "┃ " + path +
          ":8:13 error: Unresolved symbol add: Unresolved function add\n"
          "┃ 8|      return add(1, 2, 3);\n"
          "┣━━━━┓ " +
          path +
          ":2:2 note: Candidate: add(int) -> int\n"
          "┃    ┃ 2| int add(int);\n"
          "┃    ┃  |  ^^^^^^^^^^^^\n"
          "┃    ┗━━━━┓ " +
          path +
          ":8:13 note: Expected 1 argument, got 3\n"
          "┃         ┃ " +
          path +
          ":8:20 note: Argument 2 has no corresponding parameter\n"
          "┃         ┃ " +
          path +
          ":8:23 note: Argument 3 has no corresponding parameter\n"
          "┣━━━━┓ " +
          path +
          ":3:1 note: Candidate: add(int, int) -> int\n"
          "┃    ┃ 3| int add(int, int);\n"
          "┃    ┃  | ^^^^^^^^^^^^^^^^^^\n"
          "┃    ┗━━━━┓ " +
          path +
          ":8:13 note: Expected 2 arguments, got 3\n"
          "┃         ┃ " +
          path +
          ":8:23 note: Argument 3 has no corresponding parameter\n"
          "┃\n"
          "┗━━━━┓ " +
          path +
          ":4:1 note: Candidate: add(int, int, int*) -> int\n"
          "     ┃ 4| int add(int, int, int*);\n"
          "     ┃  | ^^^^^^^^^^^^^^^^^^^^^^^^\n"
          "     ┗━━━━┓ " +
          path + ":8:23 note: Argument 3 of type 'int' cannot convert to parameter '<unnamed>' of type 'int*'\n";

     CHECK(output == expected);
}

TEST_CASE("Diagnostics trim message lines and preserve continuation lines", "[unit][diagnostics]")
{
     TemporarySourceFile source{"value\n"};
     ecpps::diagnostics::DiagnosticsMessage diagnostic =
          std::make_unique<ecpps::diagnostics::TypeError>("  bad value  \n  trace frame\t ", ecpps::Location{1, 1, 1});

     const auto output = Render(source.Path(), diagnostic);
     const auto readable = StripAnsi(output);

     CHECK(readable.find("error: bad value\n") != std::string::npos);
     CHECK(readable.find("trace frame\n") != std::string::npos);
     CHECK(readable.find("  bad value  ") == std::string::npos);
}

TEST_CASE("Diagnostics align visual columns for tabs and wide UTF-8 characters", "[unit][diagnostics]")
{
     TemporarySourceFile source{"\t界x\n"};
     ecpps::diagnostics::DiagnosticsMessage diagnostic =
          std::make_unique<ecpps::diagnostics::TypeError>("wide character", ecpps::Location{1, 5, 5});

     const auto output = Render(source.Path(), diagnostic);
     const auto readable = StripAnsi(output);

     CHECK(readable.find(source.Path().string() + ":1:11 error: wide character") != std::string::npos);
     CHECK(readable.find("1|         界x") != std::string::npos);
}

TEST_CASE("Diagnostics clamp invalid source ranges to one caret", "[unit][diagnostics]")
{
     TemporarySourceFile source{"abcdef\n"};
     ecpps::diagnostics::DiagnosticsMessage diagnostic =
          std::make_unique<ecpps::diagnostics::TypeError>("invalid range", ecpps::Location{1, 3, 0});

     const auto output = Render(source.Path(), diagnostic);
     const auto readable = StripAnsi(output);

     CHECK(readable.find(source.Path().string() + ":1:3 error: invalid range") != std::string::npos);
     CHECK(CountOccurrences(output, "^") == 1);
}

TEST_CASE("Diagnostics use ASCII rails when the locale is not UTF-8", "[unit][diagnostics]")
{
     ScopedEnvironmentVariable locale{"LC_ALL", "C"};
     auto error = std::make_unique<ecpps::diagnostics::TypeError>("root", ecpps::Location{});
     error->SubDiagnostics().push_back(
          std::make_unique<ecpps::diagnostics::Information>("hint", "child", ecpps::Location{}));
     ecpps::diagnostics::DiagnosticsMessage diagnostic{std::move(error)};

     const auto output = Render({}, diagnostic);

     CHECK(output.find("| \x1b[31merror: \x1b[0mroot") != std::string::npos);
     CHECK(output.find("+--- \x1b[36mnote: \x1b[0mchild") != std::string::npos);
}

TEST_CASE("Warnings use the warning label and colour", "[unit][diagnostics]")
{
     ecpps::diagnostics::DiagnosticsMessage diagnostic =
          std::make_unique<ecpps::diagnostics::Warning>("unused-variable", "unused value", ecpps::Location{});

     const auto output = Render({}, diagnostic);
     const auto readable = StripAnsi(output);

     CHECK(readable.find("warning: unused value") != std::string::npos);
     CHECK(output.find("\x1b[33mwarning: \x1b[0m") != std::string::npos);
}

TEST_CASE("Diagnostics retain a location when source text is unavailable", "[unit][diagnostics]")
{
     const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
     const auto missingPath =
          std::filesystem::temp_directory_path() / ("ecpps-diagnostics-missing-" + std::to_string(timestamp) + ".cpp");
     ecpps::diagnostics::DiagnosticsMessage diagnostic =
          std::make_unique<ecpps::diagnostics::TypeError>("missing source", ecpps::Location{1, 4, 4});

     const auto output = Render(missingPath, diagnostic);
     const auto readable = StripAnsi(output);

     CHECK(readable.find(missingPath.string() + ":1:4 error: missing source") != std::string::npos);
     CHECK(readable.find("1| ") == std::string::npos);
     CHECK(output.find('^') == std::string::npos);
}
