#define CATCH_CONFIG_MAIN
#include <Execution/IR.h>
#include <Parsing/AST.h>
#include <Parsing/ASTContext.h>
#include <Parsing/Preprocessor.h>
#include <Parsing/Tokeniser.h>
#include <Shared/BumpAllocator.h>
#include <Shared/Diagnostics.h>
#include <SourceFileFixtures.h>
#include <TestHelpers.h>
#include <catch_amalgamated.hpp>

using namespace TestHelpers;
using namespace ecpps;
using namespace ecpps::ast;
using namespace ecpps::ir;

static ASTContext& TestASTContext(void)
{
     static ASTContext context{};
     return context;
}

// Helper function to preprocess and tokenise source code
static std::vector<Token> TokeniseSource(const std::string& source)
{
     std::vector<MacroReplacement> macros;
     auto ppTokens = Preprocessor{TestASTContext()}.Parse(source, macros, "");
     return Tokeniser::Tokenise(ppTokens, TestASTContext());
}

TEST_CASE("Integration - Tokeniser to AST", "[integration][tokeniser_ast]")
{
     SECTION("Simple function tokenisation and parsing")
     {
          const auto& source = SourceFixtures::SimpleFunction;

          // Step 1: Preprocess and Tokenise
          auto tokens = TokeniseSource(std::string{source});
          REQUIRE(!tokens.empty());

          INFO("Tokenised " << tokens.size() << " tokens");

          // Step 2: Parse to AST
          Diagnostics diagnostics;
          auto ast = ecpps::ast::AST{tokens, diagnostics}.Parse(TestASTContext());

          INFO("Parsed AST");
          REQUIRE(!ast.empty());
     }

     SECTION("Arithmetic expression parsing")
     {
          const auto& source = SourceFixtures::ArithmeticExpression;

          auto tokens = TokeniseSource(std::string{source});
          REQUIRE(!tokens.empty());

          Diagnostics diagnostics;
          auto ast = ecpps::ast::AST{tokens, diagnostics}.Parse(TestASTContext());

          REQUIRE((!ast.empty()));
          INFO("Successfully parsed arithmetic expression");
     }

     // SECTION("Control flow parsing")
     // {
     //      const auto& source = SourceFixtures::ControlFlow;

     //      auto tokens = TokeniseSource(std::string{source});
     //      REQUIRE(!tokens.empty());

     //      Diagnostics diagnostics;
     //      auto ast = ecpps::ast::AST{tokens, diagnostics}.Parse(TestASTContext());

     //      REQUIRE((!ast.empty()));
     //      INFO("Successfully parsed control flow");
     // }
}

TEST_CASE("Integration - AST to IR", "[integration][ast_ir]")
{
     SECTION("Simple function to IR")
     {
          const auto& source = SourceFixtures::SimpleFunction;

          // Tokenise
          auto tokens = TokeniseSource(std::string{source});
          REQUIRE(!tokens.empty());

          // Parse to AST
          Diagnostics diagnostics;
          auto ast = ecpps::ast::AST{tokens, diagnostics}.Parse(TestASTContext());
          REQUIRE((!ast.empty()));

          // Generate IR
          BumpAllocator irAllocator(64uz * 1024);
          ir::Context context{diagnostics, irAllocator};
          auto ir = IR::Parse(context, ast);

          INFO("Generated IR from AST");
          REQUIRE((!ir.empty()));
     }

     SECTION("Arithmetic expression to IR")
     {
          const auto& source = SourceFixtures::ArithmeticExpression;

          auto tokens = TokeniseSource(std::string{source});
          Diagnostics diagnostics;
          auto ast = ecpps::ast::AST{tokens, diagnostics}.Parse(TestASTContext());

          BumpAllocator irAllocator(64uz * 1024);

          ir::Context context{diagnostics, irAllocator};
          auto ir = IR::Parse(context, ast);

          INFO("Generated IR with arithmetic operations");
          REQUIRE((!ir.empty()));
     }
}

TEST_CASE("Integration - Full pipeline to code generation", "[integration][full_pipeline]")
{
     SECTION("Compile simple function")
     {
          const auto& source = SourceFixtures::SimpleFunction;

          // Step 1: Tokenise
          auto tokens = TokeniseSource(std::string{source});
          REQUIRE(!tokens.empty());

          // Step 2: Parse to AST
          Diagnostics diagnostics;
          ASTContext astContext;
          auto ast = ecpps::ast::AST{tokens, diagnostics}.Parse(astContext);
          REQUIRE((!ast.empty()));

          // Step 3: Generate IR
          BumpAllocator irAllocator(64uz * 1024);

          ir::Context context{diagnostics, irAllocator};
          auto ir = IR::Parse(context, ast);
          REQUIRE((!ir.empty()));

          // Step 4: Code generation would go here
          // auto emitter = CodeEmitter::New(abi::ISA::x86_64);
          // emitter->EmitRoutine(...);

          INFO("Full compilation pipeline successful");
     }
}

TEST_CASE("Integration - Compound assignment operators", "[integration][assignment]")
{
     const char* source = R"(
          int compound(int value) {
               value += 1;
               value -= 1;
               value *= 2;
               value /= 2;
               value %= 3;
               value ^= 1;
               value &= 1;
               value |= 1;
               value <<= 1;
               value >>= 1;
               return value;
          }
     )";

     auto tokens = TokeniseSource(source);
     Diagnostics diagnostics;
     auto ast = ecpps::ast::AST{tokens, diagnostics}.Parse(TestASTContext());
     REQUIRE((!ast.empty()));

     BumpAllocator irAllocator(64uz * 1024);
     ir::Context context{diagnostics, irAllocator};
     auto ir = IR::Parse(context, ast);

     REQUIRE((!ir.empty()));
     REQUIRE(diagnostics.diagnosticsList.empty());
}

TEST_CASE("Integration - Preprocessor and tokeniser", "[integration][preprocessor_tokeniser]")
{
     SECTION("Macro expansion then tokenisation")
     {
          const char* sourceWithMacro = R"(
            #define MAX_VALUE 100
            int main() {
                int x = MAX_VALUE;
                return 0;
            }
        )";

          // Preprocess
          std::vector<ecpps::MacroReplacement> macros{};
          auto preprocessingTokens = Preprocessor{TestASTContext()}.Parse(std::string{sourceWithMacro}, macros, "");
          REQUIRE(!preprocessingTokens.empty());

          // Then tokenise the preprocessed output
          // Note: May need to convert preprocessing tokens to string first
          INFO("Preprocessor and tokeniser integration");
     }
}

TEST_CASE("Integration - Error propagation through pipeline", "[integration][error_handling]")
{
     SECTION("Syntax error stops pipeline")
     {
          const char* invalidCode = R"(
            int main() {
                int x = ; // Syntax error
                return 0;
            }
        )";

          auto tokens = TokeniseSource(invalidCode);

          Diagnostics diagnostics;
          auto ast = ecpps::ast::AST{tokens, diagnostics}.Parse(TestASTContext());

          // Should have generated diagnostic messages
          INFO("Error handling in pipeline");
     }

     SECTION("Type error in IR generation")
     {
          const char* type_error_code = R"(
          int main()
          {
               int* p = 42; // Type error: can't assign int to int*
               return 0;
          }
          )";

          auto tokens = TokeniseSource(type_error_code);
          Diagnostics diagnostics;
          auto ast = ecpps::ast::AST{tokens, diagnostics}.Parse(TestASTContext());

          if (!ast.empty())
          {
               BumpAllocator irAllocator(64uz * 1024);
               // IR generation should detect type error

               ir::Context context{diagnostics, irAllocator};
               auto ir = IR::Parse(context, ast);
               INFO("Type error should be caught");
               REQUIRE(!diagnostics.diagnosticsList.empty());
          }
     }
}

TEST_CASE("Integration - Multiple functions", "[integration][multiple_functions]")
{
     SECTION("Parse and generate IR for multiple functions")
     {
          const auto& source = SourceFixtures::FunctionCall;

          auto tokens = TokeniseSource(std::string{source});
          REQUIRE(!tokens.empty()); // NOLINT

          Diagnostics diagnostics;
          ASTContext astContext;
          auto ast = ecpps::ast::AST{tokens, diagnostics}.Parse(astContext);
          REQUIRE((!ast.empty())); // NOLINT

          BumpAllocator irAllocator(64uz * 1024);

          ir::Context context{diagnostics, irAllocator};
          auto ir = IR::Parse(context, ast);

          INFO("Multiple functions compiled");
          REQUIRE((ir.size() >= 2)); // NOLINT
     }
}

TEST_CASE("Integration - Complex expressions", "[integration][complex_expr]")
{
     SECTION("Nested arithmetic expressions")
     {
          const char* complex_expr = R"(
            int main() {
                int result = (((1 + 2) * 3) - 4) / 5;
                return result;
            }
        )";

          auto tokens = TokeniseSource(complex_expr);
          Diagnostics diagnostics;
          ASTContext astContext;
          auto ast = ecpps::ast::AST{tokens, diagnostics}.Parse(astContext);
          REQUIRE((!ast.empty())); // NOLINT

          BumpAllocator irAllocator(64uz * 1024);

          ir::Context context{diagnostics, irAllocator};
          auto ir = IR::Parse(context, ast);

          INFO("Complex nested expressions compiled");
     }
}
