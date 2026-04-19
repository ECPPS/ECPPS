#define CATCH_CONFIG_MAIN
#include <Parsing/Preprocessor.h>
#include <catch_amalgamated.hpp>
#include <string>
#include <vector>
#include <ranges>
#include <algorithm>

TEST_CASE("Preprocessor - Identifiers", "[parsing][identifiers]")
{
	ecpps::Preprocessor preprocessor{};
     std::vector<ecpps::MacroReplacement> macros{};

	SECTION("Simple identifiers")
     {
          auto tokens = preprocessor.Parse("int main() {}", macros, "tests.cpp");
          std::size_t identifierCount = std::ranges::count_if(tokens, [](const auto& token)
                                                              { return token.type == ecpps::PreprocessingTokenType::Identifier; });
          
          REQUIRE((identifierCount == 2));
          REQUIRE(tokens[0].value == "int");
          REQUIRE(tokens[1].value == "main");
     }

	SECTION("Source locations")
     {
          auto tokens = preprocessor.Parse("int main string test\nhello world\n     really", macros, "tests.cpp");
          REQUIRE(tokens.size() == 7);
          REQUIRE(tokens[0].source.line == 1);
          REQUIRE(tokens[0].source.position == 1);
          REQUIRE(tokens[1].source.line == 1);
          REQUIRE(tokens[1].source.position == 5);
          REQUIRE(tokens[2].source.line == 1);
          REQUIRE(tokens[2].source.position == 10);
          REQUIRE(tokens[3].source.line == 1);
          REQUIRE(tokens[3].source.position == 17);
          REQUIRE(tokens[4].source.line == 2);
          REQUIRE(tokens[4].source.position == 1);
          REQUIRE(tokens[5].source.line == 2);
          REQUIRE(tokens[5].source.position == 7);
          REQUIRE(tokens[6].source.line == 3);
          REQUIRE(tokens[6].source.position == 6);
     }

	SECTION("Identifiers with underscores and digits")
     {
          auto tokens = preprocessor.Parse("variable _private count123 __internal", macros, "tests.cpp");
          std::size_t identifierCount = std::ranges::count_if(tokens, [](const auto& token)
                                                              { return token.type == ecpps::PreprocessingTokenType::Identifier; });

          REQUIRE((identifierCount == 4));
          REQUIRE(tokens[0].value == "variable");
          REQUIRE(tokens[1].value == "_private");
          REQUIRE(tokens[2].value == "count123");
          REQUIRE(tokens[3].value == "__internal");
     }
}

TEST_CASE("Preprocessor - operator-or-punctuators", "[parsing][operators-or-punctuators]")
{
     ecpps::Preprocessor preprocessor{};
     std::vector<ecpps::MacroReplacement> macros{};

     SECTION("Arithmetic operators")
     {
          auto tokens = preprocessor.Parse("+ - * / %", macros, "tests.cpp");
          std::size_t operatorCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::OperatorOrPunctuator; });
          REQUIRE((operatorCount == 5));
     }
     SECTION("Bitwise operators")
     {
          auto tokens = preprocessor.Parse("<< >> & | ^", macros, "tests.cpp");
          std::size_t operatorCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::OperatorOrPunctuator; });
          REQUIRE((operatorCount == 5));
     }
     SECTION("Comparison operators")
     {
          auto tokens = preprocessor.Parse("== != < > <= >=", macros, "tests.cpp");
          std::size_t operatorCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::OperatorOrPunctuator; });
          REQUIRE((operatorCount == 6));
     }
     SECTION("Assignment operators")
     {
          auto tokens = preprocessor.Parse("= += -= *= /= %=", macros, "tests.cpp");
          std::size_t operatorCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::OperatorOrPunctuator; });
          REQUIRE((operatorCount == 6));
     }
     SECTION("Increment and decrement")
     {
          auto tokens = preprocessor.Parse("++ --", macros, "tests.cpp");
          std::size_t operatorCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::OperatorOrPunctuator; });
          REQUIRE((operatorCount == 2));
     }

     SECTION("Ambiguous operators")
     {
          auto tokens = preprocessor.Parse("& && | ||", macros, "tests.cpp");
          std::size_t operatorCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::OperatorOrPunctuator; });
          REQUIRE((operatorCount == 4));
          REQUIRE(tokens[0].value == "&");
          REQUIRE(tokens[1].value == "&&");
          REQUIRE(tokens[2].value == "|");
          REQUIRE(tokens[3].value == "||");
     }
}

// TODO: includes

TEST_CASE("Preprocessor - Literals", "[parsing][literals]")
{
     ecpps::Preprocessor preprocessor{};
     std::vector<ecpps::MacroReplacement> macros{};
     SECTION("Integer literals")
     {
          auto tokens = preprocessor.Parse(
              "0 42 123 9999 0x00 0xFF 0xDEADBEEF 0b0 0b1 0b1010 0b11111111 0 07 077 0777 1'000 1'000'000", macros,
              "tests.cpp");
          std::size_t literalCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::Number; });
          REQUIRE(literalCount == 17);
          REQUIRE(tokens[0].value == "0");
          REQUIRE(tokens[1].value == "42");
          REQUIRE(tokens[2].value == "123");
          REQUIRE(tokens[3].value == "9999");
          REQUIRE(tokens[4].value == "0x00");
          REQUIRE(tokens[5].value == "0xFF");
          REQUIRE(tokens[6].value == "0xDEADBEEF");
          REQUIRE(tokens[7].value == "0b0");
          REQUIRE(tokens[8].value == "0b1");
          REQUIRE(tokens[9].value == "0b1010");
          REQUIRE(tokens[10].value == "0b11111111");
          REQUIRE(tokens[11].value == "0");
          REQUIRE(tokens[12].value == "07");
          REQUIRE(tokens[13].value == "077");
          REQUIRE(tokens[14].value == "0777");
          REQUIRE(tokens[15].value == "1000");
          REQUIRE(tokens[16].value == "1000000");
     }
     SECTION("Floating point literals")
     {
          auto tokens =
              preprocessor.Parse("3.14 2.718 0.5 1e10 3.14f 2.718F 1.0l 2.0L", macros, "tests.cpp");
          std::size_t literalCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::Number; });
          REQUIRE(literalCount == 8);
          REQUIRE(tokens[0].value == "3.14");
          REQUIRE(tokens[1].value == "2.718");
          REQUIRE(tokens[2].value == "0.5");
          REQUIRE(tokens[3].value == "1e10");
          REQUIRE(tokens[4].value == "3.14f");
          REQUIRE(tokens[5].value == "2.718F");
          REQUIRE(tokens[6].value == "1.0l");
          REQUIRE(tokens[7].value == "2.0L");
     }
     SECTION("Character literals")
     {
          auto tokens = preprocessor.Parse("'a' '\\n' '\\''", macros, "tests.cpp");
          std::size_t literalCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::CharacterLiteral; });
          REQUIRE(literalCount == 3);
          REQUIRE(tokens[0].value == "a");
          REQUIRE(tokens[1].value == "\n");
          REQUIRE(tokens[2].value == "\'");
     }

	SECTION("String literals")
     {
          auto tokens =
              preprocessor.Parse(R"("Hello, World!" "Line 1\nLine 2" "She said, \"Hello!\"")", macros, "tests.cpp");
          std::size_t literalCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::StringLiteral; });
          REQUIRE(literalCount == 3);
          REQUIRE(tokens[0].value == "Hello, World!");
          REQUIRE(tokens[1].value == "Line 1\nLine 2");
          REQUIRE(tokens[2].value == "She said, \"Hello!\"");
     }
}

TEST_CASE("Preprocessor - Macros", "[parsing][macros]")
{
     ecpps::Preprocessor preprocessor{};
     std::vector<ecpps::MacroReplacement> macros{};
     SECTION("Object-like macros")
     {
          macros.push_back(ecpps::MacroReplacement("PI", {} , "3.14", false));
          auto tokens = preprocessor.Parse("double pi = PI;", macros, "tests.cpp");
          std::size_t identifierCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::Identifier; });
          std::size_t numberCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::Number; });
          REQUIRE(identifierCount == 2);
          REQUIRE(numberCount == 1);
          REQUIRE(tokens[0].value == "double");
          REQUIRE(tokens[1].value == "pi");
          REQUIRE(tokens[2].value == "=");
          REQUIRE(tokens[3].value == "3.14");
          REQUIRE(tokens[4].value == ";");
     }

	SECTION("Function-like macros")
     {
          macros.push_back(ecpps::MacroReplacement("SQUARE", std::vector<std::string>{"x"}, "((x) * (x))", false));
          auto tokens = preprocessor.Parse("int result = SQUARE(5);", macros, "tests.cpp");
          for (const auto& token : tokens) std::print("{}", token.value);
          std::size_t identifierCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::Identifier; });
          std::size_t numberCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::Number; });
          REQUIRE(identifierCount == 2);
          REQUIRE(numberCount == 2);
          REQUIRE(tokens.size() == 13);
          REQUIRE(tokens[0].value == "int");
          REQUIRE(tokens[1].value == "result");
          REQUIRE(tokens[2].value == "=");
          REQUIRE(tokens[3].value == "(");
          REQUIRE(tokens[4].value == "(");
          REQUIRE(tokens[5].value == "5");
          REQUIRE(tokens[6].value == ")");
          REQUIRE(tokens[7].value == "*");
          REQUIRE(tokens[8].value == "(");
          REQUIRE(tokens[9].value == "5");
          REQUIRE(tokens[10].value == ")");
          REQUIRE(tokens[11].value == ")");
          REQUIRE(tokens[12].value == ";");
     }

	SECTION("Nested macros")
     {
          macros.push_back(ecpps::MacroReplacement("PI", {}, "3.14", false));
          macros.push_back(
              ecpps::MacroReplacement("CIRCLE_AREA", std::vector<std::string>{"r"}, "(PI * (r) * (r))", false));
          auto tokens = preprocessor.Parse("double area = CIRCLE_AREA(5);", macros, "tests.cpp");
          std::size_t identifierCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::Identifier; });
          std::size_t numberCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::Number; });
          REQUIRE(identifierCount == 2);
          REQUIRE(numberCount == 3);
          REQUIRE(tokens.size() == 15);
          REQUIRE(tokens[0].value == "double");
          REQUIRE(tokens[1].value == "area");
          REQUIRE(tokens[2].value == "=");
          REQUIRE(tokens[3].value == "(");
          REQUIRE(tokens[4].value == "3.14");
          REQUIRE(tokens[5].value == "*");
          REQUIRE(tokens[6].value == "(");
          REQUIRE(tokens[7].value == "5");
          REQUIRE(tokens[8].value == ")");
          REQUIRE(tokens[9].value == "*");
          REQUIRE(tokens[10].value == "(");
          REQUIRE(tokens[11].value == "5");
          REQUIRE(tokens[12].value == ")");
          REQUIRE(tokens[13].value == ")");
          REQUIRE(tokens[14].value == ";");
     }

	SECTION("Concatenation")
     {
          macros.push_back(ecpps::MacroReplacement("CONCAT", std::vector<std::string>{"a", "b"}, "a##b", false));
          auto tokens = preprocessor.Parse("int xy = CONCAT(x, y);", macros, "tests.cpp");
          std::size_t identifierCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::Identifier; });
          REQUIRE(identifierCount == 3);
          REQUIRE(tokens.size() == 5);
          REQUIRE(tokens[0].value == "int");
          REQUIRE(tokens[1].value == "xy");
          REQUIRE(tokens[2].value == "=");
          REQUIRE(tokens[3].value == "xy");
          REQUIRE(tokens[4].value == ";");
     }

	SECTION("Stringification")
     {
          macros.push_back(ecpps::MacroReplacement("STRINGIFY", std::vector<std::string>{"x"}, "#x", false));
          auto tokens = preprocessor.Parse("const char* str = STRINGIFY(Hello kitten!);", macros, "tests.cpp");
          std::size_t identifierCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::Identifier; });
          std::size_t stringLiteralCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::StringLiteral; });
          REQUIRE(identifierCount == 3);
          REQUIRE(stringLiteralCount == 1);
          REQUIRE(tokens.size() == 7);
          REQUIRE(tokens[0].value == "const");
          REQUIRE(tokens[1].value == "char");
          REQUIRE(tokens[2].value == "*");
          REQUIRE(tokens[3].value == "str");
          REQUIRE(tokens[4].value == "=");
          REQUIRE(tokens[5].value == "Hello kitten!");
          REQUIRE(tokens[6].value == ";");
     }

	SECTION("Variadic macros")
     {
          macros.push_back(
              ecpps::MacroReplacement("LOG", std::vector<std::string>{}, "printf(__VA_ARGS__)", true));
          auto tokens = preprocessor.Parse("LOG(\"Error: %d\", errorCode);", macros, "tests.cpp");
          for (const auto& token : tokens) std::print("{}", token.value);
          std::size_t identifierCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::Identifier; });
          std::size_t stringLiteralCount = std::ranges::count_if(
              tokens, [](const auto& token) { return token.type == ecpps::PreprocessingTokenType::StringLiteral; });
          REQUIRE(identifierCount == 2);
          REQUIRE(stringLiteralCount == 1);
          REQUIRE(tokens.size() == 7);
          REQUIRE(tokens[0].value == "printf");
          REQUIRE(tokens[1].value == "(");
          REQUIRE(tokens[2].value == "Error: %d");
          REQUIRE(tokens[3].value == ",");
          REQUIRE(tokens[4].value == "errorCode");
          REQUIRE(tokens[5].value == ")");
          REQUIRE(tokens[6].value == ";");
     }
}
