#pragma once

#include <cstddef>
#include <memory>
#include <string>      
#include <unordered_set>
#include <variant>

namespace ecpps::abi
{
     // TODO: Rename to *Width
     constexpr std::size_t zmmSize = 512;
     constexpr std::size_t ymmSize = 256;
     constexpr std::size_t xmmSize = 128;
     constexpr std::size_t qwordSize = 64;
     constexpr std::size_t dwordSize = 32;
     constexpr std::size_t wordSize = 16;
     constexpr std::size_t byteSize = 8;

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
          using ValueType = std::variant<std::monostate, AllocatedRegister,
                                         std::vector<AllocatedRegister>>; // TODO: Add memory location
          ValueType value;
          explicit StorageRef(ValueType value) : value(std::move(value)) {}
          explicit(false) StorageRef(std::nullptr_t) : value(std::monostate{}) {}

          StorageRef& operator=(ValueType other)
          {
               this->value = std::move(other);
               return *this;
          }
     };
     enum struct RequiredStorageKind : std::uint_fast8_t
     {
          Integer,
          FloatingPoint,
          Aggregate,
          Pointer,
          Void
     };

     struct StorageRequirement
     {
          std::size_t size;
          std::size_t alignment;
          RequiredStorageKind kind;

          explicit StorageRequirement(std::size_t size, std::size_t alignment, RequiredStorageKind kind)
              : size(size), alignment(alignment), kind(kind)
          {
          }
     };
}