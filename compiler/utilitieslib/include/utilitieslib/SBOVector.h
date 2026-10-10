#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
#define ECPPS_SBO_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define ECPPS_SBO_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

constexpr auto SboTargetBytes = 64uz;

namespace ecpps
{
     static constexpr std::size_t Align(const std::size_t number, const std::size_t alignment)
     {
          return (number + alignment - 1) & ~(alignment - 1);
     }

     namespace sboDetail
     {
          [[noreturn]] inline void Fail(const char* what)
          {
               throw std::length_error(what);
          }

          template <typename T>
          constexpr bool TriviallyRelocatable =
               std::is_trivially_move_constructible_v<T> && std::is_trivially_destructible_v<T>;

          template <typename T>
          constexpr bool TriviallyCopyConstructible =
               std::is_trivially_copy_constructible_v<T> && std::is_trivially_destructible_v<T>;

          template <typename T, typename... TArgs> T* ConstructAt(T* location, TArgs&&... args)
          {
               return ::new (static_cast<void*>(location)) T(std::forward<TArgs>(args)...);
          }

          template <typename T> void DestroyN(T* first, std::size_t count) noexcept
          {
               if constexpr (!std::is_trivially_destructible_v<T>)
               {
                    while (count-- > 0) first[count].~T();
               }
          }

          template <typename T> struct RangeGuard
          {
               T* first;
               std::size_t count;
               ~RangeGuard()
               {
                    DestroyN(first, count);
               }
          };

          template <typename TAllocator, typename T> struct HeapGuard
          {
               TAllocator& allocator; // NOLINT lgtm
               T* pointer;
               std::size_t capacity;
               ~HeapGuard(void)
               {
                    if (pointer) allocator.deallocate(pointer, capacity);
               }
               void Release(void) noexcept
               {
                    pointer = nullptr;
               }
          };

          template <typename T> void UninitialisedCopyN(const T* from, T* to, std::size_t count)
          {
               if constexpr (TriviallyCopyConstructible<T>)
               {
                    if (count) std::memcpy(static_cast<void*>(to), static_cast<const void*>(from), count * sizeof(T));
               }
               else
               {
                    RangeGuard<T> guard{to, 0};
                    for (; guard.count < count; guard.count++) ConstructAt(to + guard.count, from[guard.count]);
                    guard.count = 0;
               }
          }

          template <typename T> void UninitialisedFillN(T* to, std::size_t count, const T& value)
          {
               RangeGuard<T> guard{to, 0};
               for (; guard.count < count; guard.count++) ConstructAt(to + guard.count, value);
               guard.count = 0;
          }

          template <typename T> void UninitialisedValueN(T* to, std::size_t count)
          {
               RangeGuard<T> guard{to, 0};
               for (; guard.count < count; guard.count++) ConstructAt(to + guard.count);
               guard.count = 0;
          }

          template <bool TIfNoexcept, typename T>
          void RelocateN(T* from, T* to,
                         std::size_t count) noexcept(TriviallyRelocatable<T> || std::is_nothrow_move_constructible_v<T>)
          {
               if constexpr (TriviallyRelocatable<T>)
               {
                    if (count) std::memcpy(static_cast<void*>(to), static_cast<const void*>(from), count * sizeof(T));
               }
               else
               {
                    RangeGuard<T> guard{to, 0};
                    for (; guard.count < count; guard.count++)
                    {
                         if constexpr (TIfNoexcept)
                              ConstructAt(to + guard.count, std::move_if_noexcept(from[guard.count]));
                         else
                              ConstructAt(to + guard.count, std::move(from[guard.count]));
                    }
                    guard.count = 0;
                    DestroyN(from, count);
               }
          }

          template <typename T, typename TSize> constexpr std::size_t DefaultInlineCapacity(void) noexcept
          {
               constexpr std::size_t overhead = 2 * sizeof(TSize);
               constexpr std::size_t budget = SboTargetBytes > overhead ? SboTargetBytes - overhead : 0;
               constexpr std::size_t count = budget / sizeof(T);
               return count > 0 ? count : 1;
          }
     } // namespace sboDetail

     template <typename T>
     inline constexpr std::size_t SBODefaultInlineCapacity = sboDetail::DefaultInlineCapacity<T, std::uint32_t>();

     template <typename TElement, typename TAllocator = std::allocator<TElement>,
               std::size_t TInlineCapacity = SBODefaultInlineCapacity<TElement>, typename TSize = std::uint32_t>
     class SBOVector
     {
          static_assert(std::is_object_v<TElement> && !std::is_const_v<TElement> && !std::is_volatile_v<TElement> &&
                        !std::is_array_v<TElement>);
          static_assert(std::is_unsigned_v<TSize> && sizeof(TSize) <= sizeof(std::size_t));
          static_assert(TInlineCapacity >= 1, "inline capacity is in elements and must be at least 1");

     public:
          using value_type = TElement;
          using size_type = std::size_t;
          using allocator_type = TAllocator;
          using iterator = TElement*;
          using const_iterator = const TElement*;

          static constexpr std::size_t InlineCapacity = TInlineCapacity;
          static constexpr std::size_t MaxSize = static_cast<std::size_t>(std::numeric_limits<TSize>::max()) <
                                                           std::numeric_limits<std::size_t>::max() / sizeof(TElement)
                                                      ? static_cast<std::size_t>(std::numeric_limits<TSize>::max())
                                                      : std::numeric_limits<std::size_t>::max() / sizeof(TElement);

          static_assert(TInlineCapacity < MaxSize, "inline capacity must be smaller than the maximum size");

     private:
          using Heap = sboDetail::HeapGuard<TAllocator, TElement>;

          union Storage
          {
               alignas(TElement) std::byte inlineStorage[sizeof(TElement) * TInlineCapacity]{}; // NOLINT
               TElement* heap;

               Storage(void) noexcept
               {
               }
          };

     public:
          explicit SBOVector(void) noexcept(std::is_nothrow_default_constructible_v<TAllocator>) = default;
          explicit SBOVector(const TAllocator& allocator) noexcept : _allocator(allocator)
          {
          }

          explicit SBOVector(std::size_t count)
          {
               InitWith(count,
                        [&](TElement* to)
                        {
                             sboDetail::UninitialisedValueN(to, count);
                        });
          }

          SBOVector(std::size_t count, const TElement& value)
          {
               InitWith(count,
                        [&](TElement* to)
                        {
                             sboDetail::UninitialisedFillN(to, count, value);
                        });
          }

          SBOVector(const SBOVector& other) : _allocator(other._allocator)
          {
               static_assert(std::is_copy_constructible_v<TElement>);
               InitWith(other._size,
                        [&](TElement* to)
                        {
                             sboDetail::UninitialisedCopyN(other.Data(), to, other._size);
                        });
          }

          SBOVector(SBOVector&& other) noexcept(std::is_nothrow_move_constructible_v<TElement>)
              : _allocator(std::move(other._allocator))
          {
               StealFrom(other);
          }

          SBOVector& operator=(const SBOVector& other)
          {
               static_assert(std::is_copy_constructible_v<TElement>);
               if (this == &other) return *this;

               const std::size_t count = other._size;
               if (count <= this->_capacity)
               {
                    Clear();
                    sboDetail::UninitialisedCopyN(other.Data(), Data(), count);
                    this->_size = static_cast<TSize>(count);
               }
               else
               {
                    Heap block{this->_allocator, this->_allocator.allocate(count), count};
                    sboDetail::UninitialisedCopyN(other.Data(), block.pointer, count);
                    ReleaseAll();
                    this->_storage.heap = block.pointer;
                    this->_capacity = static_cast<TSize>(count);
                    this->_size = static_cast<TSize>(count);
                    block.Release();
               }
               return *this;
          }

          SBOVector& operator=(SBOVector&& other) noexcept(std::is_nothrow_move_constructible_v<TElement>)
          {
               if (this == &other) return *this;
               ReleaseAll();
               StealFrom(other);
               return *this;
          }

          ~SBOVector(void)
          {
               ReleaseAll();
          }

          [[nodiscard]] TElement* Data(void) noexcept
          {
               return IsInline() ? InlinePtr() : this->_storage.heap;
          }
          [[nodiscard]] const TElement* Data(void) const noexcept
          {
               return IsInline() ? InlinePtr() : this->_storage.heap;
          }

          [[nodiscard]] TElement* begin(void) noexcept // NOLINT(readability-identifier-naming)
          {
               return Data();
          }
          [[nodiscard]] const TElement* begin(void) const noexcept // NOLINT(readability-identifier-naming)
          {
               return Data();
          }
          [[nodiscard]] TElement* end(void) noexcept // NOLINT(readability-identifier-naming)
          {
               return Data() + this->_size;
          }
          [[nodiscard]] const TElement* end(void) const noexcept // NOLINT(readability-identifier-naming)
          {
               return Data() + this->_size;
          }

          [[nodiscard]] TElement& operator[](std::size_t index) noexcept
          {
               return Data()[index];
          }
          [[nodiscard]] const TElement& operator[](std::size_t index) const noexcept
          {
               return Data()[index];
          }
          [[nodiscard]] TElement& Front(void) noexcept
          {
               return *Data();
          }
          [[nodiscard]] const TElement& Front(void) const noexcept
          {
               return *Data();
          }
          [[nodiscard]] TElement& Back(void) noexcept
          {
               return Data()[this->_size - 1];
          }
          [[nodiscard]] const TElement& Back(void) const noexcept
          {
               return Data()[this->_size - 1];
          }

          [[nodiscard]] constexpr std::size_t Size(void) const noexcept
          {
               return this->_size;
          }
          [[nodiscard]] constexpr std::size_t Capacity(void) const noexcept
          {
               return this->_capacity;
          }
          [[nodiscard]] constexpr bool Empty(void) const noexcept
          {
               return this->_size == 0;
          }
          [[nodiscard]] constexpr bool UseSBO(void) const noexcept
          {
               return IsInline();
          }

          void Reserve(std::size_t capacity)
          {
               if (capacity <= this->_capacity) return;
               if (capacity > MaxSize) sboDetail::Fail("ecpps::SBOVector: capacity exceeds maximum size");
               GrowTo(capacity);
          }

          template <typename... TArgs> TElement& EmplaceBack(TArgs&&... args)
          {
               if (this->_size == this->_capacity) [[unlikely]]
                    return EmplaceBackSlow(std::forward<TArgs>(args)...);

               TElement* slot = Data() + this->_size;
               sboDetail::ConstructAt(slot, std::forward<TArgs>(args)...);
               this->_size++;
               return *slot;
          }

          TElement& Push(const TElement& value)
          {
               return EmplaceBack(value);
          }

          TElement& Push(TElement&& value)
          {
               return EmplaceBack(std::move(value));
          }

          void PopBack(void) noexcept
          {
               Data()[--_size].~TElement();
          }

          void Clear(void) noexcept
          {
               sboDetail::DestroyN(Data(), this->_size);
               this->_size = 0;
          }

     private:
          [[nodiscard]] constexpr bool IsInline(void) const noexcept
          {
               return this->_capacity == TInlineCapacity;
          }

          [[nodiscard]] TElement* InlinePtr(void) noexcept
          {
               return reinterpret_cast<TElement*>(this->_storage.inlineStorage);
          }
          [[nodiscard]] const TElement* InlinePtr(void) const noexcept
          {
               return reinterpret_cast<const TElement*>(this->_storage.inlineStorage);
          }

          void InitWith(std::size_t count, auto&& fill)
          {
               if (count > MaxSize) sboDetail::Fail("ecpps::SBOVector: size exceeds maximum size");

               if (count > TInlineCapacity)
               {
                    Heap block{_allocator, this->_allocator.allocate(count), count};
                    fill(block.pointer);
                    this->_storage.heap = block.pointer;
                    this->_capacity = static_cast<TSize>(count);
                    block.Release();
               }
               else
                    fill(InlinePtr());

               this->_size = static_cast<TSize>(count);
          }

          void ReleaseAll(void) noexcept
          {
               sboDetail::DestroyN(Data(), this->_size);
               if (!IsInline()) this->_allocator.deallocate(this->_storage.heap, this->_capacity);
               this->_size = 0;
               this->_capacity = static_cast<TSize>(TInlineCapacity);
          }

          void StealFrom(SBOVector& other) noexcept(std::is_nothrow_move_constructible_v<TElement>)
          {
               if (other.IsInline())
               {
                    sboDetail::RelocateN<false>(other.InlinePtr(), InlinePtr(), other._size);
                    this->_size = other._size;
                    other._size = 0;
               }
               else
               {
                    this->_storage.heap = other._storage.heap;
                    this->_size = other._size;
                    this->_capacity = other._capacity;
                    other._size = 0;
                    other._capacity = static_cast<TSize>(TInlineCapacity);
               }
          }

          [[nodiscard]] std::size_t NextCapacity(std::size_t required) const
          {
               if (required > MaxSize) sboDetail::Fail("ecpps::SBOVector: size exceeds maximum size");
               const std::size_t cap = this->_capacity;
               const std::size_t doubled = cap > MaxSize - cap ? MaxSize : cap * 2;
               return doubled > required ? doubled : required;
          }

          void GrowTo(std::size_t newCapacity)
          {
               Heap block{this->_allocator, this->_allocator.allocate(newCapacity), newCapacity};
               sboDetail::RelocateN<true>(Data(), block.pointer, this->_size);
               if (!IsInline()) this->_allocator.deallocate(this->_storage.heap, this->_capacity);
               this->_storage.heap = block.pointer;
               this->_capacity = static_cast<TSize>(newCapacity);
               block.Release();
          }

          template <typename... TArgs> TElement& EmplaceBackSlow(TArgs&&... args)
          {
               const std::size_t newCapacity = NextCapacity(std::size_t{_size} + 1);
               Heap block{_allocator, this->_allocator.allocate(newCapacity), newCapacity};

               TElement* slot = block.pointer + this->_size;
               sboDetail::ConstructAt(slot, std::forward<TArgs>(args)...);
               sboDetail::RangeGuard<TElement> slotGuard{slot, 1};

               sboDetail::RelocateN<true>(Data(), block.pointer, this->_size);
               slotGuard.count = 0;

               if (!IsInline()) this->_allocator.deallocate(this->_storage.heap, this->_capacity);
               this->_storage.heap = block.pointer;
               this->_capacity = static_cast<TSize>(newCapacity);
               this->_size++;
               block.Release();
               return *slot;
          }

          Storage _storage;
          TSize _size{0};
          TSize _capacity{static_cast<TSize>(TInlineCapacity)};
          ECPPS_SBO_NO_UNIQUE_ADDRESS TAllocator _allocator{};
     };
} // namespace ecpps
