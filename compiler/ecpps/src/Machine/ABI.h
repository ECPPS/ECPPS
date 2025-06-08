#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>
#include "../TypeSystem/TypeBase.h"
#include "Machine.h"

/// <summary>
/// The term "width" is always measured in bits, while "size" in bytes. ECPPS ABI defines one byte to be exactly eight
/// bits, and the ABI is unsupported on any platform that states otherwise.
/// </summary>

namespace ecpps::abi
{
     constexpr std::size_t qwordSize = 64;
     constexpr std::size_t dwordSize = 32;
     constexpr std::size_t wordSize = 16;
     constexpr std::size_t byteSize = 8;

     enum struct Linkage : std::uint_fast8_t
     {
          NoLinkage,
          Internal,
          External,
          Module,
          CLinkage
     };

     struct PhysicalRegister
     {
          std::string friendlyName;
          std::size_t width;
          std::size_t id;

          constexpr bool operator==(const PhysicalRegister& other) { return this->id == other.id; }
          constexpr bool operator!=(const PhysicalRegister& other) { return this->id != other.id; }

          /// <summary>
          /// Constructs a new register object
          /// </summary>
          /// <param name="name">Name of the register</param>
          /// <param name="width">Width (in bits) of the register</param>
          /// <param name="id">Unique register id</param>
          explicit PhysicalRegister(std::string name, const std::size_t width, const std::size_t id)
              : friendlyName(std::move(name)), width(width), id(id)
          {
          }
     };
     struct VirtualRegister
     {
          std::string friendlyName;
          std::shared_ptr<PhysicalRegister> physical;
          std::size_t width;
          std::size_t offset;
          explicit VirtualRegister(std::string name, std::shared_ptr<PhysicalRegister> physical,
                                   const std::size_t width, const std::size_t offset)
              : friendlyName(std::move(name)), physical(std::move(physical)), width(width), offset(offset)
          {
          }
     };

     class AllocatedRegister
     {
     public:
          explicit AllocatedRegister(std::shared_ptr<VirtualRegister> reg);

          AllocatedRegister(const AllocatedRegister&) = delete;
          AllocatedRegister(AllocatedRegister&&) = default;
          AllocatedRegister& operator=(const AllocatedRegister&) = delete;
          AllocatedRegister& operator=(AllocatedRegister&&) = default;

          ~AllocatedRegister(void);
          void Release(void);

          [[nodiscard]] bool operator!(void) const noexcept { return this->_register != nullptr; }

          VirtualRegister* operator->(void) noexcept { return this->_register.get(); }
          const VirtualRegister* operator->(void) const noexcept { return this->_register.get(); }

          const std::shared_ptr<VirtualRegister>& Ptr(void) const noexcept { return this->_register; }

     private:
          std::shared_ptr<VirtualRegister> _register;
     };

     struct StorageRef
     {
          using ValueType = std::variant<AllocatedRegister,
                                         std::vector<AllocatedRegister>>; // TODO: Add memory location
          ValueType value;
          explicit StorageRef(ValueType value) : value(std::move(value)) {}
     };

     enum struct CallingConventionName : std::uint_fast16_t
     {
          Microsoftx64
     };

     struct CallingConvention
     {
          explicit CallingConvention(const CallingConventionName name, const std::size_t shadowSpace,
                                     const std::size_t stackAlignment)
              : _name(name), _shadowSpace(shadowSpace), _stackAlignment(stackAlignment)
          {
          }
          virtual ~CallingConvention(void) = default;

          [[nodiscard]] virtual StorageRef ReturnValueStorage(std::size_t storageSize) const = 0;
          [[nodiscard]] virtual std::vector<StorageRef> LocateParameters(std::size_t returnSize,
                                                                         std::vector<std::size_t> parameters) const = 0;

          [[nodiscard]] CallingConventionName Name(void) const noexcept { return this->_name; }
          [[nodiscard]] std::size_t ShadowSpaceSize(void) const noexcept { return this->_shadowSpace; }
          [[nodiscard]] std::size_t StackAlignment(void) const noexcept { return this->_stackAlignment; }

     private:
          CallingConventionName _name;
          std::size_t _shadowSpace;
          std::size_t _stackAlignment;
     };

     struct MicrosoftX64CallingConvention final : CallingConvention
     {
          explicit MicrosoftX64CallingConvention(void) : CallingConvention(CallingConventionName::Microsoftx64, 32, 16)
          {
          }

          [[nodiscard]] StorageRef ReturnValueStorage(std::size_t storageSize) const override;
          [[nodiscard]] std::vector<StorageRef> LocateParameters(std::size_t returnSize,
                                                                 std::vector<std::size_t> parameters) const override;
     };

     enum struct RegisterAllocation : bool
     {
          Normal = false,
          Priority = true
     };

     class ABI
     {
     public:
          explicit ABI(ISA isa);
          void PushSIMDRegisters(SimdFeatures simd);

          static ABI& Current(void);

          [[nodiscard]] ISA Isa(void) const noexcept { return this->_isa; }
          [[nodiscard]] AllocatedRegister AllocateRegister(std::size_t width);
          [[nodiscard]] AllocatedRegister AllocateRegister(std::size_t width, const std::string& name,
                                                           RegisterAllocation allocation);
          [[nodiscard]] AllocatedRegister AllocateRegister(std::size_t width,
                                                           const std::shared_ptr<PhysicalRegister>& toAllocate,
                                                           RegisterAllocation allocation);
          [[nodiscard]] AllocatedRegister AllocateRegister(const std::shared_ptr<VirtualRegister>& toAllocate,
                                                           RegisterAllocation allocation);

          [[nodiscard]] std::string MangleName(Linkage linkage, const std::string& name,
                                               CallingConventionName callingConvetion,
                                               const typeSystem::TypePointer& returnType,
                                               const std::vector<typeSystem::TypePointer>& parameters);

          CallingConventionName DefaultCallingConventionName(void) const;

     private:
          static ABI _current;

          ISA _isa;
          std::vector<std::shared_ptr<PhysicalRegister>> _physicalRegisters;
          std::vector<std::shared_ptr<VirtualRegister>> _registers;

          std::unordered_set<std::unique_ptr<CallingConvention>> _callingConventions{};
          std::unordered_set<std::size_t> _allocatedRegisters{};

          friend AllocatedRegister;
     };
} // namespace ecpps::abi

namespace
{
     std::string ToString(const ecpps::abi::CallingConventionName callingConvention)
     {
          switch (callingConvention)
          {
          case ecpps::abi::CallingConventionName::Microsoftx64: return "__mscall";
          }
          return "__undefined";
     }
} // namespace