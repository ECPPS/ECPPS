#pragma once

#include <Numbers.h>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../Machine/Machine.h"
#include "CodeGeneration/AbstractNodes.h"
#include "Nodes.h"

define_number(ByteOffset, std::size_t);
define_number(Address, std::size_t);

namespace ecpps::codegen
{
     struct Relocation
     {
          std::string symbolName;
          std::function<std::vector<std::byte>(
               Address, std::unordered_map<std::string, std::vector<std::byte>>& thunkProcedures)>
               apply;
          std::size_t applyOutputSize;
     };
     struct StringRelocation
     {
          std::size_t offset;
          std::uint8_t _register;
          std::function<std::vector<std::byte>(std::uint8_t _register, std::size_t stringTableOffset)> apply;
     };
     using LinkerRelocationMap = std::unordered_map<ByteOffset, Relocation>;

     /// <summary>
     /// Provides a foundation for all emitters.
     /// An emitter might add custom emittees, but it has to implement the instructions added here
     /// Note that each emitter header might define its own instruction set on top of the existing one, mainly for the
     /// architecture-specific optimiser. Each custom-defined instruction should be generated from the Optimise virtual
     /// member function. Note that the Optimise function is called after the generic optimiser, and the generic
     /// optimiser will not understand its output. For each custom-defined instruction, it shall inherit from
     /// ArchitectureInstruction class that is part of the Instruction variant.
     /// </summary>
     class CodeEmitter
     {
     public:
          virtual ~CodeEmitter(void);
          [[nodiscard]] std::vector<std::byte> EmitRoutine(const Routine& routine, std::size_t displacement);
          [[nodiscard]] const std::string& Name(void) const noexcept
          {
               return this->_name;
          }

          virtual void PatchCalls(std::vector<std::byte>& source,
                                  std::unordered_map<std::string, std::size_t>& routines) = 0;

          static std::unique_ptr<CodeEmitter> New(abi::ISA isa);

          LinkerRelocationMap linkerForwardedRelocations{}; // part of the public API
          std::size_t _stringRelocationSize{};              // in bytes
          std::vector<std::size_t> _stringRelocation{};

     protected:
          explicit CodeEmitter(std::string name) : _name(std::move(name))
          {
          }

          std::size_t _currentInstructionBase{};
          // PRE emitting: used to store locations of calls to be patched later
          // POST emitting: contains offsets to be patched with the function address (imports)
          std::map<std::size_t, std::string> _relocationTable{};

     private:
          [[nodiscard]] virtual std::vector<std::byte> EmitInstruction(
               const ir::abstract::Instruction& instruction) = 0;

          std::string _name;
     };
} // namespace ecpps::codegen
