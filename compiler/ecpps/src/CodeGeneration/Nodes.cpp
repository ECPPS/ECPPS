#include "Nodes.h"
#include <variant>
#include "../Execution/NodeBase.h"

std::string ecpps::codegen::RegisterOperand::ToString(void) const noexcept { return this->_index->friendlyName; }

std::string ecpps::codegen::IntegerOperand::ToString(void) const noexcept { return std::to_string(this->_value); }

std::string ecpps::codegen::MemoryLocationOperand::ToString(void) const noexcept
{
     if (this->_displacement == 0) return "[" + this->_register.ToString() + "]";
     return "[" + this->_register.ToString() + " + " + std::to_string(this->_displacement) + "]";
}

std::string ecpps::codegen::ToString(const Instruction& instruction)
{
     return std::visit(
         OverloadedVisitor{[](const MovInstruction& instruction)
                           {
                                std::string built = "mov";
                                built += " " + std::visit([](const auto& operand) -> std::string
                                                          { return operand.ToString(); }, instruction.destination);
                                built += ", " + std::visit([](const auto& operand) -> std::string
                                                           { return operand.ToString(); }, instruction.source);
                                return built;
                           },
                           [](const AddInstruction& instruction)
                           {
                                std::string built = "add";
                                built += " " + std::visit([](const auto& operand) -> std::string
                                                          { return operand.ToString(); }, instruction.to);
                                built += ", " + std::visit([](const auto& operand) -> std::string
                                                           { return operand.ToString(); }, instruction.from);
                                return built;
                           },
                           [](const ReturnInstruction&) -> std::string { return "ret"; }},
         instruction);
}
