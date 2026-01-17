#pragma once
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace ecpps::platformlib
{
     struct NativeException : std::runtime_error
     {
          explicit NativeException(const std::string& message) : runtime_error(message) {}
     };
     template <typename TFrom, typename TTo> struct PointerInterconvertibility
     {
          constexpr static bool IsValid = false;
     };
     struct DebuggerContext
     {
          explicit DebuggerContext(const DebuggerContext&) = delete;
          explicit DebuggerContext(const DebuggerContext&&) = delete;
          DebuggerContext& operator=(const DebuggerContext&) = delete;
          DebuggerContext& operator=(const DebuggerContext&&) = delete;
          ~DebuggerContext(void) = default;

          template <typename T>
               requires(std::is_base_of_v<DebuggerContext, T> ||
                        PointerInterconvertibility<DebuggerContext, T>::IsValid)
          T& As(void) & noexcept
          {
               return reinterpret_cast<T&>(*this);
          }

          template <typename T>
               requires(std::is_base_of_v<DebuggerContext, T> ||
                        PointerInterconvertibility<DebuggerContext, T>::IsValid)
          const T& As(void) const& noexcept
          {
               return reinterpret_cast<const T&>(*this);
          }

          template <typename T>
               requires(std::is_base_of_v<DebuggerContext, T> ||
                        PointerInterconvertibility<DebuggerContext, T>::IsValid)
          T&& As(void) && noexcept
          {
               return reinterpret_cast<T&&>(*this);
          }

          template <typename T>
               requires(std::is_base_of_v<DebuggerContext, T> ||
                        PointerInterconvertibility<DebuggerContext, T>::IsValid)
          static DebuggerContext& From(T& other)
          {
               return reinterpret_cast<DebuggerContext&>(other);
          }

          explicit DebuggerContext(void);
     };
     namespace debugger
     {
          DebuggerContext& New(void);
          void Delete(DebuggerContext* context);
          std::size_t GetRegisterValue(DebuggerContext& context, const std::string& registerName);
          std::vector<void*> WalkTrace(DebuggerContext* defaultContext);
     } // namespace debugger

     inline ecpps::platformlib::DebuggerContext::DebuggerContext(void) { debugger::Delete(this); }
} // namespace ecpps::platformlib
