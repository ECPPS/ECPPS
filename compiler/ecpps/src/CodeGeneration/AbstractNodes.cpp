#include "AbstractNodes.h"
#include <algorithm>
#include <format>
#include <string>
std::string ecpps::ir::abstract::ToString(const InstructionDescription& description)
{
     using std::operator""s;

     std::string built{};
     switch (description.type)
     {
     case ecpps::ir::abstract::DescriptionType::Copy: built += "copy "; break;
     case ecpps::ir::abstract::DescriptionType::Add: built += "add "; break;
     default: built += "unknown "; break;
     }
     if (description.operands.empty()) return built;
     built += std::ranges::fold_left(description.operands, ""s,
                                     [](const std::string& built, DescribedOperand operand)
                                     {
                                          return std::format("{}{}, ", built,
                                                             [operand]
                                                             {
                                                                  switch (operand.type)
                                                                  {
                                                                  case DescribedOperandType::Unused: return "unused";
                                                                  case DescribedOperandType::Input: return "in";
                                                                  case DescribedOperandType::Output: return "out";
                                                                  }
                                                             }());
                                     });
     return built;
}
