#define CATCH_CONFIG_MAIN
#include <IRTestHelpers.h>
#include <catch_amalgamated.hpp>

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
}
