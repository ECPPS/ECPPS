#pragma once
#include <memory>

namespace ecpps::typeSystem
{
	class TypeBase	// dummy class
	{
	public:
		virtual ~TypeBase(void) = default;
	};

	using TypePointer = std::shared_ptr<TypeBase>;
}