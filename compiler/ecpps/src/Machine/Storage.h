#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace ecpps::abi
{
     // TODO: Rename to *Width
     constexpr std::size_t zmmSize = 512;
     constexpr std::size_t ymmSize = 256;
     constexpr std::size_t xmmSize = 128;
     constexpr std::size_t qwordSize = 64;
     constexpr std::size_t dwordSize = 32;
     constexpr std::size_t wordSize = 16;
     constexpr std::size_t byteSize = 8;
} // namespace ecpps::abi
