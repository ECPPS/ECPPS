#include <cstddef>
#include <vector>
#include "../x86_64.h"
#include "CodeGeneration/Emitters/x86_64/Opcodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "RuntimeAssert.h"

using namespace ecpps::abi::encoders::x8664;
using namespace ecpps::codegen::x86_64;

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitLabel(
     const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(LabelInstruction), "Invalid LABEL instruction");
     const auto& label = *std::launder(reinterpret_cast<const LabelInstruction*>(description.data()));

     [[maybe_unused]] const bool inserted =
          this->_labelOffsets.emplace(label.labelId, this->_currentInstructionBase).second;
     runtime_assert(inserted, "Label emitted twice");
     return {};
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitJmp(const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(JmpInstruction), "Invalid JMP instruction");
     const auto& jmp = *std::launder(reinterpret_cast<const JmpInstruction*>(description.data()));

     auto bytes = GenerateJmpRel32(0); // E9 rel32
     this->_jumpPatches.push_back(
          {.instructionOffset = this->_currentInstructionBase, .length = bytes.size(), .labelId = jmp.labelId});
     return bytes;
}

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitJcc(const ir::abstract::DynamicBytecode& description)
{
     using ecpps::ir::abstract::ConditionCode;
     runtime_assert(description.size() == sizeof(JccInstruction), "Invalid Jcc instruction");
     const auto& jcc = *std::launder(reinterpret_cast<const JccInstruction*>(description.data()));

     const auto condition = [&]
     {
          switch (jcc.condition)
          {
          case ConditionCode::Equal: return Condition::Equal;
          case ConditionCode::NotEqual: return Condition::NotEqual;
          case ConditionCode::Less: return Condition::Less;
          case ConditionCode::LessEqual: return Condition::LessOrEqual;
          case ConditionCode::Greater: return Condition::Greater;
          case ConditionCode::GreaterEqual: return Condition::GreaterOrEqual;
          case ConditionCode::Below: return Condition::Below;
          case ConditionCode::BelowEqual: return Condition::BelowOrEqual;
          case ConditionCode::Above: return Condition::Above;
          case ConditionCode::AboveEqual: return Condition::AboveOrEqual;
          }
          std::unreachable();
     }();

     auto bytes = GenerateJccRel32(condition, 0);
     this->_jumpPatches.push_back(
          {.instructionOffset = this->_currentInstructionBase, .length = bytes.size(), .labelId = jcc.labelId});
     return bytes;
}
