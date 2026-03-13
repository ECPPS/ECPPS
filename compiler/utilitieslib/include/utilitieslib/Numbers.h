#pragma once

#include <concepts>

namespace ecpps
{
     template <std::integral TUnderlying> struct Number
     {
          TUnderlying value;

          [[nodiscard]] TUnderlying& Value(void) noexcept { return this->value; }
          [[nodiscard]] TUnderlying Value(void) const noexcept { return this->value; }

          [[nodiscard]] friend bool operator==(const Number& lhs, const Number& rhs) noexcept
          {
               return lhs.Value() == rhs.Value();
          }

          explicit(false) constexpr Number(const TUnderlying value) : value(value) {}
     };

#define define_number(name, underlying)                                                                                \
     struct name : ecpps::Number<underlying>                                                                           \
     {                                                                                                                 \
          using Number::Number;                                                                                        \
     };                                                                                                                \
     [[nodiscard]] consteval name operator""_##name(const unsigned long long value) { return name{value}; }            \
     template <> struct std::hash<name>                                                                                \
     {                                                                                                                 \
          [[nodiscard]] static underlying operator()(const name number) noexcept                                       \
          {                                                                                                            \
               return std::hash<underlying>{}(number.Value());                                                         \
          }                                                                                                            \
     };
} // namespace ecpps
