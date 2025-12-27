#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace ecpps
{
     static constexpr std::size_t Align(const std::size_t number, const std::size_t alignment)
     {
          return (number + alignment - 1) & ~(alignment - 1);
     }
     template <typename TElement, typename TAllocator = std::allocator<TElement>,
               std::size_t TNSBOSize = std::hardware_destructive_interference_size>
     class SBOVector
     {
          struct NoSBO
          {
               TElement* begin{};
               std::size_t capacity{};
          };

          /// <summary>
          /// In elements, not bytes
          /// </summary>
          static constexpr std::size_t SBOSize =
              std::max<std::size_t>(1, Align(TNSBOSize / sizeof(TElement), sizeof(TElement)));

          union BufferUnion
          {
               std::byte sbo[sizeof(TElement) * SBOSize]; // NOLINT(cppcoreguidelines-avoid-c-arrays)
               NoSBO noSbo;

               explicit BufferUnion(void) {} // NOLINT(cppcoreguidelines-pro-type-member-init)
               ~BufferUnion(void) {}
          };

     public:
          explicit SBOVector(void) { new (this->_buffer.sbo) std::byte[sizeof(TElement) * SBOSize]; }

          SBOVector(std::size_t count, const TElement& value = TElement{}) : _size(count)
          {
               const bool sbo = UseSBO();
               if (sbo)
               {
                    auto* destination =
                        std::launder(reinterpret_cast<TElement(&)[SBOSize]>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                             // modernize-avoid-c-arrays)
                            this->_buffer.sbo));
                    std::uninitialized_fill_n(destination, count, value);
               }
               else
               {
                    const std::size_t cap = Align(count, SBOSize);
                    this->_buffer.noSbo.capacity = cap;
                    TAllocator allocator{};
                    this->_buffer.noSbo.begin = allocator.allocate(cap);
                    std::uninitialized_fill_n(this->_buffer.noSbo.begin, count, value);
               }
          }

          SBOVector(const SBOVector& other) : _size(other._size)
          {
               const bool sbo = other.UseSBO();
               if (sbo)
               {
                    if constexpr (std::is_trivially_copyable_v<TElement>)
                         std::memcpy(_buffer.sbo, other._buffer.sbo, sizeof(TElement) * _size);
                    else
                    {
                         auto* dst = reinterpret_cast<TElement*>(_buffer.sbo);
                         const auto* src = other.begin();
                         for (std::size_t i = 0; i < _size; ++i) std::construct_at(dst + i, src[i]);
                    }
               }
               else
               {
                    const std::size_t cap = other._buffer.noSbo.capacity;
                    _buffer.noSbo.capacity = cap;
                    TAllocator allocator{};
                    _buffer.noSbo.begin = allocator.allocate(cap);
                    std::uninitialized_copy_n(other.begin(), _size, _buffer.noSbo.begin);
               }
          }

          SBOVector& operator=(const SBOVector& other)
          {
               if (this == &other) return *this;

               this->~SBOVector();
               new (this) SBOVector(other);
               return *this;
          }
          SBOVector(SBOVector&& other) noexcept : _size(std::exchange(other._size, 0))
          {
               if (other.UseSBO()) { std::memcpy(_buffer.sbo, other._buffer.sbo, sizeof(TElement) * this->_size); }
               else
                    this->_buffer.noSbo = std::exchange(other._buffer.noSbo, NoSBO{});
          }
          SBOVector& operator=(SBOVector&& other) noexcept
          {
               if (this == &other) return *this;

               this->~SBOVector();
               new (this) SBOVector(std::move(other));
               return *this;
          }
          ~SBOVector(void)
          {
               const auto* first = begin();
               for (std::size_t i = 0; i < _size; ++i) std::destroy_at(const_cast<TElement*>(first + i));
               if (!UseSBO())
               {
                    TAllocator allocator{};
                    allocator.deallocate(_buffer.noSbo.begin, _buffer.noSbo.capacity);
               }
          }

          TElement* begin(void) // NOLINT(readability-identifier-naming)
          {
               if (UseSBO())
                    return std::launder(
                        reinterpret_cast<TElement(&)[SBOSize]>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                // modernize-avoid-c-arrays)
                            this->_buffer.sbo)); // NOLINT(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
               return this->_buffer.noSbo.begin;
          }

          const TElement* begin(void) const // NOLINT(readability-identifier-naming)
          {
               if (UseSBO())
                    return std::launder(
                        reinterpret_cast<const TElement(&)[SBOSize]>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                      // modernize-avoid-c-arrays)
                            this->_buffer.sbo)); // NOLINT(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
               return this->_buffer.noSbo.begin;
          }

          TElement* end(void) // NOLINT(readability-identifier-naming)
          {
               return UseSBO() ? std::launder(
                                     this->_size +
                                     reinterpret_cast<TElement(&)[SBOSize]>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                             // modernize-avoid-c-arrays)
                                         this->_buffer.sbo))
                               : (this->begin() + this->_size);
          }

          const TElement* end(void) const // NOLINT(readability-identifier-naming)
          {
               return UseSBO()
                          ? std::launder(this->_size +
                                         reinterpret_cast<
                                             const TElement(&)[SBOSize]>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                          // modernize-avoid-c-arrays)
                                             this->_buffer.sbo))
                          : (this->begin() + this->_size);
          }
          template <typename... TArgs> TElement& EmplaceBack(TArgs&&... args)
          {
               const bool wasSBO = UseSBO();
               this->_size++;

               if (!UseSBO())
               {
                    TAllocator allocator{};
                    if (wasSBO)
                    {
                         const std::size_t cap = SBOSize * 2;
                         TElement* newBuffer = allocator.allocate(cap);
                         for (std::size_t i = 0; i < SBOSize; ++i)
                         {
                              new (newBuffer + i)
                                  TElement(std::move(reinterpret_cast<TElement*>(this->_buffer.sbo)[i]));
                              reinterpret_cast<TElement*>(this->_buffer.sbo)[i].~TElement();
                         }
                         _buffer.noSbo.begin = newBuffer;
                         _buffer.noSbo.capacity = cap;

                         return *std::construct_at(newBuffer + SBOSize, std::forward<TArgs>(args)...);
                    }

                    if (_buffer.noSbo.capacity < _size)
                    {
                         const std::size_t oldCap = _buffer.noSbo.capacity;
                         const std::size_t newCap = oldCap * 2;
                         TElement* newBuf = allocator.allocate(newCap);

                         if constexpr (std::is_trivially_copyable_v<TElement>)
                         {
                              std::memcpy(newBuf, _buffer.noSbo.begin, (_size - 1) * sizeof(TElement));
                         }
                         else
                         {
                              for (std::size_t i = 0; i < (_size - 1); ++i)
                              {
                                   new (newBuf + i) TElement(std::move(_buffer.noSbo.begin[i]));
                                   _buffer.noSbo.begin[i].~TElement();
                              }
                         }
                         allocator.deallocate(std::exchange(_buffer.noSbo.begin, newBuf), oldCap);
                         _buffer.noSbo.capacity = newCap;
                    }

                    return *std::construct_at(_buffer.noSbo.begin + (_size - 1), std::forward<TArgs>(args)...);
               }

               return *std::construct_at(reinterpret_cast<TElement*>(_buffer.sbo) + (_size - 1),
                                         std::forward<TArgs>(args)...);
          }
          TElement& Push(const TElement& value)
          {
               const bool wasSBO = UseSBO();
               const std::size_t index = this->_size++;
               if (!UseSBO())
               {
                    TAllocator allocator{};
                    if (wasSBO)
                    {
                         const std::size_t cap = SBOSize * 2;
                         TElement* newBuf = allocator.allocate(cap);
                         std::memcpy(newBuf, _buffer.sbo, SBOSize * sizeof(TElement));
                         _buffer.noSbo.begin = newBuf;
                         _buffer.noSbo.capacity = cap;

                         return *std::construct_at(newBuf + SBOSize, value);
                    }

                    if (this->_buffer.noSbo.capacity < this->_size)
                    {
                         const std::size_t oldCap = this->_buffer.noSbo.capacity;
                         const std::size_t newCap = oldCap * 2;
                         TElement* newBuf = allocator.allocate(newCap);
                         std::uninitialized_move(this->_buffer.noSbo.begin, this->_buffer.noSbo.begin + index, newBuf);
                         for (std::size_t i = 0; i < index; ++i) { std::destroy_at(this->_buffer.noSbo.begin + i); }
                         allocator.deallocate(std::exchange(_buffer.noSbo.begin, newBuf), oldCap);
                         this->_buffer.noSbo.capacity = newCap;
                    }

                    return *std::construct_at(_buffer.noSbo.begin + index, value);
               }
               return *std::construct_at(
                   std::launder(reinterpret_cast<TElement(&)[SBOSize]>( // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                                        // modernize-avoid-c-arrays)
                       this->_buffer.sbo)) +
                       index,
                   value);
          }

          TElement& Push(TElement&& value)
          {
               const bool wasSBO = UseSBO();
               const std::size_t index = this->_size++;
               if (!UseSBO())
               {
                    TAllocator allocator{};
                    if (wasSBO)
                    {
                         const std::size_t cap = SBOSize * 2;
                         TElement* newBuf = allocator.allocate(cap);
                         std::memcpy(newBuf, this->_buffer.sbo, SBOSize * sizeof(TElement));
                         this->_buffer.noSbo.begin = newBuf;
                         this->_buffer.noSbo.capacity = cap;

                         return *std::construct_at(newBuf + SBOSize, std::move(value));
                    }

                    if (this->_buffer.noSbo.capacity < _size)
                    {
                         const std::size_t oldCap = this->_buffer.noSbo.capacity;
                         const std::size_t newCap = oldCap * 2;
                         TElement* newBuf = allocator.allocate(newCap);
                         std::uninitialized_move(this->_buffer.noSbo.begin, this->_buffer.noSbo.begin + index, newBuf);
                         for (std::size_t i = 0; i < index; ++i)
                         {
                              this->_buffer.noSbo.begin[i].~TElement();
                              allocator.deallocate(std::exchange(this->_buffer.noSbo.begin, newBuf), oldCap);
                         }
                         this->_buffer.noSbo.capacity = newCap;
                    }

                    return *std::construct_at(_buffer.noSbo.begin + index, std::move(value));
               }
               return *std::construct_at(reinterpret_cast<TElement*>(_buffer.sbo) + index, std::move(value));
          }

          constexpr std::size_t Size(void) const noexcept { return this->_size; }
          constexpr bool UseSBO(void) const noexcept { return this->_size <= SBOSize; }

     private:
          BufferUnion _buffer;
          std::size_t _size{};
     };
} // namespace ecpps
