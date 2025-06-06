#include "Nodes.h"
#include "../Execution/NodeBase.h"

std::string ecpps::codegen::RegisterOperand::ToString(void) const noexcept { return this->_index->friendlyName; }

std::string ecpps::codegen::IntegerOperand::ToString(void) const noexcept { return std::to_string(this->_value); }

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
                           [](const ReturnInstruction&) -> std::string { return "ret"; }},
         instruction);
}
