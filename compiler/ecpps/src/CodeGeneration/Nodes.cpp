#include "Nodes.h"

std::string ecpps::codegen::RegisterOperand::ToString(void) const noexcept
{
	return ::ToString(this->_index, static_cast<OperandSize>(this->_size));
}