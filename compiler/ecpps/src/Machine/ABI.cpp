#include "ABI.h"
#include "Vendor/Shared/ISA.h"

using ecpps::abi::ABI;

ABI ABI::_current{Platform::CurrentISA<Platform::CurrentVendor()>()};

ecpps::abi::ABI::ABI(ISA isa) : _isa(isa)
{
     switch (isa)
     {
     case ISA::x86_64:
     {
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
          this->_registers.push_back(std::make_shared<VirtualRegister>("rsp", rsp, qwordSize, 0));
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
          this->_registers.push_back(std::make_shared<VirtualRegister>("rdi", rdi, qwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("edi", rdi, dwordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("di", rdi, wordSize, 0));
          this->_registers.push_back(std::make_shared<VirtualRegister>("dil", rdi, byteSize, 0));

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
     }
}

void ecpps::abi::ABI::PushSIMDRegisters(const SimdFeatures simd)
{
     switch (this->_isa)
     {
     case ISA::x86_64:
     {
          constexpr auto xmmSize = 16;
          constexpr auto ymmSize = 32;
          constexpr auto zmmSize = 64;

          std::size_t id = this->_physicalRegisters.empty() ? 0 : this->_physicalRegisters.back()->id + 1;

          if (simd & (SimdFeatures::AVX512F | SimdFeatures::AVX512BW | SimdFeatures::AVX512DQ | SimdFeatures::AVX512VL))
          {
               for (std::size_t i = 0; i < 16; i++)
               {
                    const auto& physicalReg = this->_physicalRegisters.emplace_back(
                        std::make_shared<PhysicalRegister>(std::string("zmm") + std::to_string(i), zmmSize, id++));
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
                        std::make_shared<PhysicalRegister>(std::string("ymm") + std::to_string(i), ymmSize, id++));
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
     }
}

ABI& ecpps::abi::ABI::Current(void) { return ABI::_current; }

ecpps::abi::AllocatedRegister ecpps::abi::ABI::AllocateRegister(const std::size_t width)
{
     for (const auto& reg : this->_registers)
     {
          if (reg->width != width || this->_allocatedRegisters.contains(reg->physical->id)) continue;
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
          if (allocation == RegisterAllocation::Normal && this->_allocatedRegisters.contains(reg->physical->id))
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
          if (allocation == RegisterAllocation::Normal && this->_allocatedRegisters.contains(reg->physical->id))
               continue;
          return AllocatedRegister{reg};
     }

     return AllocatedRegister{nullptr};
}

ecpps::abi::AllocatedRegister ecpps::abi::ABI::AllocateRegister(const std::shared_ptr<VirtualRegister>& toAllocate,
                                                                RegisterAllocation allocation)
{
     if (allocation == RegisterAllocation::Normal && this->_allocatedRegisters.contains(toAllocate->physical->id))
          return AllocatedRegister{nullptr};

     return AllocatedRegister{toAllocate};
}

ecpps::abi::StorageRef ecpps::abi::MicrosoftX64CallingConvention::ReturnValueStorage(std::size_t storageSize) const
{
     switch (storageSize)
     {
     case 1: return StorageRef{abi::ABI::Current().AllocateRegister(byteSize, "rax", RegisterAllocation::Priority)};
     case 2: return StorageRef{abi::ABI::Current().AllocateRegister(wordSize, "rax", RegisterAllocation::Priority)};
     case 4: return StorageRef{abi::ABI::Current().AllocateRegister(dwordSize, "rax", RegisterAllocation::Priority)};
     case 8: return StorageRef{abi::ABI::Current().AllocateRegister(qwordSize, "rax", RegisterAllocation::Priority)};
     default:
     {

     }
     break;
     }
}

std::vector<ecpps::abi::StorageRef> ecpps::abi::MicrosoftX64CallingConvention::LocateParameters(
    std::size_t returnSize, std::vector<std::size_t> parameters) const
{
     return std::vector<StorageRef>();
}

ecpps::abi::AllocatedRegister::AllocatedRegister(std::shared_ptr<VirtualRegister> reg)
     : _register(std::move(reg))
{
     abi::ABI::Current()._allocatedRegisters.emplace(this->_register->physical->id);
}

ecpps::abi::AllocatedRegister::~AllocatedRegister(void)
{
     if (this->_register == nullptr) return;

     ABI::Current()._allocatedRegisters.erase(this->_register->physical->id);
}