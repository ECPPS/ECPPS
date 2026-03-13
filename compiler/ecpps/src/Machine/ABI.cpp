#include "ABI.h"
#include <RuntimeAssert.h>
#include <cstdint>
#include <format>
#include "Machine.h"
#include "Machine/Storage.h"
#ifndef NDEBUG
#endif
#include <stdexcept>
#include <vector>
#include "../CodeGeneration/Nodes.h"
#include "../Shared/Diagnostics.h"
#include "../TypeSystem/ArithmeticTypes.h"
#include "Mangling.h"
#include "Vendor/Shared/ISA.h"

using ecpps::abi::ABI;

#ifdef __clang__
__attribute__((no_sanitize("address")))
#endif
ABI ABI::_current{Platform::CurrentISA<Platform::CurrentVendor()>()};

ecpps::abi::ABI::ABI(ISA isa) : _isa(isa)
{
     this->_callingConventions.emplace(std::make_unique<MicrosoftX64CallingConvention>());
     auto& specialStringRegister =
         this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("__string", qwordSize, SIZE_MAX));
     this->_specialStringRegister = std::make_shared<VirtualRegister>("__string", specialStringRegister, 0, 0);

     switch (isa)
     {
     case ISA::x86_64:
     {
          this->_endianness = Endianness::Little;
          this->_pointerSize = 8;
          std::size_t id{};

          const auto& rax =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rax", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("rax", rax, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("eax", rax, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("ax", rax, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("al", rax, byteSize, 0));

          const auto& rcx =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rcx", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("rcx", rcx, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("ecx", rcx, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("cx", rcx, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("cl", rcx, byteSize, 0));

          const auto& rdx =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rdx", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("rdx", rdx, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("edx", rdx, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("dx", rdx, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("dl", rdx, byteSize, 0));

          const auto& rbx =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rbx", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("rbx", rbx, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("ebx", rbx, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("bx", rbx, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("bl", rbx, byteSize, 0));

          const auto& rsp =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rsp", qwordSize, id++));
          this->_registers.push_back(this->_stackPointerRegister =
                                         std::make_shared<VirtualRegister>("rsp", rsp, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("esp", rsp, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("sp", rsp, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("spl", rsp, byteSize, 0));

          const auto& rbp =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rbp", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("rbp", rbp, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("ebp", rbp, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("bp", rbp, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("bpl", rbp, byteSize, 0));

          const auto& rsi =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rsi", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("rsi", rsi, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("esi", rsi, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("si", rsi, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("sil", rsi, byteSize, 0));

          const auto& rdi =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rdi", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("rdi", rdi, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("edi", rdi, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("di", rdi, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("dil", rdi, byteSize, 0));

          const auto& r8 =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r8", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r8", r8, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r8d", r8, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r8w", r8, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r8b", r8, byteSize, 0));

          const auto& r9 =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r9", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r9", r9, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r9d", r9, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r9w", r9, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r9b", r9, byteSize, 0));

          const auto& r10 =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r10", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r10", r10, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r10d", r10, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r10w", r10, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r10b", r10, byteSize, 0));

          const auto& r11 =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r11", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r11", r11, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r11d", r11, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r11w", r11, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r11b", r11, byteSize, 0));

          const auto& r12 =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r12", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r12", r12, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r12d", r12, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r12w", r12, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r12b", r12, byteSize, 0));

          const auto& r13 =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r13", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r13", r13, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r13d", r13, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r13w", r13, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r13b", r13, byteSize, 0));

          const auto& r14 =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r14", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r14", r14, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r14d", r14, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r14w", r14, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r14b", r14, byteSize, 0));

          const auto& r15 =
              this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r15", qwordSize, id++));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r15", r15, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r15d", r15, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r15w", r15, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("r15b", r15, byteSize, 0));
     }
     break;
     default: throw TracedException("ISA not handled yet");
     }
}

void ecpps::abi::ABI::PushSIMDRegisters(const SimdFeatures simd)
{
     switch (this->_isa)
     {
     case ISA::x86_64:
     {
          std::size_t id = this->_physicalRegisters.empty() ? 0 : this->_physicalRegisters.back()->id + 1;

          if (simd & (SimdFeatures::AVX512F | SimdFeatures::AVX512BW | SimdFeatures::AVX512DQ | SimdFeatures::AVX512VL))
          {
               for (std::size_t i = 0; i < 16; i++)
               {
                    const auto& physicalReg = this->_physicalRegisters.emplace_back(
                        std::make_shared<PhysicalRegister>(std::string("xmm") + std::to_string(i), zmmSize, id++));
                    this->_registers.push_back(std::make_shared<VirtualRegister>(std::string("zmm") + std::to_string(i),
                                                                                 physicalReg, zmmSize, 0));
                    this->_registers.push_back(std::make_shared<VirtualRegister>(std::string("ymm") + std::to_string(i),
                                                                                 physicalReg, ymmSize, 0));
                    this->_registers.push_back(std::make_shared<VirtualRegister>(std::string("xmm") + std::to_string(i),
                                                                                 physicalReg, xmmSize, 0));
               }
          }
          else if (simd & (SimdFeatures::AVX | SimdFeatures::AVX2))
          {
               for (std::size_t i = 0; i < 16; i++)
               {
                    const auto& physicalReg = this->_physicalRegisters.emplace_back(
                        std::make_shared<PhysicalRegister>(std::string("xmm") + std::to_string(i), ymmSize, id++));
                    this->_registers.push_back(std::make_shared<VirtualRegister>(std::string("ymm") + std::to_string(i),
                                                                                 physicalReg, ymmSize, 0));
                    this->_registers.push_back(std::make_shared<VirtualRegister>(std::string("xmm") + std::to_string(i),
                                                                                 physicalReg, xmmSize, 0));
               }
          }
          else if (simd & (SimdFeatures::SSE | SimdFeatures::SSE2 | SimdFeatures::SSE3 | SimdFeatures::SSSE3 |
                           SimdFeatures::SSE4_1 | SimdFeatures::SSE4_2))
          {
               for (std::size_t i = 0; i < 16; i++)
               {
                    auto registerName = std::string("xmm") + std::to_string(i);
                    const auto& physicalReg = this->_physicalRegisters.emplace_back(
                        std::make_shared<PhysicalRegister>(registerName, xmmSize, id++));
                    this->_registers.push_back(
                        std::make_shared<VirtualRegister>(registerName, physicalReg, xmmSize, 0));
               }
          }
     }
     break;
     default: throw TracedException("ISA not implemented yet");
     }
}

ABI& ecpps::abi::ABI::Current(void) { return ABI::_current; }

template <std::size_t TTo, std::size_t TFrom>
std::size_t ecpps::abi::ABI::ConvertEndian(std::size_t value) const noexcept
{
     if constexpr (TFrom == 1) { return value & ((std::size_t{1} << CHAR_BIT) - 1); }

     std::size_t result = 0;

     switch (this->_endianness)
     {
     case ecpps::abi::Endianness::Big:
          for (std::size_t i = 0; i < TFrom && i < TTo; i++)
          {
               const std::size_t byte = (value >> (i * 8)) & 0xFF;
               result |= byte << ((TTo - 1 - i) * 8);
          }
          break;
     case ecpps::abi::Endianness::Little:
     {
          for (std::size_t i = 0; i < TFrom && i < TTo; i++)
          {
               const std::size_t byte = (value >> (i * 8)) & 0xFF;
               result |= byte << (i * 8);
          }
     }
     break;
     }

     return result;
}

ecpps::abi::AllocatedRegister ecpps::abi::ABI::AllocateRegister(const std::size_t width)
{
     for (const auto& reg : this->_registers)
     {
          if (reg->width != width || this->_allocatedRegisters.try_emplace(reg->physical->id, 0).first->second > 0)
               continue;
          return AllocatedRegister{reg};
     }

     return AllocatedRegister{nullptr};
}

ecpps::abi::AllocatedRegister ecpps::abi::ABI::AllocateRegister(const std::size_t width, const std::string& name,
                                                                const RegisterAllocation allocation)
{
     for (const auto& reg : this->_registers)
     {
          if (reg->width != width || (reg->friendlyName != name && reg->physical->friendlyName != name)) continue;
          if (allocation == RegisterAllocation::Normal &&
              this->_allocatedRegisters.try_emplace(reg->physical->id, 0).first->second > 0)
               continue;
          return AllocatedRegister{reg};
     }

     return AllocatedRegister{nullptr};
}

ecpps::abi::AllocatedRegister ecpps::abi::ABI::AllocateRegister(std::size_t width,
                                                                const std::shared_ptr<PhysicalRegister>& toAllocate,
                                                                RegisterAllocation allocation)
{
     for (const auto& reg : this->_registers)
     {
          if (reg->width != width || reg->physical != toAllocate) continue;
          if (allocation == RegisterAllocation::Normal &&
              this->_allocatedRegisters.try_emplace(reg->physical->id, 0).first->second > 0)
               continue;
          return AllocatedRegister{reg};
     }

     return AllocatedRegister{nullptr};
}

ecpps::abi::AllocatedRegister ecpps::abi::ABI::AllocateRegister(const std::shared_ptr<VirtualRegister>& toAllocate,
                                                                RegisterAllocation allocation)
{
     if (allocation == RegisterAllocation::Normal &&
         this->_allocatedRegisters.try_emplace(toAllocate->physical->id, 0).first->second > 0)
          return AllocatedRegister{nullptr};

     return AllocatedRegister{toAllocate};
}

bool ecpps::abi::ABI::CheckRegisterAllocation(std::size_t width, const std::string& name) const
{
     for (const auto& reg : this->_registers)
     {
          if (reg->width != width || reg->physical->friendlyName != name) continue;
          if (!this->_allocatedRegisters.contains(reg->physical->id) ||
              this->_allocatedRegisters.at(reg->physical->id) == 0)
               return false;

          return true;
     }

     return false;
}

std::string ecpps::abi::ABI::MangleName(Linkage linkage, const std::string& name,
                                        const CallingConventionName callingConvention,
                                        typeSystem::NonowningTypePointer returnType,
                                        const std::vector<typeSystem::NonowningTypePointer>& parameters)
{
     if (name == "main") return "main";

     return Mangling::MangleName(linkage, name, callingConvention, returnType, parameters);
}

ecpps::abi::CallingConventionName ecpps::abi::ABI::DefaultCallingConventionName(void) const
{
     switch (this->_isa)
     {
     case abi::ISA::x86_64: return abi::CallingConventionName::Microsoftx64;
     default: throw TracedException("ISA not handled yet");
     }
}

ecpps::abi::CallingConvention& ecpps::abi::ABI::CallingConventionFromName(CallingConventionName name)
{
     for (const auto& cc : this->_callingConventions)
          if (cc->Name() == name) return *cc;

     throw ecpps::TracedException(std::logic_error("Invalid calling convention"));
}

ecpps::abi::StorageRef ecpps::abi::MicrosoftX64CallingConvention::ReturnValueStorage(
    StorageRequirement storageSize) const
{
     switch (storageSize.kind)
     {
     case RequiredStorageKind::Void: return StorageRef{std::monostate{}};
     case RequiredStorageKind::Integer:
     {
          switch (storageSize.size)
          {
          case 1:
               return StorageRef{abi::ABI::Current().AllocateRegister(byteSize, "rax", RegisterAllocation::Priority)};
          case 2:
               return StorageRef{abi::ABI::Current().AllocateRegister(wordSize, "rax", RegisterAllocation::Priority)};
          case 4:
               return StorageRef{abi::ABI::Current().AllocateRegister(dwordSize, "rax", RegisterAllocation::Priority)};
          case 8:
               return StorageRef{abi::ABI::Current().AllocateRegister(qwordSize, "rax", RegisterAllocation::Priority)};
          default:
          {
               // TODO: 16/32/64 bit + errors
          }
          break;
          }
     }
     break;
     case RequiredStorageKind::FloatingPoint:
     {
          switch (storageSize.size)
          {
          case 16:
               return StorageRef{abi::ABI::Current().AllocateRegister(xmmSize, "xmm0", RegisterAllocation::Priority)};
          case 32:
               return StorageRef{abi::ABI::Current().AllocateRegister(ymmSize, "xmm0", RegisterAllocation::Priority)};
          case 64:
               return StorageRef{abi::ABI::Current().AllocateRegister(zmmSize, "xmm0", RegisterAllocation::Priority)};
          default:
          {
               // TODO: errors
          }
          break;
          }
     }
     break;
     default: throw TracedException("Not implemented storage size");
     }

     throw nullptr; // FIXME: No comment needed
}

std::vector<ecpps::abi::StorageRef> ecpps::abi::MicrosoftX64CallingConvention::LocateParameters(
    [[maybe_unused]] StorageRequirement returnSize, std::vector<StorageRequirement> parameters) const
{
     std::vector<ecpps::abi::StorageRef> result{};
     result.reserve(parameters.size());
     std::size_t stackIndex = this->_shadowSpace;
     std::size_t shadowOffset = 8;

     for (std::size_t parameterIndex = 0; parameterIndex < parameters.size(); ++parameterIndex)
     {
          const auto& parameter = parameters[parameterIndex];
          StorageRef storage = nullptr;

          if (parameterIndex < 4)
          {
               const std::size_t widthInBits = parameter.size * CHAR_BIT;
               std::string registerName;

               switch (widthInBits)
               {
               case 64:
               {
                    const auto regs = std::to_array<std::string_view>({"rcx", "rdx", "r8", "r9"});
                    registerName = regs[parameterIndex];
               }
               break;
               case 32:
               {
                    const auto regs = std::to_array<std::string_view>({"ecx", "edx", "r8d", "r9d"});
                    registerName = regs[parameterIndex];
               }
               break;
               case 16:
               {
                    const auto regs = std::to_array<std::string_view>({"cx", "dx", "r8w", "r9w"});
                    registerName = regs[parameterIndex];
               }
               break;
               case 8:
               {
                    const auto regs = std::to_array<std::string_view>({"cx", "dx", "r8w", "r9w"});
                    registerName = regs[parameterIndex];
               }
               break;
               default: registerName = ""; break;
               }

               if (!registerName.empty())
               {
                    auto allocatedReg =
                        ABI::Current().AllocateRegister(widthInBits, registerName, abi::RegisterAllocation::Priority);

                    if (allocatedReg.Ptr() != nullptr) { storage = StorageRef{std::move(allocatedReg)}; }
               }

               if (std::holds_alternative<std::monostate>(storage.value))
               {
                    storage = StorageRef{
                        MemoryLocation{.offset = shadowOffset, .reg = ABI::Current().StackPointerRegister()}};
               }
               shadowOffset += 8;
          }
          else
          {
               storage = StorageRef{MemoryLocation{.offset = stackIndex, .reg = ABI::Current().StackPointerRegister()}};
               stackIndex += std::max<std::size_t>(parameter.size, 8);
          }

          result.push_back(std::move(storage));
     }

     return result;
}

ecpps::abi::StorageRequirement ecpps::abi::MicrosoftX64CallingConvention::GetRequirementsForType(
    typeSystem::NonowningTypePointer type) const
{
     if (IsIntegral(type))
     {
          const auto* const integralType = type->CastTo<typeSystem::IntegralType>();
          runtime_assert(integralType != nullptr, std::format("Integral type `{}` was not integral", type->RawName()));
          return StorageRequirement{integralType->Size(), integralType->Alignment(), RequiredStorageKind::Integer};
     }

     if (IsPointer(type)) { return StorageRequirement{8, 8, RequiredStorageKind::Integer}; }
     if (IsFloatingPoint(type))
     {
          // TODO: Implement floating-point types
     }
     if (IsIncomplete(type)) return StorageRequirement{0, 0, RequiredStorageKind::Void};

     throw nullptr; // FIXME: Obvious
}

std::size_t ecpps::abi::MicrosoftX64CallingConvention::CalculateArgumentStackSpace(
    [[maybe_unused]] StorageRequirement returnSize, const std::vector<StorageRequirement>& parameters) const
{
     if (parameters.size() <= 4) return 0;

     std::size_t stackSpace = 0;
     for (std::size_t i = 4; i < parameters.size(); ++i) { stackSpace += std::max<std::size_t>(parameters[i].size, 8); }

     return stackSpace;
}

struct Microsoftx64StackManager final : ecpps::abi::ProcedureStackManager
{
     explicit Microsoftx64StackManager(std::vector<ecpps::codegen::Instruction>& instructions,
                                       const ecpps::abi::CallingConvention& callingConvention)
         : ProcedureStackManager(instructions), _currentCallingConvention(std::ref(callingConvention)),
           _currentStackSize(callingConvention.ShadowSpaceSize())
     {
     }
     [[nodiscard]] ecpps::abi::StorageRef ReserveStorage(const ecpps::abi::StorageRequirement& request) final
     {
          const std::size_t alignmentMask = request.alignment - 1;
          if ((this->_currentStackSize & alignmentMask) != 0)
          {
               this->_currentStackSize = (_currentStackSize + alignmentMask) & ~alignmentMask;
          }

          const std::size_t variableOffset = _currentStackSize;
          this->_currentStackSize += request.size;

          return ecpps::abi::StorageRef{
              ecpps::abi::MemoryLocation{.offset = variableOffset, .reg = ABI::Current().StackPointerRegister()}};
     }

     [[nodiscard]] std::size_t GetParameterAdjustment(void) const final
     {
          return (((this->_currentStackSize) + 15) & ~15) + 16;
     }
     void AfterParameters(void) const override {}
     void ReserveCallArgumentSpace(std::size_t argumentStackSpace) final
     {
          this->_maxCallArgumentSpace = std::max(this->_maxCallArgumentSpace, argumentStackSpace);
          this->_currentStackSize += argumentStackSpace;
     }

protected:
     [[nodiscard]] std::vector<ecpps::codegen::Instruction> GeneratePrologue(void) const final;
     [[nodiscard]] std::vector<ecpps::codegen::Instruction> GenerateEpilogue(void) const final;

private:
     std::reference_wrapper<const ecpps::abi::CallingConvention> _currentCallingConvention;
     std::size_t _currentStackSize{};
     std::size_t _maxCallArgumentSpace{};
};

std::unique_ptr<ecpps::abi::ProcedureStackManager> ecpps::abi::MicrosoftX64CallingConvention::BeginStack(
    std::vector<ecpps::codegen::Instruction>& instructions) const
{
     return std::make_unique<Microsoftx64StackManager>(instructions, *this);
}

std::unique_ptr<ecpps::abi::CallTemporaryProxy> ecpps::abi::MicrosoftX64CallingConvention::PrepareForCall(
    std::vector<ecpps::codegen::Instruction>& instructions)
{
     auto& abi = ABI::Current();
     const auto pointerWidth = abi.PointerSize() * byteSize;
     const bool wasRcxUsed = abi.CheckRegisterAllocation(pointerWidth, "rcx");
     const bool wasRdxUsed = abi.CheckRegisterAllocation(pointerWidth, "rdx");
     const bool wasR8Used = abi.CheckRegisterAllocation(pointerWidth, "r8");
     const bool wasR9Used = abi.CheckRegisterAllocation(pointerWidth, "r9");
     std::shared_ptr<VirtualRegister> rcx{};
     std::shared_ptr<VirtualRegister> rdx{};
     std::shared_ptr<VirtualRegister> r8{};
     std::shared_ptr<VirtualRegister> r9{};
     std::shared_ptr<VirtualRegister> rsp = abi.StackPointerRegister();

     const auto& registers = abi.VirtualRegisters();
     for (const auto& reg : registers)
     {
          if (reg->width != pointerWidth) continue;

          if (rcx != nullptr && rdx != nullptr && r8 != nullptr && r9 != nullptr) break;

          if (reg->physical->friendlyName == "rcx")
          {
               rcx = reg;
               continue;
          }
          if (reg->physical->friendlyName == "rdx")
          {
               rdx = reg;
               continue;
          }
          if (reg->physical->friendlyName == "r8")
          {
               r8 = reg;
               continue;
          }
          if (reg->physical->friendlyName == "r9")
          {
               r9 = reg;
               continue;
          }
     }
     runtime_assert(rcx != nullptr && rdx != nullptr && r8 != nullptr && r9 != nullptr,
                    "Cannot find volatile registers");
     std::bitset<4> savedRegisters{};

     if (wasRcxUsed)
     {
          instructions.emplace_back(ecpps::codegen::MovInstruction{
              ecpps::codegen::RegisterOperand{rcx},
              ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{rsp}, 0, pointerWidth},
              pointerWidth});
          savedRegisters.set(0);
     }

     if (wasRdxUsed)
     {
          instructions.emplace_back(
              ecpps::codegen::MovInstruction{ecpps::codegen::RegisterOperand{rdx},
                                             ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{rsp},
                                                                                   abi.PointerSize(), pointerWidth},
                                             pointerWidth});
          savedRegisters.set(1);
     }

     if (wasR8Used)
     {
          instructions.emplace_back(
              ecpps::codegen::MovInstruction{ecpps::codegen::RegisterOperand{r8},
                                             ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{rsp},
                                                                                   2 * abi.PointerSize(), pointerWidth},
                                             pointerWidth});
          savedRegisters.set(2);
     }

     if (wasR9Used)
     {
          instructions.emplace_back(
              ecpps::codegen::MovInstruction{ecpps::codegen::RegisterOperand{r8},
                                             ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{rsp},
                                                                                   3 * abi.PointerSize(), pointerWidth},
                                             pointerWidth});
          savedRegisters.set(3);
     }

     return std::make_unique<WindowsABICallProxy>(savedRegisters);
}

ecpps::abi::AllocatedRegister::AllocatedRegister(std::shared_ptr<VirtualRegister> reg) : _register(std::move(reg))
{
     if (this->_register == nullptr) return;

     abi::ABI::Current()._allocatedRegisters.try_emplace(this->_register->physical->id, 0).first->second++;
}

ecpps::abi::AllocatedRegister::~AllocatedRegister(void)
{
     if (this->_register == nullptr) return;

     (*abi::ABI::Current()._allocatedRegisters.try_emplace(this->_register->physical->id, 1).first).second--;
}

void ecpps::abi::AllocatedRegister::Release(void)
{
     if (this->_register == nullptr) return;

     (*abi::ABI::Current()
           ._allocatedRegisters.try_emplace(std::exchange(this->_register, nullptr)->physical->id, 1)
           .first)
         .second--;
}

std::vector<ecpps::codegen::Instruction> Microsoftx64StackManager::GeneratePrologue(void) const
{
     std::vector<ecpps::codegen::Instruction> instructions{};
     const auto& stackPointerRegister = ABI::Current().StackPointerRegister();
     instructions.emplace_back(ecpps::codegen::SubInstruction{
         ecpps::codegen::IntegerOperand{((this->_currentStackSize + 15) & ~15) + 8, stackPointerRegister->width},
         ecpps::codegen::RegisterOperand{stackPointerRegister}, stackPointerRegister->width});
     return instructions;
}

std::vector<ecpps::codegen::Instruction> Microsoftx64StackManager::GenerateEpilogue(void) const
{
     std::vector<ecpps::codegen::Instruction> instructions{};
     const auto& stackPointerRegister = ABI::Current().StackPointerRegister();
     instructions.emplace_back(ecpps::codegen::AddInstruction{
         ecpps::codegen::IntegerOperand{((this->_currentStackSize + 15) & ~15) + 8, stackPointerRegister->width},
         ecpps::codegen::RegisterOperand{stackPointerRegister}, stackPointerRegister->width});
     return instructions;
}

void ecpps::abi::WindowsABICallProxy::End(std::vector<ecpps::codegen::Instruction>& instructions)
{
     auto& abi = ABI::Current();
     const bool wasRcxUsed = this->_savedRegisters.test(0);
     const bool wasRdxUsed = this->_savedRegisters.test(1);
     const bool wasR8Used = this->_savedRegisters.test(2);
     const bool wasR9Used = this->_savedRegisters.test(3);
     std::shared_ptr<VirtualRegister> rcx{};
     std::shared_ptr<VirtualRegister> rdx{};
     std::shared_ptr<VirtualRegister> r8{};
     std::shared_ptr<VirtualRegister> r9{};
     std::shared_ptr<VirtualRegister> rsp = abi.StackPointerRegister();
     const auto pointerWidth = abi.PointerSize() * byteSize;

     const auto& registers = abi.VirtualRegisters();
     for (const auto& reg : registers)
     {
          if (reg->width != pointerWidth) continue;

          if (rcx != nullptr && rdx != nullptr && r8 != nullptr && r9 != nullptr) break;

          if (reg->physical->friendlyName == "rcx")
          {
               rcx = reg;
               continue;
          }
          if (reg->physical->friendlyName == "rdx")
          {
               rdx = reg;
               continue;
          }
          if (reg->physical->friendlyName == "r8")
          {
               r8 = reg;
               continue;
          }
          if (reg->physical->friendlyName == "r9")
          {
               r9 = reg;
               continue;
          }
     }
     runtime_assert(rcx != nullptr && rdx != nullptr && r8 != nullptr && r9 != nullptr,
                    "Cannot find volatile registers");

     if (wasRcxUsed)
          instructions.emplace_back(ecpps::codegen::MovInstruction{
              ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{rsp}, 0, pointerWidth},
              ecpps::codegen::RegisterOperand{rcx}, pointerWidth});

     if (wasRdxUsed)
          instructions.emplace_back(
              ecpps::codegen::MovInstruction{ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{rsp},
                                                                                   abi.PointerSize(), pointerWidth},
                                             ecpps::codegen::RegisterOperand{rdx}, pointerWidth});

     if (wasR8Used)
          instructions.emplace_back(
              ecpps::codegen::MovInstruction{ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{rsp},
                                                                                   2 * abi.PointerSize(), pointerWidth},
                                             ecpps::codegen::RegisterOperand{r8}, pointerWidth});

     if (wasR9Used)
          instructions.emplace_back(
              ecpps::codegen::MovInstruction{ecpps::codegen::MemoryLocationOperand{ecpps::codegen::RegisterOperand{rsp},
                                                                                   3 * abi.PointerSize(), pointerWidth},
                                             ecpps::codegen::RegisterOperand{r9}, pointerWidth});
}
