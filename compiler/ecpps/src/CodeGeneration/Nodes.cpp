#include "Nodes.h"
#include <format>
#include <variant>
#include "../Parsing/Tokeniser.h"

std::string ecpps::codegen::RegisterOperand::ToString(void) const { return this->_index->friendlyName; }

std::string ecpps::codegen::IntegerOperand::ToString(void) const { return std::to_string(this->_value); }

std::string ecpps::codegen::MemoryLocationOperand::ToString(void) const
{
     if (this->_displacement == 0) return "[" + this->_register.ToString() + "]";
     return "[" + this->_register.ToString() + " + " + std::to_string(this->_displacement) + "]";
}

std::string ecpps::codegen::ToString(const Instruction& instruction)
{
     return std::visit(
         OverloadedVisitor{
             [](const MovInstruction& instruction)
             {
                  std::string built =
                      instruction.isConversion
                          ? std::format("mov.{}-{}",
                                        std::visit(OverloadedVisitor{[](std::monostate) -> std::size_t { return 0; },
                                                                     [](const auto& operand) -> std::size_t
                                                                     { return operand.Size(); }},
                                                   instruction.source),
                                        std::visit(OverloadedVisitor{[](std::monostate) -> std::size_t { return 0; },
                                                                     [](const auto& operand) -> std::size_t
                                                                     { return operand.Size(); }},
                                                   instruction.destination))
                          : std::format("mov.{}", instruction.width);
                  built += " " + std::visit(OverloadedVisitor{[](std::monostate) -> std::string { return "?"; },
                                                              [](const auto& operand) -> std::string
                                                              { return operand.ToString(); }},
                                            instruction.destination);
                  built += ", " + std::visit(OverloadedVisitor{[](std::monostate) -> std::string { return "?"; },
                                                               [](const auto& operand) -> std::string
                                                               { return operand.ToString(); }},
                                             instruction.source);
                  return built;
             },
             [](const TakeAddressInstruction& instruction)
             {
                  std::string built = "address-of";
                  built += " " + std::visit(OverloadedVisitor{[](std::monostate) -> std::string { return "?"; },
                                                              [](const auto& operand) -> std::string
                                                              { return operand.ToString(); }},
                                            instruction.to);
                  built += ", " + instruction.from.ToString();
                  return built;
             },
             [](const AddInstruction& instruction)
             {
                  std::string built = std::format("add.{}", instruction.width);
                  built += " " + std::visit(OverloadedVisitor{[](std::monostate) -> std::string { return "?"; },
                                                              [](const auto& operand) -> std::string
                                                              { return operand.ToString(); }},
                                            instruction.to);
                  built += ", " + std::visit(OverloadedVisitor{[](std::monostate) -> std::string { return "?"; },
                                                               [](const auto& operand) -> std::string
                                                               { return operand.ToString(); }},
                                             instruction.from);
                  return built;
             },
             [](const SubInstruction& instruction)
             {
                  std::string built = std::format("sub.{}", instruction.width);
                  built += " " + std::visit(OverloadedVisitor{[](std::monostate) -> std::string { return "?"; },
                                                              [](const auto& operand) -> std::string
                                                              { return operand.ToString(); }},
                                            instruction.to);
                  built += ", " + std::visit(OverloadedVisitor{[](std::monostate) -> std::string { return "?"; },
                                                               [](const auto& operand) -> std::string
                                                               { return operand.ToString(); }},
                                             instruction.from);
                  return built;
             },
             [](const MulInstruction& instruction)
             {
                  std::string built = instruction.isSigned ? std::format("imul.{}", instruction.width)
                                                           : std::format("mul.{}", instruction.width);
                  built += " " + std::visit(OverloadedVisitor{[](std::monostate) -> std::string { return "?"; },
                                                              [](const auto& operand) -> std::string
                                                              { return operand.ToString(); }},
                                            instruction.to);
                  built += ", " + std::visit(OverloadedVisitor{[](std::monostate) -> std::string { return "?"; },
                                                               [](const auto& operand) -> std::string
                                                               { return operand.ToString(); }},
                                             instruction.from);
                  return built;
             },
             [](const DivInstruction& instruction)
             {
                  std::string built = instruction.isSigned ? std::format("idiv.{}", instruction.width)
                                                           : std::format("div.{}", instruction.width);
                  built += " " + std::visit(OverloadedVisitor{[](std::monostate) -> std::string { return "?"; },
                                                              [](const auto& operand) -> std::string
                                                              { return operand.ToString(); }},
                                            instruction.to);
                  built += ", " + std::visit(OverloadedVisitor{[](std::monostate) -> std::string { return "?"; },
                                                               [](const auto& operand) -> std::string
                                                               { return operand.ToString(); }},
                                             instruction.from);
                  return built;
             },
             [](const CallInstruction& instruction) { return "call " + instruction.functionName; },
             [](const ReturnInstruction&) -> std::string { return "ret"; }},
         instruction);
}
