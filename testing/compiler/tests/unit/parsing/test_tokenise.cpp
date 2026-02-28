#define CATCH_CONFIG_MAIN
#include <Parsing/Preprocessor.h>
#include <Parsing/Tokeniser.h>
#include <TestHelpers.h>
#include <catch_amalgamated.hpp>
#include <string>
#include <vector>

using namespace TestHelpers;
using namespace ecpps;

// Helper function to preprocess and tokenise source code
static std::vector<Token> TokeniseSource(const std::string& source)
{
     std::vector<MacroReplacement> macros;
     auto ppTokens = Preprocessor::Parse(source, macros);
     return Tokeniser::Tokenise(ppTokens);
}

TEST_CASE("Tokeniser - Keywords", "[parsing][tokeniser]")
{
     SECTION("Basic control flow keywords")
     {
          // Test individual keywords
          auto tokens = TokeniseSource("if else for while do return");

          INFO("Tokenised " << tokens.size() << " tokens");
          REQUIRE((tokens.size() >= 6));

          // Verify we have keyword tokens
          bool has_keywords = false;
          for (const auto& token : tokens)
          {
               if (token.type == TokenType::Keyword)
               {
                    has_keywords = true;
                    break;
               }
          }

          REQUIRE(has_keywords);
     }

     SECTION("Type keywords")
     {
          auto tokens = TokeniseSource("int char float double void");
          REQUIRE((tokens.size() >= 5));
     }

     SECTION("Modern C++ keywords")
     {
          auto tokens = TokeniseSource("constexpr consteval constinit concept requires");
          REQUIRE(!tokens.empty());
     }
}

TEST_CASE("Tokeniser - Identifiers", "[parsing][tokeniser]")
{
     SECTION("Valid identifiers")
     {
          auto tokens = TokeniseSource("variable _private count123 __internal");

          // Should have multiple identifier tokens
          int identifier_count = 0;
          for (const auto& token : tokens)
          {
               if (token.type == TokenType::Identifier) { identifier_count++; }
          }

          REQUIRE((identifier_count >= 4));
     }

     SECTION("Single character identifier")
     {
          auto tokens = TokeniseSource("x");
          REQUIRE(!tokens.empty());
     }

     SECTION("Identifiers with underscores")
     {
          auto tokens = TokeniseSource("my_variable __special__");
          REQUIRE((tokens.size() >= 2));
     }
}

TEST_CASE("Tokeniser - Operators", "[parsing][tokeniser]")
{
     SECTION("Arithmetic operators")
     {
          auto tokens = TokeniseSource("+ - * / %");
          REQUIRE((tokens.size() >= 5));
     }

     SECTION("Bitwise operators")
     {
          auto tokens = TokeniseSource("<< >> & | ^");
          REQUIRE((tokens.size() >= 5));
     }

     SECTION("Comparison operators")
     {
          auto tokens = TokeniseSource("== != < > <= >=");
          REQUIRE((tokens.size() >= 6));
     }

     SECTION("Assignment operators")
     {
          auto tokens = TokeniseSource("= += -= *= /= %=");
          REQUIRE((tokens.size() >= 6));
     }

     SECTION("Increment and decrement")
     {
          auto tokens = TokeniseSource("++ --");
          REQUIRE((tokens.size() >= 2));
     }
}

TEST_CASE("Tokeniser - Integer literals", "[parsing][tokeniser]")
{
     SECTION("Decimal integers")
     {
          auto tokens = TokeniseSource("0 42 123 9999");

          int literal_count = 0;
          for (const auto& token : tokens)
          {
               if (token.type == TokenType::Literal) { literal_count++; }
          }

          REQUIRE((literal_count >= 4));
     }

     SECTION("Hexadecimal integers")
     {
          auto tokens = TokeniseSource("0x00 0xFF 0xDEADBEEF");
          REQUIRE(!tokens.empty());
     }

     SECTION("Binary integers")
     {
          auto tokens = TokeniseSource("0b0 0b1 0b1010 0b11111111");
          REQUIRE(!tokens.empty());
     }

     SECTION("Octal integers")
     {
          auto tokens = TokeniseSource("0 07 077 0777");
          REQUIRE(!tokens.empty());
     }

     SECTION("Integers with digit separators")
     {
          auto tokens = TokeniseSource("1'000 1'000'000");
          REQUIRE(!tokens.empty());
     }
}

TEST_CASE("Tokeniser - Floating point literals", "[parsing][tokeniser]")
{
     SECTION("Basic floats")
     {
          auto tokens = TokeniseSource("3.14 2.718 0.5");
          REQUIRE(!tokens.empty());
     }

     SECTION("Scientific notation")
     {
          auto tokens = TokeniseSource("1e10 1.5e-5 2.0E+3");
          REQUIRE(!tokens.empty());
     }

     SECTION("Float suffixes")
     {
          auto tokens = TokeniseSource("3.14f 2.718F 1.0l 2.0L");
          REQUIRE(!tokens.empty());
     }
}

TEST_CASE("Tokeniser - String literals", "[parsing][tokeniser]")
{
     SECTION("Basic string")
     {
          auto tokens = TokeniseSource(R"("hello world")");
          REQUIRE(!tokens.empty());
     }

     SECTION("String with escape sequences")
     {
          auto tokens = TokeniseSource(R"("line1\nline2\ttab")");
          REQUIRE(!tokens.empty());
     }

     SECTION("Raw string literal")
     {
          auto tokens = TokeniseSource(R"delim(R"(raw string with \n literal)")delim");
          REQUIRE(!tokens.empty());
     }

     SECTION("Empty string")
     {
          auto tokens = TokeniseSource(R"("")");
          REQUIRE(!tokens.empty());
     }
}

TEST_CASE("Tokeniser - Character literals", "[parsing][tokeniser]")
{
     SECTION("Basic character")
     {
          auto tokens = TokeniseSource("'a' 'Z' '0'");
          REQUIRE((tokens.size() >= 3));
     }

     SECTION("Escape sequences")
     {
          auto tokens = TokeniseSource(R"('\n' '\t' '\'' '\\')");
          REQUIRE(!tokens.empty());
     }

     SECTION("Hex character")
     {
          auto tokens = TokeniseSource(R"('\x41')");
          REQUIRE(!tokens.empty());
     }
}

TEST_CASE("Tokeniser - Delimiters", "[parsing][tokeniser]")
{
     SECTION("Parentheses")
     {
          auto tokens = TokeniseSource("( )");

          bool has_left = false;
          bool has_right = false;
          for (const auto& token : tokens)
          {
               if (token.type == TokenType::LeftParenthesis) has_left = true;
               if (token.type == TokenType::RightParenthesis) has_right = true;
          }

          REQUIRE(has_left);
          REQUIRE(has_right);
     }

     SECTION("Brackets")
     {
          auto tokens = TokeniseSource("[ ]");

          bool has_left = false;
          bool has_right = false;
          for (const auto& token : tokens)
          {
               if (token.type == TokenType::LeftBracket) has_left = true;
               if (token.type == TokenType::RightBracket) has_right = true;
          }

          REQUIRE(has_left);
          REQUIRE(has_right);
     }

     SECTION("Braces")
     {
          auto tokens = TokeniseSource("{ }");

          bool has_left = false;
          bool has_right = false;
          for (const auto& token : tokens)
          {
               if (token.type == TokenType::LeftBrace) has_left = true;
               if (token.type == TokenType::RightBrace) has_right = true;
          }

          REQUIRE(has_left);
          REQUIRE(has_right);
     }

     SECTION("Semicolon and colon")
     {
          auto tokens = TokeniseSource("; :");

          bool has_semicolon = false;
          bool has_colon = false;
          for (const auto& token : tokens)
          {
               if (token.type == TokenType::SemiColon) has_semicolon = true;
               if (token.type == TokenType::Colon) has_colon = true;
          }

          REQUIRE(has_semicolon);
          REQUIRE(has_colon);
     }
}

TEST_CASE("Tokeniser - Comments", "[parsing][tokeniser]")
{
     SECTION("Single-line comment")
     {
          auto tokens = TokeniseSource("int x; // This is a comment\nint y;");
          // Comments should be stripped
          REQUIRE(!tokens.empty());
     }

     SECTION("Multi-line comment")
     {
          auto tokens = TokeniseSource("int x; /* comment\n spanning multiple\n lines */ int y;");
          REQUIRE(!tokens.empty());
     }
}

TEST_CASE("Tokeniser - Whitespace handling", "[parsing][tokeniser]")
{
     SECTION("Spaces between tokens")
     {
          auto tokens1 = TokeniseSource("int x");
          auto tokens2 = TokeniseSource("int     x");

          // Should produce same number of meaningful tokens
          REQUIRE((tokens1.size() == tokens2.size()));
     }

     SECTION("Tab and newline")
     {
          auto tokens = TokeniseSource("int\tx\n;\r\n");
          REQUIRE((tokens.size() >= 3)); // int, x, ;
     }
}

TEST_CASE("Tokeniser - Complete statements", "[parsing][tokeniser]")
{
     SECTION("Variable declaration")
     {
          auto tokens = TokeniseSource("int x = 42;");
          REQUIRE((tokens.size() >= 5)); // int, x, =, 42, ;
     }

     SECTION("Function signature")
     {
          auto tokens = TokeniseSource("int main(int argc, char* argv[])");
          REQUIRE(!tokens.empty());
     }

     SECTION("If statement")
     {
          auto tokens = TokeniseSource("if (x > 0) { return 1; }");
          REQUIRE(!tokens.empty());
     }
}

TEST_CASE("Tokeniser - Edge cases", "[parsing][tokeniser]")
{
     SECTION("Empty input")
     {
          auto tokens = TokeniseSource("");
          REQUIRE(tokens.empty());
     }

     SECTION("Only whitespace")
     {
          auto tokens = TokeniseSource("   \t\n\r\n  ");
          REQUIRE(tokens.empty());
     }

     SECTION("Very long line")
     {
          std::string long_str = "int " + std::string(10000, 'x') + ";";
          auto tokens = TokeniseSource(long_str);
          REQUIRE(!tokens.empty());
     }
}
