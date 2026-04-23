#define CATCH_CONFIG_MAIN
#include <Parsing/Tokeniser.h>
#include <catch_amalgamated.hpp>
#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

TEST_CASE("Simpe programs", "[integration][tokeniser]")
{
     std::vector<ecpps::MacroReplacement> macros;

     SECTION("Simple function")
     {
          const char* source = R"(
		  int main()
	       {
			 return 0;
		  }
	   )";
          auto tokens = ecpps::Tokeniser::Tokenise(ecpps::Preprocessor{}.Parse(source, macros, ""));
          REQUIRE(tokens.size() == 9);
          REQUIRE(tokens[0].AsKeyword() == "int");
          REQUIRE(tokens[1].AsIdentifier() == "main");
          REQUIRE(tokens[2].type == ecpps::TokenType::LeftParenthesis);
          REQUIRE(tokens[3].type == ecpps::TokenType::RightParenthesis);
          REQUIRE(tokens[4].type == ecpps::TokenType::LeftBrace);
          REQUIRE(tokens[5].AsKeyword() == "return");
          REQUIRE(tokens[6].IsLiteral());
          REQUIRE(tokens[7].type == ecpps::TokenType::SemiColon);
          REQUIRE(tokens[8].type == ecpps::TokenType::RightBrace);
     }
     SECTION("Function with arithmetic")
     {
          const char* source = R"(
		  int add(int a, int b)
		  {
			 return a + b;
		  }
	   )";
          auto tokens = ecpps::Tokeniser::Tokenise(ecpps::Preprocessor{}.Parse(source, macros, ""));
          REQUIRE(tokens.size() == 16);
          REQUIRE(tokens[0].AsKeyword() == "int");
          REQUIRE(tokens[1].AsIdentifier() == "add");
          REQUIRE(tokens[2].type == ecpps::TokenType::LeftParenthesis);
          REQUIRE(tokens[3].AsKeyword() == "int");
          REQUIRE(tokens[4].AsIdentifier() == "a");
          REQUIRE(tokens[5].AsOperator() == ",");
          REQUIRE(tokens[6].AsKeyword() == "int");
          REQUIRE(tokens[7].AsIdentifier() == "b");
          REQUIRE(tokens[8].type == ecpps::TokenType::RightParenthesis);
          REQUIRE(tokens[9].type == ecpps::TokenType::LeftBrace);
          REQUIRE(tokens[10].AsKeyword() == "return");
          REQUIRE(tokens[11].AsIdentifier() == "a");
          REQUIRE(tokens[12].AsOperator() == "+");
          REQUIRE(tokens[13].AsIdentifier() == "b");
          REQUIRE(tokens[14].type == ecpps::TokenType::SemiColon);
          REQUIRE(tokens[15].type == ecpps::TokenType::RightBrace);
     }
}

TEST_CASE("Advanced macros", "[integration][tokeniser]")
{
     std::vector<ecpps::MacroReplacement> macros;
     SECTION("Object-like macro")
     {
          const char* source = R"(
#define PI 3.14
int CircleArea(float radius)
{
	return PI * radius * radius;
}
	   )";
          auto tokens = ecpps::Tokeniser::Tokenise(ecpps::Preprocessor{}.Parse(source, macros, ""));
          REQUIRE(tokens.size() == 15);
          REQUIRE(tokens[0].AsKeyword() == "int");
          REQUIRE(tokens[1].AsIdentifier() == "CircleArea");
          REQUIRE(tokens[2].type == ecpps::TokenType::LeftParenthesis);
          REQUIRE(tokens[3].AsKeyword() == "float");
          REQUIRE(tokens[4].AsIdentifier() == "radius");
          REQUIRE(tokens[5].type == ecpps::TokenType::RightParenthesis);
          REQUIRE(tokens[6].type == ecpps::TokenType::LeftBrace);
          REQUIRE(tokens[7].AsKeyword() == "return");
          REQUIRE(tokens[8].IsLiteral());
          REQUIRE(tokens[9].AsOperator() == "*");
          REQUIRE(tokens[10].AsIdentifier() == "radius");
          REQUIRE(tokens[11].AsOperator() == "*");
          REQUIRE(tokens[12].AsIdentifier() == "radius");
          REQUIRE(tokens[13].type == ecpps::TokenType::SemiColon);
          REQUIRE(tokens[14].type == ecpps::TokenType::RightBrace);
     }
}
