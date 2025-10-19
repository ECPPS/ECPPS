#pragma once
#include <Assert.h>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>
#include "../CodeGeneration/Nodes.h"
#include "../TypeSystem/TypeBase.h"
#include "Machine.h"
#include "Storage.h"

/// <summary>
/// The term "width" is always measured in bits, while "size" in bytes. ECPPS ABI defines one byte to be exactly eight
/// bits, and the ABI is unsupported on any platform that states otherwise.
/// </summary>

namespace ecpps::abi
{
     enum struct Linkage : std::uint_fast8_t
     {
          NoLinkage,
          Internal,
          External,
          Module,
          CLinkage
     };

     enum struct CallingConventionName : std::uint_fast16_t
     {
          Microsoftx64
     };

     struct [[nodiscard]] ProcedureStackManager
     {
          virtual ~ProcedureStackManager(void)
          {
               runtime_assert(this->_wasFinished, "You did not call Finish() silly");
          }
          void Finish(void)
          {
               runtime_assert(!this->_wasFinished, "finishing up the stack twice is weird");

               this->_wasFinished = true;

               auto& instructions = this->_instructions.get();
               instructions.insert_range(instructions.begin(), this->GeneratePrologue());
               auto epilogue = this->GenerateEpilogue();

               for (auto it = instructions.begin(); it != instructions.end(); ++it)
               {
                    if (!std::holds_alternative<codegen::ReturnInstruction>(*it)) continue;

                    it = instructions.insert_range(it, epilogue);
                    std::advance(it, epilogue.size());
               }
          }
          explicit ProcedureStackManager(std::vector<codegen::Instruction>& instructions)
              : _instructions(std::ref(instructions))
          {
          }

          //[[nodiscard]] virtual StorageRef ReserveVariable(const StorageRequirement& request) = 0;

     protected:
          virtual std::vector<ecpps::codegen::Instruction> GeneratePrologue(void) const = 0;
          virtual std::vector<ecpps::codegen::Instruction> GenerateEpilogue(void) const = 0;

     private:
          DebugOnly<bool> _wasFinished = false;
          std::reference_wrapper<std::vector<codegen::Instruction>> _instructions;
     };

     struct CallingConvention
     {
          explicit CallingConvention(const CallingConventionName name, const std::size_t shadowSpace,
                                     const std::size_t stackAlignment)
              : _name(name), _shadowSpace(shadowSpace), _stackAlignment(stackAlignment)
          {
          }
          virtual ~CallingConvention(void) = default;

          [[nodiscard]] virtual StorageRef ReturnValueStorage(StorageRequirement storageSize) const = 0;
          [[nodiscard]] virtual std::vector<StorageRef> LocateParameters(
              StorageRequirement returnSize, std::vector<StorageRequirement> parameters) const = 0;
          [[nodiscard]] virtual StorageRequirement GetRequirementsForType(
              const typeSystem::TypePointer& type) const = 0;

          [[nodiscard]] CallingConventionName Name(void) const noexcept { return this->_name; }
          [[nodiscard]] std::size_t ShadowSpaceSize(void) const noexcept { return this->_shadowSpace; }
          [[nodiscard]] std::size_t StackAlignment(void) const noexcept { return this->_stackAlignment; }
          [[nodiscard]] virtual std::unique_ptr<ProcedureStackManager> BeginStack(
              std::vector<ecpps::codegen::Instruction>&) const = 0;

     protected:
          CallingConventionName _name;
          std::size_t _shadowSpace;
          std::size_t _stackAlignment;
     };

     struct MicrosoftX64CallingConvention final : CallingConvention
     {
          explicit MicrosoftX64CallingConvention(void) : CallingConvention(CallingConventionName::Microsoftx64, 32, 16)
          {
          }

          [[nodiscard]] StorageRef ReturnValueStorage(StorageRequirement storageSize) const override;
          [[nodiscard]] std::vector<StorageRef> LocateParameters(
              StorageRequirement returnSize, std::vector<StorageRequirement> parameters) const override;
          [[nodiscard]] StorageRequirement GetRequirementsForType(const typeSystem::TypePointer& type) const override;
          [[nodiscard]] std::unique_ptr<ProcedureStackManager> BeginStack(
              std::vector<ecpps::codegen::Instruction>& instructionVector) const final override;
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
          [[nodiscard]] const CallingConvention& CallingConventionFromName(CallingConventionName name);
          [[nodiscard]] const std::shared_ptr<VirtualRegister>& StackPointerRegister(void) const noexcept
          {
               return this->_stackPointerRegister;
          }

          const std::vector<std::shared_ptr<PhysicalRegister>>& PhysicalRegisters(void) const noexcept
          {
               return this->_physicalRegisters;
          }
          const std::vector<std::shared_ptr<VirtualRegister>>& VirtualRegisters(void) const noexcept
          {
               return this->_registers;
          }

     private:
          static ABI _current;

          ISA _isa;
          std::vector<std::shared_ptr<PhysicalRegister>> _physicalRegisters;
          std::vector<std::shared_ptr<VirtualRegister>> _registers;

          std::unordered_set<std::unique_ptr<CallingConvention>> _callingConventions{};
          std::unordered_set<std::size_t> _allocatedRegisters{};
          std::shared_ptr<VirtualRegister> _stackPointerRegister{};

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