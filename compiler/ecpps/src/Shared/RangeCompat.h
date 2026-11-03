#pragma once

#include <ranges>
#include <utility>

namespace ecpps::detail
{
     template <std::ranges::input_range Range> void AppendRange(auto& container, Range&& range)
     {
#if defined(__GNUC__) && __GNUC__ < 15
          container.insert(container.end(), std::ranges::begin(range), std::ranges::end(range));
#else
          container.append_range(std::forward<Range>(range));
#endif
     }
} // namespace ecpps::detail
