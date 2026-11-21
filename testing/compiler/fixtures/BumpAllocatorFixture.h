#pragma once

#include <Shared/BumpAllocator.h>
#include <catch_amalgamated.hpp>
#include <memory>

namespace testHelpers
{

     /// RAII wrapper for BumpAllocator that ensures cleanup
     class BumpAllocatorFixture
     {
          std::unique_ptr<ecpps::BumpAllocator> _allocator;

     public:
          enum struct Size
          {
               Small = 1024 * 4,   // 4 KB
               Medium = 1024 * 64, // 64 KB
               Large = 1024 * 1024, // 1 MB
          };

          explicit BumpAllocatorFixture(Size size = Size::Medium)
              : _allocator(std::make_unique<ecpps::BumpAllocator>(static_cast<std::size_t>(size)))
          {
          }

          explicit BumpAllocatorFixture(std::size_t customSize)
              : _allocator(std::make_unique<ecpps::BumpAllocator>(customSize))
          {
          }

          ~BumpAllocatorFixture() = default;

          // Non-copyable, movable
          BumpAllocatorFixture(const BumpAllocatorFixture&) = delete;
          BumpAllocatorFixture& operator=(const BumpAllocatorFixture&) = delete;
          BumpAllocatorFixture(BumpAllocatorFixture&&) = default;
          BumpAllocatorFixture& operator=(BumpAllocatorFixture&&) = default;

          ecpps::BumpAllocator& Get()
          {
               return *_allocator;
          }
          [[nodiscard]] const ecpps::BumpAllocator& Get() const
          {
               return *_allocator;
          }

          ecpps::BumpAllocator& operator*()
          {
               return *_allocator;
          }
          const ecpps::BumpAllocator& operator*() const
          {
               return *_allocator;
          }

          ecpps::BumpAllocator* operator->()
          {
               return _allocator.get();
          }
          const ecpps::BumpAllocator* operator->() const
          {
               return _allocator.get();
          }
     };

} // namespace TestHelpers
