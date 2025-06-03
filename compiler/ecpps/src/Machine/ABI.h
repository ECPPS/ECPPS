#pragma once
#include <cstddef>
#include <vector>
#include <string>
#include <memory>
#include "Machine.h"


/// <summary>
/// The term "width" is always measured in bits, while "size" in bytes. ECPPS ABI defines one byte to be exactly eight bits, and the ABI is unsupported on any platform that states otherwise.
/// </summary>

namespace ecpps::abi
{
	constexpr std::size_t qwordSize = 64;
	constexpr std::size_t dwordSize = 32;
	constexpr std::size_t wordSize = 16;
	constexpr std::size_t byteSize = 8;

	struct PhysicalRegister
	{
		std::string friendlyName;
		std::size_t width;
		std::size_t id;

		constexpr bool operator==(const PhysicalRegister& other)
		{
			return this->id == other.id;
		}

		/// <summary>
		/// Constructs a new register object
		/// </summary>
		/// <param name="name">Name of the register</param>
		/// <param name="width">Width (in bits) of the register</param>
		/// <param name="id">Unique register id</param>
		explicit PhysicalRegister(std::string name, const std::size_t width, const std::size_t id)
			: friendlyName(std::move(name)), width(width), id(id)
		{}
	};
	struct VirtualRegister
	{
		std::string friendlyName;
		std::shared_ptr<PhysicalRegister> physical;
		std::size_t width;
		std::size_t offset;
		explicit VirtualRegister(std::string name, std::shared_ptr<PhysicalRegister> physical, const std::size_t width, const std::size_t offset)
			: friendlyName(std::move(name)), physical(std::move(physical)), width(width), offset(offset)
		{}
	};

	class ABI
	{
	public:
		explicit ABI(ISA isa);
		void PushSIMDRegisters(SimdFeatures simd);

		static ABI& Current(void);

		ISA Isa(void) const noexcept { return this->_isa; }
	private:
		static ABI _current;

		ISA _isa;
		std::size_t _stackAlignment;
		std::vector<std::shared_ptr<PhysicalRegister>> _physicalRegisters;
		std::vector<std::shared_ptr<VirtualRegister>> _registers;
	};
}