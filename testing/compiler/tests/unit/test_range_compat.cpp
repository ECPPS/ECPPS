#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>
#include <memory>
#include <ranges>
#include <utility>
#include <vector>
#include "Execution/NodeBase.h"
#include "Shared/RangeCompat.h"

TEST_CASE("AppendRange appends lvalue and temporary ranges")
{
     std::vector<int> values{1};
     const std::vector<int> source{2, 3};

     ecpps::detail::AppendRange(values, source);
     ecpps::detail::AppendRange(values, std::vector<int>{4, 5});

     const std::vector<int> expected{1, 2, 3, 4, 5};
     REQUIRE(values == expected);
}

TEST_CASE("AppendRange moves elements from an rvalue view")
{
     std::vector<std::unique_ptr<int>> values;
     std::vector<std::unique_ptr<int>> source;
     source.emplace_back(std::make_unique<int>(42));
     auto sourceRvalues = source | std::views::as_rvalue;

     ecpps::detail::AppendRange(values, sourceRvalues);

     REQUIRE(values.size() == 1);
     REQUIRE(*values.front() == 42);
     REQUIRE(source.front() == nullptr);
}

TEST_CASE("IntegerArrayNode formats range values")
{
     ecpps::ir::IntegerArrayNode node{std::vector<std::uint32_t>{1, 2, 3}, nullptr, ecpps::Location{}};

     REQUIRE(node.ToString(0) == "[1, 2, 3]");
}
