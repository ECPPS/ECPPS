// NOLINT(readability-identifier-length)

#include "x86_64.h"
#include <format>
#include <mutex>
#include <stdexcept>
#include "../../CodeGeneration/PseudoAssembly.h"
#include "../../Parsing/Tokeniser.h"
#include "CodeGeneration/Nodes.h"
#include "Machine/ABI.h"
#include "x86_64/Opcodes.h"

void ecpps::codegen::emitters::X8664Emitter::PatchCalls(std::vector<std::byte>& source,
                                                        std::unordered_map<std::string, std::size_t>& routines)
{
     constexpr static auto ApplyImportLambda =
         [](Address resolved, [[maybe_unused]] std::unordered_map<std::string, std::vector<std::byte>>& thunkProcedures)
         -> std::vector<std::byte>
     { return x86_64::GenerateIndirectCall2(static_cast<std::int32_t>(resolved.Value())); };

     for (const auto& [index, name] : this->_relocationTable)
     {
          if (ecpps::codegen::g_functionImports.contains(name))
          {
               this->linkerForwardedRelocations.emplace(
                   ByteOffset{index}, Relocation{.symbolName = name,
                                                 .apply = ApplyImportLambda,
                                                 .applyOutputSize = 6}); // Linker pass handles that, hopefully
               continue;
          }

          std::size_t foundFunction = 0;
          for (const auto& [functionName, functionOffset] : routines)
          {
               if (functionName != name) continue;
               foundFunction = functionOffset;
               break;
          }

          const auto code = x86_64::GenerateIndirectCall(-static_cast<std::int32_t>(index) +
                                                         static_cast<std::int32_t>(foundFunction));

          for (std::size_t i = 0; i < code.size(); i++) source[index + i] = code[i];
     }
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitInstruction(
    [[maybe_unused]] const ir::abstract::Instruction& instruction)
{ return {}; }
