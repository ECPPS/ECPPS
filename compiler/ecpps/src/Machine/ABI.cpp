#include "ABI.h"

using ecpps::abi::ABI;

ABI* ABI::_current{};

static ABI g_machine{};

ecpps::abi::ABI::ABI(void)
{}

const ABI& ecpps::abi::ABI::Current(void)
{
	return ABI::_current;
}
