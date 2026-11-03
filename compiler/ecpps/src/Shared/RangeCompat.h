#pragma once

#include <ranges>
#include <utility>
#include <vector>
#include <version>

namespace ecpps::detail
{
     template <std::ranges::input_range Range> void AppendRange(auto& container, Range&& range)
     {
#if !defined(__cpp_lib_containers_ranges) || __cpp_lib_containers_ranges < 202202L
          container.insert(container.end(), std::ranges::begin(range), std::ranges::end(range));
#else
          container.append_range(std::forward<Range>(range));
#endif
     }

     template <std::ranges::input_range Range> [[nodiscard]] auto MaterialiseVector(Range&& range)
     {
#if !defined(__cpp_lib_ranges_to_container) || __cpp_lib_ranges_to_container < 202202L ||                              \
     (defined(__clang__) && defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE < 15)
          using Value = std::ranges::range_value_t<Range>;
          std::vector<Value> values;
          if constexpr (std::ranges::sized_range<Range>) values.reserve(std::ranges::size(range));
          for (auto&& value : range) values.emplace_back(std::forward<decltype(value)>(value));
          return values;
#else
          return std::ranges::to<std::vector>(std::forward<Range>(range));
#endif
     }

     struct ToVectorAdaptor
     {
          template <std::ranges::input_range Range> friend auto operator|(Range&& range, ToVectorAdaptor)
          {
               return MaterialiseVector(std::forward<Range>(range));
          }
     };

     inline constexpr ToVectorAdaptor ToVector{};
} // namespace ecpps::detail
