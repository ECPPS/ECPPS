#pragma once

namespace ecpps
{
     class BumpAllocator
     {
     public:
          explicit BumpAllocator(std::size_t maxMemory = 0);

          struct Deleter
          {
               static void operator()(void*) noexcept {}
          };
          std::byte* Allocate(std::size_t size) noexcept;
          ~BumpAllocator(void);
          void Release(void);

     private:
          std::byte* _begin;
          std::byte* _currentEnd;
          std::byte* _capacity;
          std::byte* _commitEnd;
     };
} // namespace ecpps

inline void* operator new(const std::size_t count,
                          ecpps::BumpAllocator& allocator) noexcept(noexcept(allocator.Allocate(count)))
{
     return allocator.Allocate(count);
}

inline void operator delete([[maybe_unused]] void* address, [[maybe_unused]] ecpps::BumpAllocator& allocator) noexcept
{
}
