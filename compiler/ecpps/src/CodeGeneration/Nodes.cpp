#include "Nodes.h"
#include <TypeSystem/TypeBase.h>
#include <format>
#include <variant>
#include "../Parsing/Tokeniser.h"

std::string ecpps::codegen::Routine::GenerateName(void)
{
     static std::size_t index{};
     return ".LOC" + std::to_string(++index);
}
