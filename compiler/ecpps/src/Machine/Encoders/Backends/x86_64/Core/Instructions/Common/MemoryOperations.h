#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "RuntimeAssert.h"

namespace ecpps::abi::encoders::x8664
{
     [[nodiscard]] inline std::size_t NextSyntheticLabel() noexcept
     {
          static std::atomic<std::size_t> counter{static_cast<std::size_t>(1ULL << 40)};
          return counter.fetch_add(1);
     }

     [[nodiscard]] inline ir::abstract::VirtualRegister NextScratchOwner() noexcept
     {
          static std::atomic<std::size_t> counter{0};
          return ir::abstract::VirtualRegister{.index = std::numeric_limits<std::size_t>::max() - counter.fetch_add(1)};
     }

     class ScratchRegister
     {
     public:
          explicit ScratchRegister(PhysicalRegisterAllocator& allocator)
              : _allocator(&allocator), _owner(NextScratchOwner()), _register(allocator.Allocate(_owner))
          {
          }

          [[nodiscard]] static ScratchRegister Take(PhysicalRegisterAllocator& allocator,
                                                    const ir::abstract::VirtualRegister from)
          {
               const auto colour = allocator.ColourOf(from);
               runtime_assert(colour.has_value(), "Cannot take over a register that holds no physical colour");

               const auto owner = NextScratchOwner();
               allocator.Reassign(from, owner);
               return ScratchRegister{allocator, owner, *colour};
          }

          ScratchRegister(const ScratchRegister&) = delete;
          ScratchRegister& operator=(const ScratchRegister&) = delete;
          ScratchRegister& operator=(ScratchRegister&&) = delete;
          ScratchRegister(ScratchRegister&& other) noexcept
              : _allocator(std::exchange(other._allocator, nullptr)), _owner(other._owner), _register(other._register)
          {
          }
          ~ScratchRegister()
          {
               if (this->_allocator != nullptr) this->_allocator->Free(this->_owner);
          }

          [[nodiscard]] RegisterIndex Register(void) const noexcept
          {
               return this->_register;
          }
          [[nodiscard]] RegisterOperand AsOperand(void) const noexcept
          {
               return RegisterOperand{this->_register};
          }

          void Commit(const ir::abstract::VirtualRegister newOwner)
          {
               runtime_assert(this->_allocator != nullptr, "Scratch register was already committed or moved from");
               this->_allocator->Reassign(this->_owner, newOwner);
               this->_allocator = nullptr;
          }

     private:
          ScratchRegister(PhysicalRegisterAllocator& allocator, const ir::abstract::VirtualRegister owner,
                          const RegisterIndex reg)
              : _allocator(&allocator), _owner(owner), _register(reg)
          {
          }

          PhysicalRegisterAllocator* _allocator;
          ir::abstract::VirtualRegister _owner;
          RegisterIndex _register;
     };

     struct MemoryChunk
     {
          std::size_t offset{};
          Width width{};
     };

     [[nodiscard]] constexpr Width ChunkWidth(const std::size_t bytes) noexcept
     {
          return bytes >= 8 ? Width::W64 : bytes >= 4 ? Width::W32 : bytes >= 2 ? Width::W16 : Width::W8;
     }

     [[nodiscard]] inline std::vector<MemoryChunk> PlanTail(const std::size_t begin, const std::size_t size)
     {
          runtime_assert(begin <= size, "Tail begins after the end of the block");

          std::vector<MemoryChunk> chunks{};
          auto offset = begin;
          while (size - offset >= 8)
          {
               chunks.push_back({.offset = offset, .width = Width::W64});
               offset += 8;
          }

          const auto remaining = size - offset;
          if (remaining == 0) return chunks;

          if (size >= 8)
          {
               chunks.push_back({.offset = size - 8, .width = Width::W64});
               return chunks;
          }

          runtime_assert(begin == 0, "Blocks smaller than 8 bytes must be planned from offset 0");
          const auto chunk = std::bit_floor(remaining);
          chunks.push_back({.offset = 0, .width = ChunkWidth(chunk)});
          if (chunk != remaining) chunks.push_back({.offset = remaining - chunk, .width = ChunkWidth(chunk)});
          return chunks;
     }

     enum struct BlockStrategy : std::uint8_t
     {
          UnrolledMoves,
          GprLoop,
          RepMovs,   // TODO: Implement at some point
          SimdChunks // TODO: Implement at some point
     };

     [[nodiscard]] constexpr bool IsStrategyAvailable(const BlockStrategy strategy) noexcept
     {
          // TODO: Decide by the optimiser
          switch (strategy)
          {
          case BlockStrategy::UnrolledMoves:
          case BlockStrategy::GprLoop: return true;
          case BlockStrategy::RepMovs:
          case BlockStrategy::SimdChunks: return false;
          }
          std::unreachable();
     }

     [[nodiscard]] constexpr BlockStrategy SelectBlockStrategy(const std::size_t size,
                                                               const std::size_t inlineLimit) noexcept
     {
          constexpr std::size_t gprLoopMinimum = 16; // loop body is 16 bytes; below this, unrolling is 2 moves anyway
          constexpr std::size_t simdMinimum = 64;
          constexpr std::size_t repMinimum = 256;

          if (size < gprLoopMinimum || size <= inlineLimit) return BlockStrategy::UnrolledMoves;
          if (IsStrategyAvailable(BlockStrategy::SimdChunks) && size >= simdMinimum) return BlockStrategy::SimdChunks;
          if (IsStrategyAvailable(BlockStrategy::RepMovs) && size >= repMinimum) return BlockStrategy::RepMovs;
          return BlockStrategy::GprLoop;
     }
} // namespace ecpps::abi::encoders::x8664
