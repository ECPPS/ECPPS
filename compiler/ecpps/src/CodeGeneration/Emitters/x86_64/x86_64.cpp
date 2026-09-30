// NOLINT(readability-identifier-length)

#include "x86_64.h"
#include <cstddef>
#include <cstring>
#include <limits>
#include "CodeGeneration/Emitters/x86_64/Opcodes.h"
#include "CodeGeneration/PseudoAssembly.h"
#include "Execution/Context.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "RuntimeAssert.h"

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitInstruction(
     const ir::abstract::Instruction& instruction)
{
     switch (instruction.opcode)
     {
     case abi::encoders::x8664::X8664InstructionName::Mov: return this->EmitMov(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Add: return this->EmitAdd(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Ret: return this->EmitRet(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Sub: return this->EmitSub(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Push: return this->EmitPush(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::LeftShift: return this->EmitLeftShift(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::RightShift: return this->EmitRightShift(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Xchg: return this->EmitXchg(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Pop: return this->EmitPop(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::BinaryOr: return this->EmitBinaryOr(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::BinaryAnd: return this->EmitBinaryAnd(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::BinaryXor: return this->EmitBinaryXor(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::BitwiseNot: return this->EmitBitwiseNot(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Neg: return this->EmitArithmeticNegation(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::SignExtend: return this->EmitMovsx(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::ZeroExtend: return this->EmitMovzx(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Call: return this->EmitCall(instruction.description);
     case abi::encoders::x8664::X8664InstructionName::Lea: return this->EmitLea(instruction.description);
     default: throw TracedException("x86-64 does not implement this opcode yet");
     }
}

void ecpps::codegen::emitters::X8664Emitter::PatchCalls(std::vector<std::byte>& instructions, const Routine& routine)
{
     constexpr static auto ApplyImportLambda = [](const bool isIndirect) -> std::vector<std::byte>
     {
          return isIndirect ? x86_64::GenerateIndirectCall2(0) : x86_64::GenerateCallRel32(0);
     };

     for (const auto offset : this->_callPatches)
     {
          if (instructions.size() <= offset) continue; // ???
          runtime_assert(instructions[offset] == std::byte{0xe8}, "Invalid offset");
          std::int32_t index{};
          std::memcpy(&index, instructions.data() + offset + 1uz, sizeof(std::int32_t));
          const ir::FunctionScope* function = routine.scopes[static_cast<std::size_t>(index)];

          if (function->emittedOffset == std::numeric_limits<std::size_t>::max())
          {
               const auto mangled =
                    ecpps::abi::ABI::MangleName(function->linkage, function->Name().value_or("__undefined_call"),
                                                function->callingConvention, function->returnType,
                                                function->parameters |
                                                     std::views::transform(
                                                          [](const ecpps::ir::FunctionScope::Parameter& parameter)
                                                          {
                                                               return parameter.type;
                                                          }) |
                                                     std::ranges::to<std::vector>(),
                                                function->namespacePath);
               this->linkerForwardedRelocations.emplace(
                    ByteOffset(offset + routine.emittedOffset),
                    Relocation{.symbolName = mangled,
                               .apply = ApplyImportLambda,
                               .applyOutputSize = 2uz,
                               .isIndirect = function->isDllImportExport}); // Linker pass handles that, hopefully

               const auto prefix = function->isDllImportExport ? ecpps::abi::ABI::Current().importPrefix : "";
               if (!ecpps::codegen::g_functionImports.contains(mangled))
                    ecpps::codegen::g_functionImports[mangled] =
                         prefix + (function->dllImportName.empty() ? mangled : function->dllImportName);

               continue;
          }

          const std::int32_t displacement = static_cast<std::int32_t>(function->emittedOffset) -
                                            static_cast<std::int32_t>(offset + routine.emittedOffset) - 5;
          std::memcpy(instructions.data() + offset + 1uz, &displacement, sizeof(std::int32_t));
     }
     this->_callPatches.clear();
}

void ecpps::codegen::emitters::X8664Emitter::PatchStrings(std::vector<std::byte>& instructions,
                                                          std::vector<StringPatch> patches,
                                                          const AssemblyContext& asmContext, const Routine& routine)
{
     this->_stringRelocation.reserve(patches.size());
     for (const auto& patch : patches)
     {
          const auto index = asmContext.GetStringOffset(patch.string);
          std::memcpy(&instructions[patch.instructionOffset + 3], &index, sizeof(std::uint32_t));
          this->_stringRelocation.push_back(patch.instructionOffset + routine.emittedOffset + 3);
     }
}
