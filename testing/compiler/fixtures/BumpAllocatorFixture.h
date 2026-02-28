#pragma once

#include <Shared/BumpAllocator.h>
#include <catch_amalgamated.hpp>
#include <memory>

namespace TestHelpers
{

     /// RAII wrapper for BumpAllocator that ensures cleanup
     class BumpAllocatorFixture
     {
          std::unique_ptr<ecpps::BumpAllocator> allocator_;

     public:
          enum class Size
          {
               Small = 1024 * 4,   // 4 KB
               Medium = 1024 * 64, // 64 KB
               Large = 1024 * 1024 // 1 MB
          };

          explicit BumpAllocatorFixture(Size size = Size::Medium)
              : allocator_(std::make_unique<ecpps::BumpAllocator>(static_cast<std::size_t>(size)))
          {
          }

          explicit BumpAllocatorFixture(std::size_t customSize)
              : allocator_(std::make_unique<ecpps::BumpAllocator>(customSize))
          {
          }

          ~BumpAllocatorFixture() = default;

          // Non-copyable, movable
          BumpAllocatorFixture(const BumpAllocatorFixture&) = delete;
          BumpAllocatorFixture& operator=(const BumpAllocatorFixture&) = delete;
          BumpAllocatorFixture(BumpAllocatorFixture&&) = default;
          BumpAllocatorFixture& operator=(BumpAllocatorFixture&&) = default;

          ecpps::BumpAllocator& get() { return *allocator_; }
          const ecpps::BumpAllocator& get() const { return *allocator_; }

          ecpps::BumpAllocator& operator*() { return *allocator_; }
          const ecpps::BumpAllocator& operator*() const { return *allocator_; }

          ecpps::BumpAllocator* operator->() { return allocator_.get(); }
          const ecpps::BumpAllocator* operator->() const { return allocator_.get(); }
     };

} // namespace TestHelpers
