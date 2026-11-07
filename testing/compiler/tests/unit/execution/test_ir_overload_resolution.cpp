#define CATCH_CONFIG_MAIN
#include <Execution/OverloadDiagnostics.h>
#include <IRTestHelpers.h>
#include <catch_amalgamated.hpp>
#include <algorithm>
#include <format>
#include <iterator>
#include <string_view>

using namespace IRTestHelpers;

namespace
{
     std::vector<ecpps::ast::NodePointer> MakeOverloadProgram(Fixture& fixture, const std::string& firstParameter,
                                                              const std::string& secondParameter,
                                                              const std::string& callerParameter)
     {
          std::vector<ecpps::ast::NodePointer> ast{};
          ast.push_back(fixture.MakeFunctionDeclaration("pick", firstParameter));
          ast.push_back(fixture.MakeFunctionDeclaration("pick", secondParameter));
          ast.push_back(fixture.MakeOverloadCaller(callerParameter));
          return ast;
     }

     ecpps::ast::NodePointer MakeFunctionDeclaration(Fixture& fixture, const std::string& name,
                                                     const std::vector<std::string>& parameterTypes)
     {
          std::vector<std::pair<std::string, std::string>> parameters{};
          parameters.reserve(parameterTypes.size());
          for (std::size_t index = 0; index < parameterTypes.size(); index++)
               parameters.emplace_back(parameterTypes[index], std::format("parameter{}", index));
          auto signature = fixture.MakeFunctionSignature("int", name, parameters);
          return MakeASTNode<ecpps::ast::FunctionDeclarationNode>(fixture.astAllocator, std::move(signature), Source);
     }

     std::vector<ecpps::ast::NodePointer> MakeCallProgram(Fixture& fixture, const std::string& functionName,
                                                          const std::size_t argumentCount)
     {
          ecpps::SBOVector<ecpps::ast::NodePointer> arguments{};
          for (std::size_t index = 0; index < argumentCount; index++) arguments.Push(fixture.MakeIntegerLiteral(index));
          auto call = MakeASTNode<ecpps::ast::CallOperatorNode>(
               fixture.astAllocator, fixture.MakeIdentifier(functionName), std::move(arguments), Source);
          ecpps::SBOVector<ecpps::ast::NodePointer> body{};
          body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(call), Source));
          auto signature = fixture.MakeFunctionSignature("int", "caller", {});
          std::vector<ecpps::ast::NodePointer> ast{};
          ast.push_back(fixture.MakeFunctionDefinition(std::move(signature), std::move(body)));
          return ast;
     }

     bool ContainsMessage(const ecpps::diagnostics::IDiagnosticsBase& diagnostic, const std::string_view message)
     {
          if (diagnostic.Message().find(message) != std::string::npos) return true;
          return std::ranges::any_of(diagnostic.SubDiagnostics(),
                                     [message](const ecpps::diagnostics::DiagnosticsMessage& child)
                                     {
                                          return child != nullptr && ContainsMessage(*child, message);
                                     });
     }
} // namespace

TEST_CASE("IR - Overload resolution prefers an exact match", "[unit][ir][overload-resolution]")
{
     Fixture fixture{};
     auto nodes = fixture.Parse(MakeOverloadProgram(fixture, "short", "int", "short"));

     const auto* procedure = FindProcedure(nodes, "caller");
     REQUIRE((procedure != nullptr));
     const auto* call = FindCall(*procedure);
     REQUIRE((call != nullptr));
     REQUIRE((call->Function().parameters.size() == 1));
     REQUIRE((call->Function().parameters.front().type->RawName() == "short"));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Overload resolution prefers promotion to conversion", "[unit][ir][overload-resolution]")
{
     Fixture fixture{};
     auto nodes = fixture.Parse(MakeOverloadProgram(fixture, "int", "long long", "short"));

     const auto* procedure = FindProcedure(nodes, "caller");
     REQUIRE((procedure != nullptr));
     const auto* call = FindCall(*procedure);
     REQUIRE((call != nullptr));
     REQUIRE((call->Function().parameters.size() == 1));
     REQUIRE((call->Function().parameters.front().type->RawName() == "int"));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());
}

TEST_CASE("IR - Overload resolution diagnoses equal-rank candidates", "[unit][ir][overload-resolution]")
{
     Fixture fixture{};
     auto nodes = fixture.Parse(MakeOverloadProgram(fixture, "short", "long long", "int"));

     const auto* procedure = FindProcedure(nodes, "caller");
     REQUIRE((procedure != nullptr));
     REQUIRE((FindCall(*procedure) == nullptr));
     REQUIRE(HasTypeErrorContaining(fixture.diagnostics, "ambiguous"));
     REQUIRE(ContainsMessage(*fixture.diagnostics.diagnosticsList.front(), "Call to 'pick' is ambiguous"));
}

TEST_CASE("IR - No matching overload is a type error with candidate failures", "[unit][ir][overload-resolution]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeFunctionDeclaration(fixture, "pick", {"int"}));
     auto callProgram = MakeCallProgram(fixture, "pick", 2);
     ast.insert(ast.end(), std::make_move_iterator(callProgram.begin()), std::make_move_iterator(callProgram.end()));

     (void)fixture.Parse(std::move(ast));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 1));
     const auto& diagnostic = *fixture.diagnostics.diagnosticsList.front();
     CHECK((diagnostic.Name() == "type-error"));
     CHECK(ContainsMessage(diagnostic, "No matching overload for call to 'pick'"));
     CHECK(ContainsMessage(diagnostic, "Expected 1 argument, got 2"));
}

TEST_CASE("IR - Unknown function suggestions rank argument matches", "[unit][ir][overload-resolution]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeFunctionDeclaration(fixture, "fob", {"int", "int"}));
     ast.push_back(MakeFunctionDeclaration(fixture, "fool", {"int"}));
     auto callProgram = MakeCallProgram(fixture, "fop", 1);
     ast.insert(ast.end(), std::make_move_iterator(callProgram.begin()), std::make_move_iterator(callProgram.end()));

     (void)fixture.Parse(std::move(ast));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 1));
     const auto& diagnostic = *fixture.diagnostics.diagnosticsList.front();
     CHECK((diagnostic.Name() == "unresolved-symbol-error"));
     CHECK(ContainsMessage(diagnostic, "Did you mean:"));
     CHECK(ContainsMessage(diagnostic, "* fool [distance=2; accepts these arguments]"));
     CHECK(ContainsMessage(diagnostic, "fob [distance=1; no overload accepts these arguments]"));
     REQUIRE((diagnostic.SubDiagnostics().front()->SubDiagnostics().size() == 2));
     CHECK(
          (diagnostic.SubDiagnostics().front()->SubDiagnostics().front()->Message().find("fool") != std::string::npos));
}

TEST_CASE("IR - No-match calls also suggest similar viable functions", "[unit][ir][overload-resolution]")
{
     Fixture fixture{};
     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(MakeFunctionDeclaration(fixture, "pick", {}));
     ast.push_back(MakeFunctionDeclaration(fixture, "pack", {"int"}));
     auto callProgram = MakeCallProgram(fixture, "pick", 1);
     ast.insert(ast.end(), std::make_move_iterator(callProgram.begin()), std::make_move_iterator(callProgram.end()));

     (void)fixture.Parse(std::move(ast));
     REQUIRE((fixture.diagnostics.diagnosticsList.size() == 1));
     const auto& diagnostic = *fixture.diagnostics.diagnosticsList.front();
     CHECK((diagnostic.Name() == "type-error"));
     CHECK(ContainsMessage(diagnostic, "No matching overload for call to 'pick'"));
     CHECK(ContainsMessage(diagnostic, "* pack [distance=1; accepts these arguments]"));
}

TEST_CASE("IR - Constructor overload diagnostic templates are shared", "[unit][ir][overload-resolution]")
{
     using namespace ecpps::ir::overloadDiagnostics;
     Candidate candidate{.signature = "Widget::Widget(int)", .source = Source};
     auto noMatch = NoMatchingConstructor("Widget", Source, {candidate});
     auto ambiguous = AmbiguousConstructor("Widget", Source, {candidate});
     auto inaccessible = SelectedConstructorFailure("Selected constructor is not accessible", Source, candidate);

     CHECK((noMatch->Message() == "No constructor of 'Widget' matches these arguments"));
     CHECK((ambiguous->Message() == "Constructor call for 'Widget' is ambiguous"));
     CHECK(ContainsMessage(*noMatch, "Candidate: Widget::Widget(int)"));
     CHECK(ContainsMessage(*ambiguous, "Candidate: Widget::Widget(int)"));
     CHECK(ContainsMessage(*inaccessible, "Candidate: Widget::Widget(int)"));
}

TEST_CASE("IR - Member overload diagnostics retain candidate detail trees", "[unit][ir][overload-resolution]")
{
     using namespace ecpps::ir::overloadDiagnostics;
     Candidate candidate{.signature = "Widget::pick(int)",
                         .source = Source,
                         .details = {CandidateDetail{.message = "Expected 1 argument, got 2", .source = Source}},
                         .access = "private",
                         .isDeleted = true};
     SimilarName suggestion{.name = "picks",
                            .source = Source,
                            .distance = 1,
                            .argumentFailureCount = 0,
                            .acceptsArguments = true,
                            .candidates = {candidate}};
     auto diagnostic = NoMatchingOverload("Widget::pick", Source, {candidate}, {suggestion}, SimilarNamesKind::Members);
     auto selected = SelectedOverloadFailure(candidate.signature, Source, true, true, candidate);

     CHECK((diagnostic->Message() == "No matching overload for call to 'Widget::pick'"));
     REQUIRE((diagnostic->SubDiagnostics().size() == 2));
     CHECK(ContainsMessage(*diagnostic, "Candidate: Widget::pick(int)"));
     CHECK(ContainsMessage(*diagnostic, "Similar members"));
     CHECK(ContainsMessage(*diagnostic, "* Similar member 'picks'"));
     CHECK((selected->Message() == "Selected overload 'Widget::pick(int)' is inaccessible and deleted"));
}
