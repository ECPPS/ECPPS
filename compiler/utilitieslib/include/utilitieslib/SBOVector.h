#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
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
               TElement* _begin{};
               std::size_t _capacity{};
          };

          /// <summary>
          /// In elements, not bytes
          /// </summary>
          static constexpr std::size_t SBOSize = Align(TNSBOSize / sizeof(TElement), sizeof(TElement));

          union BufferUnion
          {
               std::byte sbo[sizeof(TElement) * SBOSize];
               NoSBO noSbo;

               explicit BufferUnion(void) {}
               ~BufferUnion(void) {}
          };

     public:
          explicit SBOVector(void) { new (this->_buffer.sbo) std::byte[sizeof(TElement) * SBOSize]; }

          SBOVector(std::size_t count, const TElement& value = TElement{}) : _size(count)
          {
               const bool sbo = UseSBO();
               if (sbo)
               {
                    auto* destination = std::launder(reinterpret_cast<TElement(&)[SBOSize]>(this->_buffer.sbo));
                    std::uninitialized_fill_n(destination, count, value);
               }
               else
               {
                    const std::size_t cap = Align(count, SBOSize);
                    this->_buffer.noSbo._capacity = cap;
                    TAllocator allocator{};
                    this->_buffer.noSbo._begin = allocator.allocate(cap);
                    std::uninitialized_fill_n(this->_buffer.noSbo._begin, count, value);
               }
          }

          SBOVector(const SBOVector& other) : _size(other._size)
          {
               const bool sbo = other.UseSBO();
               if (sbo)
               {
                    std::memcpy(_buffer.sbo, other._buffer.sbo, sizeof(TElement) * _size);
                    auto* dst = reinterpret_cast<TElement*>(_buffer.sbo);
                    const auto* src = other.begin();
                    for (std::size_t i = 0; i < _size; ++i) std::construct_at(dst + i, src[i]);
               }
               else
               {
                    const std::size_t cap = other._buffer.noSbo._capacity;
                    _buffer.noSbo._capacity = cap;
                    TAllocator allocator{};
                    _buffer.noSbo._begin = allocator.allocate(cap);
                    std::uninitialized_copy_n(other.begin(), _size, _buffer.noSbo._begin);
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
                    allocator.deallocate(_buffer.noSbo._begin, _buffer.noSbo._capacity);
               }
          }

          TElement* begin(void)
          {
               if (UseSBO()) return std::launder(reinterpret_cast<TElement(&)[SBOSize]>(this->_buffer.sbo));
               return this->_buffer.noSbo._begin;
          }

          const TElement* begin(void) const
          {
               if (UseSBO()) return std::launder(reinterpret_cast<const TElement(&)[SBOSize]>(this->_buffer.sbo));
               return this->_buffer.noSbo._begin;
          }

          TElement* end(void)
          {
               return UseSBO() ? std::launder(this->_size + reinterpret_cast<TElement(&)[SBOSize]>(this->_buffer.sbo))
                               : (this->begin() + this->_size);
          }

          const TElement* end(void) const
          {
               return UseSBO()
                          ? std::launder(this->_size + reinterpret_cast<const TElement(&)[SBOSize]>(this->_buffer.sbo))
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
                         TElement* newBuf = allocator.allocate(cap);
                         std::memcpy(newBuf, _buffer.sbo, SBOSize * sizeof(TElement));
                         _buffer.noSbo._begin = newBuf;
                         _buffer.noSbo._capacity = cap;

                         return *std::construct_at(newBuf + SBOSize, std::forward<TArgs>(args)...);
                    }

                    if (_buffer.noSbo._capacity < _size)
                    {
                         const std::size_t oldCap = _buffer.noSbo._capacity;
                         const std::size_t newCap = oldCap * 2;
                         TElement* newBuf = allocator.allocate(newCap);
                         std::memcpy(newBuf, _buffer.noSbo._begin, (_size - 1) * sizeof(TElement));
                         allocator.deallocate(std::exchange(_buffer.noSbo._begin, newBuf), oldCap);
                         _buffer.noSbo._capacity = newCap;
                    }

                    return *std::construct_at(_buffer.noSbo._begin + (_size - 1), std::forward<TArgs>(args)...);
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
                         _buffer.noSbo._begin = newBuf;
                         _buffer.noSbo._capacity = cap;

                         return *std::construct_at(newBuf + SBOSize, value);
                    }

                    if (this->_buffer.noSbo._capacity < this->_size)
                    {
                         const std::size_t oldCap = this->_buffer.noSbo._capacity;
                         const std::size_t newCap = oldCap * 2;
                         TElement* newBuf = allocator.allocate(newCap);
                         std::memcpy(newBuf, this->_buffer.noSbo._begin, index * sizeof(TElement));
                         allocator.deallocate(std::exchange(_buffer.noSbo._begin, newBuf), oldCap);
                         this->_buffer.noSbo._capacity = newCap;
                    }

                    return *std::construct_at(_buffer.noSbo._begin + index, value);
               }
               return *std::construct_at(std::launder(reinterpret_cast<TElement(&)[SBOSize]>(this->_buffer.sbo)) + index, value);
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
                         this->_buffer.noSbo._begin = newBuf;
                         this->_buffer.noSbo._capacity = cap;

                         return *std::construct_at(newBuf + SBOSize, std::move(value));
                    }

                    if (this->_buffer.noSbo._capacity < _size)
                    {
                         const std::size_t oldCap = this->_buffer.noSbo._capacity;
                         const std::size_t newCap = oldCap * 2;
                         TElement* newBuf = allocator.allocate(newCap);
                         std::memcpy(newBuf, this->_buffer.noSbo._begin, index * sizeof(TElement));
                         allocator.deallocate(std::exchange(this->_buffer.noSbo._begin, newBuf), oldCap);
                         this->_buffer.noSbo._capacity = newCap;
                    }

                    return *std::construct_at(_buffer.noSbo._begin + index, std::move(value));
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