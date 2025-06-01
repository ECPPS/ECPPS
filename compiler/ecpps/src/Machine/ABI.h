#pragma once
#include <cstddef>
#include <vector>
#include <string>
#include <memory>

namespace ecpps::abi
{
	struct PhysicalRegister
	{
		std::string friendlyName;
		std::size_t size;
		std::size_t id;
		explicit PhysicalRegister(std::string name, const std::size_t size, const std::size_t id)
			: friendlyName(std::move(name)), size(size), id(id)
		{}
	};
	struct VirtualRegister
	{
		std::string friendlyName;
		std::shared_ptr<PhysicalRegister> physical;
		std::size_t size;
		std::size_t displacement;
		explicit VirtualRegister(std::string name, std::shared_ptr<PhysicalRegister> physical, const std::size_t size, const std::size_t displacement)
			: friendlyName(std::move(name)), physical(std::move(physical)), size(size), displacement(displacement)
		{}
	};

	class ABI
	{
	public:
		explicit ABI(void);

		static const ABI& Current(void);
	private:
		static ABI _current;

		std::size_t _stackAlignment;
		std::vector<std::shared_ptr<PhysicalRegister>> _physicalRegisters;
		std::vector<std::shared_ptr<VirtualRegister>> _registers;
	};
}