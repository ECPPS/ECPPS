#pragma once

#include <Shared/BumpAllocator.h>
#include <cstring>
#include <format>
#include <new>
#include <string>
#include <string_view>
#include <unordered_set>

namespace ecpps
{
     class StringPool;

     class InternedString
     {
     public:
          InternedString(void) = default;

          [[nodiscard]] StringPool& Pool(void) const noexcept;

          [[nodiscard]] std::string_view View(void) const noexcept
          {
               return this->_view;
          }
          [[nodiscard]] std::string Copy(void) const noexcept
          {
               return std::string{this->_view};
          }

          [[nodiscard]] std::size_t size(void) const noexcept
          {
               return this->_view.size();
          }

          [[nodiscard]] std::size_t length(void) const noexcept
          {
               return this->_view.length();
          }

          [[nodiscard]] bool empty(void) const noexcept
          {
               return this->_view.empty();
          }

          [[nodiscard]] const char* data(void) const noexcept
          {
               return this->_view.data();
          }

          [[nodiscard]] std::string_view::const_iterator begin(void) const noexcept
          {
               return this->_view.begin();
          }

          [[nodiscard]] std::string_view::const_iterator end(void) const noexcept
          {
               return this->_view.end();
          }

          [[nodiscard]] char operator[](const std::size_t index) const noexcept
          {
               return this->_view[index];
          }

          [[nodiscard]] operator std::string_view(void) const noexcept
          {
               return this->View();
          }

          friend bool operator==(const InternedString& lhs, const InternedString& rhs) noexcept
          {
               if (lhs._pool == rhs._pool && lhs._view.data() == rhs._view.data() &&
                   lhs._view.size() == rhs._view.size())
                    return true;
               return lhs._view == rhs._view;
          }

          friend bool operator==(const InternedString& lhs, const std::string_view rhs) noexcept
          {
               return lhs._view == rhs;
          }

          friend bool operator==(const std::string_view lhs, const InternedString& rhs) noexcept
          {
               return lhs == rhs._view;
          }

     private:
          StringPool* _pool{};
          std::string_view _view{};

          InternedString(StringPool* pool, const std::string_view view) noexcept : _pool(pool), _view(view)
          {
          }

          friend class StringPool;
     };

     class StringPool : public BumpAllocator
     {
     public:
          using BumpAllocator::BumpAllocator;

          [[nodiscard]] InternedString Intern(const std::string_view text)
          {
               if (const auto found = this->_strings.find(text); found != this->_strings.end())
                    return InternedString{this, *found};

               auto* const storage = reinterpret_cast<char*>(this->Allocate(text.size() + 1));
               if (storage == nullptr) throw std::bad_alloc{};

               if (!text.empty()) std::memcpy(storage, text.data(), text.size());
               storage[text.size()] = '\0';

               const std::string_view stored{storage, text.size()};
               const auto [inserted, unused] = this->_strings.emplace(stored);
               static_cast<void>(unused);
               return InternedString{this, *inserted};
          }

     private:
          std::unordered_set<std::string_view> _strings{};
     };

     inline StringPool& InternedString::Pool(void) const noexcept
     {
          return *this->_pool;
     }
} // namespace ecpps

template <> struct std::formatter<ecpps::InternedString> : std::formatter<std::string_view>
{
     template <typename TFormatContext> auto format(const ecpps::InternedString& value, TFormatContext& context) const
     {
          return std::formatter<std::string_view>::format(value.View(), context);
     }
};
