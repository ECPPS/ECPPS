#include <atomic>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
namespace
{
     std::atomic<std::uint64_t> g_syntheticLabel{1ULL << 40};
} // namespace

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::ZeroMemory>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& operands)
{
     std::vector<ir::abstract::Instruction> out{};
     const auto target = operands[0];
     const std::size_t size = operands[1].index;
     const auto base = this->EnsureStackSlot(target);
     const auto limit = this->_target->platform->InlineInitialisationLimit();

     const auto widthOf = [](const std::size_t bytes)
     {
          return bytes == 8 ? Width::W64 : bytes == 4 ? Width::W32 : bytes == 2 ? Width::W16 : Width::W8;
     };
     const auto emitZeroes = [&](const std::size_t begin, const std::size_t end, const auto& at)
     {
          for (auto offset = begin; offset < end;)
          {
               const auto remaining = end - offset;
               const std::size_t chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
               out.push_back(BuildMov(widthOf(chunk), at(offset), IntegerOperand{0}));
               offset += chunk;
          }
     };

     if (size <= limit)
     {
          emitZeroes(0, size,
                     [&](const std::size_t offset) -> Operand
                     {
                          return StackOperand{.offset = base.offset + static_cast<std::uint32_t>(offset)};
                     });
          return out;
     }

     const ir::abstract::VirtualRegister pointerOwner{.index = std::numeric_limits<std::size_t>::max()};
     const ir::abstract::VirtualRegister counterOwner{.index = std::numeric_limits<std::size_t>::max() - 1};
     const auto pointer = this->_registerAllocator.Allocate(pointerOwner);
     const auto counter = this->_registerAllocator.Allocate(counterOwner);

     const std::size_t words = size / 8;
     const auto label = g_syntheticLabel.fetch_add(1);

     out.push_back(BuildLea(Width::W64, RegisterOperand{pointer}, base));
     out.push_back(BuildMov(Width::W64, RegisterOperand{counter}, IntegerOperand{words}));
     out.push_back(BuildLabel(label));
     out.push_back(BuildMov(Width::W64, MemoryOperand{.relativeTo = pointer, .offset = 0}, IntegerOperand{0}));
     out.push_back(BuildAdd(Width::W64, RegisterOperand{pointer}, IntegerOperand{8}));
     out.push_back(BuildSub(Width::W64, RegisterOperand{counter}, IntegerOperand{1}));
     out.push_back(BuildCmp(Width::W64, RegisterOperand{counter}, IntegerOperand{0}));
     out.push_back(BuildJcc(ir::abstract::ConditionCode::NotEqual, label));
     emitZeroes(words * 8, size,
                [&](const std::size_t offset) -> Operand
                {
                     return MemoryOperand{.relativeTo = pointer,
                                          .offset = static_cast<std::int32_t>(offset - (words * 8))};
                });

     this->_registerAllocator.Free(pointerOwner);
     this->_registerAllocator.Free(counterOwner);
     return out;
}

template <>
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::CopyFromData>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& operands)
{
     std::vector<ir::abstract::Instruction> out{};
     const auto target = operands[0];
     const std::size_t size = operands[3].index;
     const auto base = this->EnsureStackSlot(target);
     const auto limit = this->_target->platform->InlineInitialisationLimit();

     const auto stringAt = [&](const std::size_t extra) -> Operand
     {
          return StringAddressOperand{
               .tableIndex = static_cast<decltype(StringAddressOperand::tableIndex)>(operands[1].index),
               .offset = static_cast<decltype(StringAddressOperand::offset)>(operands[2].index + extra)};
     };
     const auto widthOf = [](const std::size_t bytes)
     {
          return bytes == 8 ? Width::W64 : bytes == 4 ? Width::W32 : bytes == 2 ? Width::W16 : Width::W8;
     };

     const ir::abstract::VirtualRegister sourceOwner{.index = std::numeric_limits<std::size_t>::max()};
     const ir::abstract::VirtualRegister temporaryOwner{.index = std::numeric_limits<std::size_t>::max() - 1};
     const auto source = this->_registerAllocator.Allocate(sourceOwner);
     const auto temporary = this->_registerAllocator.Allocate(temporaryOwner);

     const auto emitCopy =
          [&](const std::size_t begin, const std::size_t end, const std::size_t sourceBegin, const auto& at)
     {
          for (auto offset = begin; offset < end;)
          {
               const auto remaining = end - offset;
               const std::size_t chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
               const auto width = widthOf(chunk);
               out.push_back(BuildMov(
                    width, RegisterOperand{temporary},
                    MemoryOperand{.relativeTo = source, .offset = static_cast<std::int32_t>(offset - sourceBegin)}));
               out.push_back(BuildMov(width, at(offset), RegisterOperand{temporary}));
               offset += chunk;
          }
     };

     out.push_back(BuildLea(Width::W64, RegisterOperand{source}, stringAt(0)));

     if (size <= limit)
     {
          emitCopy(0, size, 0,
                   [&](const std::size_t offset) -> Operand
                   {
                        return StackOperand{.offset = base.offset + static_cast<std::uint32_t>(offset)};
                   });
     }
     else
     {
          const ir::abstract::VirtualRegister destinationOwner{.index = std::numeric_limits<std::size_t>::max() - 2};
          const ir::abstract::VirtualRegister counterOwner{.index = std::numeric_limits<std::size_t>::max() - 3};
          const auto destination = this->_registerAllocator.Allocate(destinationOwner);
          const auto counter = this->_registerAllocator.Allocate(counterOwner);

          const std::size_t words = size / 8;
          const auto label = g_syntheticLabel.fetch_add(1);

          out.push_back(BuildLea(Width::W64, RegisterOperand{destination}, base));
          out.push_back(BuildMov(Width::W64, RegisterOperand{counter}, IntegerOperand{words}));
          out.push_back(BuildLabel(label));
          out.push_back(
               BuildMov(Width::W64, RegisterOperand{temporary}, MemoryOperand{.relativeTo = source, .offset = 0}));
          out.push_back(
               BuildMov(Width::W64, MemoryOperand{.relativeTo = destination, .offset = 0}, RegisterOperand{temporary}));
          out.push_back(BuildAdd(Width::W64, RegisterOperand{source}, IntegerOperand{8}));
          out.push_back(BuildAdd(Width::W64, RegisterOperand{destination}, IntegerOperand{8}));
          out.push_back(BuildSub(Width::W64, RegisterOperand{counter}, IntegerOperand{1}));
          out.push_back(BuildCmp(Width::W64, RegisterOperand{counter}, IntegerOperand{0}));
          out.push_back(BuildJcc(ir::abstract::ConditionCode::NotEqual, label));
          emitCopy(words * 8, size, words * 8,
                   [&](const std::size_t offset) -> Operand
                   {
                        return MemoryOperand{.relativeTo = destination,
                                             .offset = static_cast<std::int32_t>(offset - (words * 8))};
                   });

          this->_registerAllocator.Free(destinationOwner);
          this->_registerAllocator.Free(counterOwner);
     }

     this->_registerAllocator.Free(sourceOwner);
     this->_registerAllocator.Free(temporaryOwner);
     return out;
}
