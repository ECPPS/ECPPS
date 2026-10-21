#include "MemoryOperations.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "RuntimeAssert.h"
#include "Shared/Diagnostics.h"

void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::EmitMemoryCopy(
     std::vector<ir::abstract::Instruction>& out, const MemoryLocation& destination, const MemoryLocation& source,
     const std::size_t size)
{
     this->EmitMemoryBlock(out, destination, &source, size);
}

void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::EmitMemoryZero(
     std::vector<ir::abstract::Instruction>& out, const MemoryLocation& destination, const std::size_t size)
{
     this->EmitMemoryBlock(out, destination, nullptr, size);
}

void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::EmitMemoryBlock(
     std::vector<ir::abstract::Instruction>& out, const MemoryLocation& destination, const MemoryLocation* source,
     const std::size_t size)
{
     if (size == 0) return;

     const bool copying = source != nullptr;
     const auto strategy = SelectBlockStrategy(size, this->_target->platform->InlineInitialisationLimit());

     std::optional<ScratchRegister> temporary{};
     if (copying) temporary.emplace(this->_registerAllocator);

     const auto emit = [&](const Width width, const MemoryLocation& to, const std::int64_t toOffset,
                           const MemoryLocation* from, const std::int64_t fromOffset)
     {
          if (from == nullptr)
          {
               out.push_back(BuildMov(width, to.At(toOffset), IntegerOperand{0}));
               return;
          }
          out.push_back(BuildMov(width, temporary->AsOperand(), from->At(fromOffset)));
          out.push_back(BuildMov(width, to.At(toOffset), temporary->AsOperand()));
     };

     if (strategy == BlockStrategy::UnrolledMoves)
     {
          for (const auto& chunk : PlanTail(0, size))
               emit(chunk.width, destination, static_cast<std::int64_t>(chunk.offset), source,
                    static_cast<std::int64_t>(chunk.offset));
          return;
     }

     if (strategy != BlockStrategy::GprLoop)
          throw TracedException("Selected a block strategy that is not implemented yet");

     constexpr std::size_t bodyBytes = 16;
     const std::size_t iterations = size / bodyBytes;
     runtime_assert(iterations != 0, "Loop strategy selected for a block smaller than one loop body");
     const std::size_t loopBytes = iterations * bodyBytes;

     struct Cursor
     {
          RegisterIndex reg;
          std::optional<ScratchRegister> owned;
     };

     const auto makeCursor = [&](const MemoryLocation& location) -> Cursor
     {
          if (location.disposable)
          {
               const auto* memory = std::get_if<MemoryOperand>(&location.base);
               if (memory != nullptr && memory->offset == 0)
                    return Cursor{.reg = memory->relativeTo, .owned = std::nullopt};
          }

          ScratchRegister pointer{this->_registerAllocator};
          out.push_back(BuildLea(Width::W64, pointer.AsOperand(), location.base));
          const auto reg = pointer.Register();
          return Cursor{.reg = reg, .owned = std::move(pointer)};
     };

     Cursor to = makeCursor(destination);
     std::optional<Cursor> from{};
     if (copying) from.emplace(makeCursor(*source));

     ScratchRegister counter{this->_registerAllocator};
     const auto label = NextSyntheticLabel();

     out.push_back(BuildMov(Width::W64, counter.AsOperand(), IntegerOperand{static_cast<std::uint64_t>(iterations)}));
     out.push_back(BuildLabel(label));

     const MemoryLocation toBase{.base = MemoryOperand{.relativeTo = to.reg, .offset = 0}};
     const MemoryLocation fromBase{.base = MemoryOperand{.relativeTo = copying ? from->reg : to.reg, .offset = 0}};
     const MemoryLocation* fromLocation = copying ? &fromBase : nullptr;

     for (std::size_t offset = 0; offset < bodyBytes; offset += 8)
          emit(Width::W64, toBase, static_cast<std::int64_t>(offset), fromLocation, static_cast<std::int64_t>(offset));

     if (copying) out.push_back(BuildAdd(Width::W64, RegisterOperand{from->reg}, IntegerOperand{bodyBytes}));
     out.push_back(BuildAdd(Width::W64, RegisterOperand{to.reg}, IntegerOperand{bodyBytes}));
     out.push_back(BuildSub(Width::W64, counter.AsOperand(), IntegerOperand{1}));
     out.push_back(BuildJcc(ir::abstract::ConditionCode::NotEqual, label));

     for (const auto& chunk : PlanTail(loopBytes, size))
     {
          const auto relative = static_cast<std::int64_t>(chunk.offset) - static_cast<std::int64_t>(loopBytes);
          emit(chunk.width, toBase, relative, fromLocation, relative);
     }
}
