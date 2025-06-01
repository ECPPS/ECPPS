#include "Nodes.h"
#include <string>
#include "../Execution/NodeBase.h"

std::string ecpps::codegen::RegisterOperand::ToString(void) const noexcept
{
     return ::ToString(this->_index, static_cast<OperandSize>(this->_size));
}

std::string ecpps::codegen::IntegerOperand::ToString(void) const noexcept { return std::to_string(this->_value); }

std::string ecpps::codegen::ToString(const Instruction& instruction)
{
     static std::unordered_map<OperandSize, std::string_view> SizePrefixes = {
         {OperandSize::Byte, "b"}, {OperandSize::Word, "w"}, {OperandSize::Dword, "d"}, {OperandSize::Qword, "q"},
         {OperandSize::Xmm, "ps"}, {OperandSize::Ymm, "qs"}, {OperandSize::Zmm, "qs"},
     };

     return std::visit(
         OverloadedVisitor{[](const MovInstruction& instruction)
                           {
                                std::string built{};
                                if (instruction.size == OperandSize::Ymm || instruction.size == OperandSize::Zmm)
                                     built += "v";
                                built += "mov";
                                if (instruction.alignment == InstructionAlignment::Aligned) built += "a";
                                else if (instruction.alignment == InstructionAlignment::Unaligned)
                                     built += "u";
                                built += SizePrefixes.at(instruction.size);
                                built += " " + std::visit([](const auto& operand) -> std::string
                                                          { return operand.ToString(); }, instruction.destination);
                                built += ", " + std::visit([](const auto& operand) -> std::string
                                                           { return operand.ToString(); }, instruction.source);
                                return built;
                           },
                           [](const ReturnInstruction&) -> std::string { return "ret"; }},
         instruction);
}
