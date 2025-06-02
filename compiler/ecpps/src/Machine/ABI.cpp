#include "ABI.h"
#include "Vendor/Shared/ISA.h"

using ecpps::abi::ABI;

ABI ABI::_current{Platform::CurrentISA<Platform::CurrentVendor()>()};

ecpps::abi::ABI::ABI(ISA isa)
{
	switch (isa)
	{
	case ISA::x86_64:
	{
		std::size_t id{};

		const auto& rax = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rax", 8, id++));
		const auto& rcx = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rcx", 8, id++));
		const auto& rdx = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rdx", 8, id++));
		const auto& rbx = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rbx", 8, id++));
		const auto& rsp = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rsp", 8, id++));
		const auto& rbp = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rbp", 8, id++));
		const auto& rsi = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rsi", 8, id++));
		const auto& rdi = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("rdi", 8, id++));

		const auto& r8 = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r8", 8, id++));
		const auto& r9 = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r9", 8, id++));
		const auto& r10 = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r10", 8, id++));
		const auto& r11 = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r11", 8, id++));
		const auto& r12 = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r12", 8, id++));
		const auto& r13 = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r13", 8, id++));
		const auto& r14 = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r14", 8, id++));
		const auto& r15 = this->_physicalRegisters.emplace_back(std::make_shared<PhysicalRegister>("r15", 8, id++));
	} break;
	}
}

const ABI& ecpps::abi::ABI::Current(void) { return ABI::_current; }
