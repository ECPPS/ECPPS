#include "../Opcodes.h"
#include "../x86_64.h"

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitRet(
     [[maybe_unused]] const ir::abstract::DynamicBytecode& description)
{
     return x86_64::GenerateRet();
}
