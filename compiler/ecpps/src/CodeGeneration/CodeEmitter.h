#pragma once

#include "../Machine/Machine.h"
#include "Nodes.h"
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ecpps::codegen
{

	/// <summary>
	/// Provides a foundation for all emitters.
	/// An emitter might add custom emittees, but it has to implement the instructions added here
	/// Note that each emitter header might define its own instruction set on top of the existing one, mainly for the
	/// architecture-specific optimiser. Each custom-defined instruction should be generated from the Optimise virtual
	/// member function. Note that the Optimise function is called after the generic optimiser, and the generic
	/// optimiser will not understand its output. For each custom-defined instruction, it shall inherit from
	/// ArchitectureInstruction class that is part of the Instruction variant.
	/// </summary>
	class CodeEmitter
	{
	public:
		~CodeEmitter(void) = default;
		[[nodiscard]] std::vector<std::byte> EmitRoutine(const Routine& routine, std::size_t displacement);
		[[nodiscard]] const std::string& Name(void) const noexcept { return this->_name; }

		virtual void PatchCalls(std::vector<std::byte>& source, const std::unordered_map<std::string, std::size_t>& routines) = 0;

		static std::unique_ptr<CodeEmitter> New(abi::ISA isa);

	protected:
		explicit CodeEmitter(std::string name) : _name(std::move(name)) {}

		std::size_t _currentInstructionBase{};
		std::map<std::size_t, std::string> _relocationTable{};
	private:
		[[nodiscard]] std::vector<std::byte> EmitInstruction(const Instruction& instruction);

		// generic instruction emitters
		[[nodiscard]] virtual std::vector<std::byte> EmitMov(const MovInstruction& mov) = 0;
		[[nodiscard]] virtual std::vector<std::byte> EmitAdd(const AddInstruction& add) = 0;
		[[nodiscard]] virtual std::vector<std::byte> EmitSub(const SubInstruction& sub) = 0;
		[[nodiscard]] virtual std::vector<std::byte> EmitMul(const MulInstruction& mul) = 0;
		[[nodiscard]] virtual std::vector<std::byte> EmitDiv(const DivInstruction& div) = 0;
		[[nodiscard]] virtual std::vector<std::byte> EmitCall(const CallInstruction& call) = 0;
		[[nodiscard]] virtual std::vector<std::byte> EmitReturn(void) = 0;

		/// <summary>
		/// Custom instruction emitter. By default a no-op.
		/// </summary>
		[[nodiscard]] virtual std::vector<std::byte> EmitCustomInstruction(const CustomInstruction& instruction)
		{
			return {};
		}

		std::string _name;
	};
} // namespace ecpps::codegen