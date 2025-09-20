#pragma once
#include <cstddef>
#include <iterator>
#include <memory>
#include <new>
#include <utility>

namespace ecpps
{
     template <typename T, typename TAllocator = std::allocator<T>,
               std::size_t TNSBOSize = std::hardware_destructive_interference_size>
     class SBOQueue
     {
          static constexpr std::size_t SBOSize = (TNSBOSize / sizeof(T) + sizeof(T) - 1) & ~(sizeof(T) - 1);
          static_assert(SBOSize > 0);
          union BufferUnion
          {
               alignas(T) std::byte sbo[sizeof(T) * SBOSize];
               struct
               {
                    T* begin{};
                    std::size_t capacity{};
               } noSbo;

               explicit BufferUnion(void) noexcept {}
               ~BufferUnion(void) noexcept {}
          };

     public:
          SBOQueue(void) noexcept { new (_buffer.sbo) std::byte[sizeof(T) * SBOSize]; }

          ~SBOQueue(void)
          {
               for (std::size_t i = 0; i < this->_size; i++) std::destroy_at(&this->operator[](i));

               if (!UseSBO())
               {
                    TAllocator alloc{};
                    alloc.deallocate(this->_buffer.noSbo.begin, this->_buffer.noSbo.capacity);
               }
          }

          void Push(const T& value) { Emplace(value); }
          void Push(T&& value) { Emplace(std::move(value)); }

          void Pop(void)
          {
               if (this->_size == 0) return;
               std::destroy_at(FrontPtr());
               this->_head = (this->_head + 1) % Capacity();
               this->_size--;
          }

          T& Front(void) { return *FrontPtr(); }
          const T& Front(void) const { return *FrontPtr(); }

          T& Back(void) { return *BackPtr(); }
          const T& Back(void) const { return *BackPtr(); }

          std::size_t Size(void) const noexcept { return this->_size; }
          bool Empty(void) const noexcept { return this->_size == 0; }

          class iterator
          {
          public:
               using difference_type = std::ptrdiff_t;
               using value_type = T;
               using reference = T&;
               using pointer = T*;
               using iterator_category = std::bidirectional_iterator_tag;

               iterator(SBOQueue* queue, const std::size_t index) : _queue(queue), _index(index) {}

               reference operator*(void) const { return (*this->_queue)[this->_index]; }
               pointer operator->(void) const { return &(*this->_queue)[this->_index]; }

               iterator& operator++(void)
               {
                    if (this->_index == 0) this->_index = this->_queue->Capacity() - 1;
                    else
                         this->_index--;
                    return *this;
               }

               iterator operator++(int)
               {
                    auto tmp = *this;
                    ++*this;
                    return tmp;
               }

               iterator& operator--(void)
               {
                    this->_index = (this->_index + 1) % this->_queue->Capacity();
                    return *this;
               }

               iterator operator--(int)
               {
                    auto tmp = *this;
                    --*this;
                    return tmp;
               }

               friend bool operator==(const iterator& a, const iterator& b)
               {
                    return a._queue == b._queue && a._index == b._index;
               }
               friend bool operator!=(const iterator& a, const iterator& b) { return !(a == b); }

          private:
               SBOQueue* _queue;
               std::size_t _index;
          };

          iterator begin(void)
          {
               if (this->_size == 0) return end();
               return iterator(this, this->_tail == 0 ? Capacity() - 1 : this->_tail - 1);
          }
          iterator end(void) { return iterator(this, this->_head == 0 ? Capacity() - 1 : this->_head - 1); }

          SBOQueue(const SBOQueue& other)
          {
               new (_buffer.sbo) std::byte[sizeof(T) * SBOSize];
               if (other._size == 0) return;

               if (other.UseSBO())
               {
                    for (std::size_t i = 0; i < other._size; ++i)
                         new (StoragePtr() + i) T(other[i]);
               }
               else
               {
                    TAllocator alloc{};
                    _buffer.noSbo.begin = alloc.allocate(other._buffer.noSbo.capacity);
                    _buffer.noSbo.capacity = other._buffer.noSbo.capacity;

                    for (std::size_t i = 0; i < other._size; ++i)
                         new (_buffer.noSbo.begin + i) T(other[i]);
               }

               _size = other._size;
               _head = other._head;
               _tail = other._tail;
          }
          SBOQueue(SBOQueue&& other) noexcept
          {
               new (_buffer.sbo) std::byte[sizeof(T) * SBOSize];

               if (other.UseSBO())
               {
                    for (std::size_t i = 0; i < other._size; ++i)
                         new (StoragePtr() + i) T(std::move(other[i]));
               }
               else
               {
                    _buffer.noSbo.begin = std::exchange(other._buffer.noSbo.begin, nullptr);
                    _buffer.noSbo.capacity = std::exchange(other._buffer.noSbo.capacity, 0);
               }

               _size = std::exchange(other._size, 0);
               _head = std::exchange(other._head, 0);
               _tail = std::exchange(other._tail, 0);
          }
          SBOQueue& operator=(const SBOQueue& other)
          {
               if (this == &other) return *this;

               this->~SBOQueue();
               new (this) SBOQueue(other);
               return *this;
          }

          SBOQueue& operator=(SBOQueue&& other) noexcept
          {
               if (this == &other) return *this;

               this->~SBOQueue();
               new (this) SBOQueue(std::move(other));
               return *this;
          }

     private:
          void Emplace(const T& value) { EmplaceImpl(value); }
          void Emplace(T&& value) { EmplaceImpl(std::move(value)); }

          template <typename U> void EmplaceImpl(U&& value)
          {
               if (_size == Capacity()) Grow();

               T* ptr = StoragePtr() + this->_tail;
               new (ptr) T(std::forward<U>(value));
               this->_tail = (this->_tail + 1) % Capacity();
               this->_size++;
          }

          void Grow(void)
          {
               const std::size_t oldCapacity = Capacity();
               const std::size_t newCapacity = oldCapacity * 2;

               TAllocator alloc{};
               T* newBuffer = alloc.allocate(newCapacity);

               for (std::size_t i = 0; i < _size; ++i)
               {
                    std::size_t index = (_head + i) % oldCapacity;
                    new (newBuffer + i) T(std::move((*this)[index]));
                    std::destroy_at(StoragePtr() + index);
               }

               if (!UseSBO()) alloc.deallocate(this->_buffer.noSbo.begin, this->_buffer.noSbo.capacity);

               this->_buffer.noSbo.begin = newBuffer;
               this->_buffer.noSbo.capacity = newCapacity;

               this->_head = 0;
               this->_tail = _size;
          }

          T* StoragePtr(void) noexcept
          {
               return UseSBO() ? std::launder(reinterpret_cast<T(&)[TNSBOSize / sizeof(T)]>(this->_buffer.sbo))
                               : this->_buffer.noSbo.begin;
          }

          const T* StoragePtr() const noexcept
          {
               return UseSBO() ? std::launder(reinterpret_cast<const T(&)[TNSBOSize / sizeof(T)]>(this->_buffer.sbo))
                               : _buffer.noSbo.begin;
          }

          T& operator[](std::size_t index) { return *(StoragePtr() + index); }
          const T& operator[](std::size_t index) const { return *(StoragePtr() + index); }

          T* FrontPtr(void) noexcept { return StoragePtr() + this->_head; }
          const T* FrontPtr(void) const noexcept { return StoragePtr() + this->_head; }

          T* BackPtr(void) noexcept { return StoragePtr() + (this->_tail == 0 ? Capacity() - 1 : this->_tail - 1); }
          const T* BackPtr(void) const noexcept
          {
               return StoragePtr() + (this->_tail == 0 ? Capacity() - 1 : this->_tail - 1);
          }

          constexpr std::size_t Capacity(void) const noexcept
          {
               return UseSBO() ? SBOSize : this->_buffer.noSbo.capacity;
          }

          bool UseSBO(void) const noexcept { return this->_size <= SBOSize; }

     private:
          BufferUnion _buffer;
          std::size_t _size{};
          std::size_t _head{};
          std::size_t _tail{};
     };
} // namespace ecpps
