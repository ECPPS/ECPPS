#include "encoder.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include "CodeGeneration/AbstractNodes.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Backends/x86_64/Core/Instructions/Common/CommonOperations.h"
#include "Parsing/Tokeniser.h"
#include "RuntimeAssert.h"
#include "Shared/Diagnostics.h"

extern template std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::
     X8664VirtualInstructionEncoder::EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Copy>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray);
extern template std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::
     X8664VirtualInstructionEncoder::EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::CopyInteger>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray);
extern template std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::
     X8664VirtualInstructionEncoder::EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Add>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray);
extern template std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::
     X8664VirtualInstructionEncoder::EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Return>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray);

extern template ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::
     X8664VirtualInstructionEncoder::MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Copy>(
          ecpps::ir::abstract::VirtualRegister owner, std::span<const std::byte> data);

extern template ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::
     X8664VirtualInstructionEncoder::MaterialisationImplementation<
          ecpps::ir::abstract::VirtualInstructionType::CopyInteger>(ecpps::ir::abstract::VirtualRegister owner,
                                                                    std::span<const std::byte> data);

extern template ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::
     X8664VirtualInstructionEncoder::MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Add>(
          ecpps::ir::abstract::VirtualRegister owner, std::span<const std::byte> data);

extern template std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::
     X8664VirtualInstructionEncoder::EncoderImplementation<ecpps::ir::abstract::VirtualInstructionType::Sub>(
          const std::vector<ecpps::ir::abstract::VirtualRegister>& registerArray);

extern template ecpps::abi::encoders::x8664::MaterialisationOutcome ecpps::abi::encoders::x8664::
     X8664VirtualInstructionEncoder::MaterialisationImplementation<ecpps::ir::abstract::VirtualInstructionType::Sub>(
          ecpps::ir::abstract::VirtualRegister owner, std::span<const std::byte> data);

std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::Encode(
     const std::vector<ir::abstract::VirtualInstruction>& input)
{
     this->_registerAllocator = PhysicalRegisterAllocator{};
     this->_stackSlots.clear();
     this->_remainingUses.clear();
     this->_localsSize = 0;
     this->_outgoingReserve = this->_target->platform->InitialStackReserve();
     this->_stackFrameSize = 0;

     for (const auto& instruction : input)
     {
          if (instruction.type == ir::abstract::VirtualInstructionType::CopyInteger) continue;

          const std::size_t firstSource = instruction.type == ir::abstract::VirtualInstructionType::Return ? 0 : 1;
          for (const auto& source : instruction.operands | std::views::drop(firstSource))
               ++this->_remainingUses[source.index];
     }

     std::vector<ecpps::ir::abstract::Instruction> instructions{};

     for (const auto& instruction : input) instructions.append_range(EncodeSingle(instruction));

     return instructions;
}
std::string ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::Stringify(
     const ecpps::ir::abstract::Instruction& instruction) const
{
     auto opcode = instruction.opcode;
     switch (opcode)
     {
     case X8664InstructionName::Mov:
     {
          runtime_assert(instruction.description.size() == sizeof(MovInstruction), "invalid MOV");
          const auto* mov = std::launder(reinterpret_cast<const MovInstruction*>(instruction.description.data()));
          return std::format("MOV.{} {}, {}", ToString(mov->width), ToString(mov->destination), ToString(mov->source));
     }
     case X8664InstructionName::Add:
     {
          runtime_assert(instruction.description.size() == sizeof(AddInstruction), "invalid ADD");
          const auto* add = std::launder(reinterpret_cast<const AddInstruction*>(instruction.description.data()));
          return std::format("ADD.{} {}, {}", ToString(add->width), ToString(add->modifiedDestination),
                             ToString(add->source));
     }
     case X8664InstructionName::Sub:
     {
          runtime_assert(instruction.description.size() == sizeof(SubInstruction), "invalid SUB");
          const auto* sub = std::launder(reinterpret_cast<const SubInstruction*>(instruction.description.data()));
          return std::format("SUB.{} {}, {}", ToString(sub->width), ToString(sub->modifiedDestination),
                             ToString(sub->source));
     }
     case X8664InstructionName::Ret:
     {
          runtime_assert(instruction.description.empty(), "invalid RET");
          return "RET";
     }
     case X8664InstructionName::Pop:
     {
          runtime_assert(instruction.description.size() == sizeof(PopInstruction), "invalid POP");
          const auto* pop = std::launder(reinterpret_cast<const PopInstruction*>(instruction.description.data()));
          return std::format("POP {}", ToString(pop->reg));
     }
     case X8664InstructionName::Push:
     {
          runtime_assert(instruction.description.size() == sizeof(PushInstruction), "invalid PUSH");
          const auto* push = std::launder(reinterpret_cast<const PushInstruction*>(instruction.description.data()));
          return std::format("PUSH {}", ToString(push->reg));
     }
     }

     return "__unknown";
}

[[nodiscard]] ecpps::ir::abstract::VirtualRegisterMap& ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     GetVRM(void) noexcept
{
     return *this->_target->registerMap;
}
std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::EncodeSingle(
     const ir::abstract::VirtualInstruction& instruction)
{
     std::vector<ecpps::ir::abstract::Instruction> instructions{};

     switch (instruction.type)
     {
     case ecpps::ir::abstract::VirtualInstructionType::Copy:
          instructions.append_range(
               EncoderImplementation<ir::abstract::VirtualInstructionType::Copy>(instruction.operands));
          break;
     case ecpps::ir::abstract::VirtualInstructionType::CopyInteger:
          instructions.append_range(
               EncoderImplementation<ir::abstract::VirtualInstructionType::CopyInteger>(instruction.operands));
          break;
     case ecpps::ir::abstract::VirtualInstructionType::Add:
          instructions.append_range(
               EncoderImplementation<ir::abstract::VirtualInstructionType::Add>(instruction.operands));
          break;
     case ecpps::ir::abstract::VirtualInstructionType::Sub:
          instructions.append_range(
               EncoderImplementation<ir::abstract::VirtualInstructionType::Sub>(instruction.operands));
          break;
     case ecpps::ir::abstract::VirtualInstructionType::Return:
          instructions.append_range(
               EncoderImplementation<ir::abstract::VirtualInstructionType::Return>(instruction.operands));
          break;
     default: throw TracedException("Invalid instruction");
     }

     return instructions;
}

bool ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::IsMutable(
     const ecpps::ir::abstract::VirtualRegister reg)
{
     return this->GetVRM().GetAllocationClass(reg) >= ir::abstract::AllocationClass::Allocation;
}

bool ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::IsSpilled(
     const ecpps::ir::abstract::VirtualRegister reg)
{
     return this->GetVRM().GetAllocationClass(reg) >= SpillThreshold(this->_optimisation);
}

std::optional<std::uint64_t> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::ImmediateOf(
     const ecpps::ir::abstract::VirtualRegister reg)
{
     if (this->IsSpilled(reg) || this->GetVRM().IsMaterialised(reg)) return std::nullopt;

     const auto& value = this->GetVRM().GetValue(reg);
     if (value.type != ir::abstract::StateType::Allocation) return std::nullopt;

     const auto& valueBase = *std::launder(reinterpret_cast<const AssignedValueBase*>(value.data.data()));
     if (valueBase.type != AssignedValueType::CopyInteger) return std::nullopt;

     const auto& copyValue = *std::launder(reinterpret_cast<const values::CopyIntegerToRegister*>(value.data.data()));
     return std::get<0>(copyValue.parameters);
}

ecpps::abi::encoders::x8664::StackOperand ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::EnsureStackSlot(
     const ecpps::ir::abstract::VirtualRegister reg)
{
     if (const auto iterator = this->_stackSlots.find(reg.index); iterator != this->_stackSlots.end())
          return StackOperand{.offset = iterator->second};

     const auto size = std::max<std::size_t>(this->GetVRM().GetSize(reg), 1);
     const auto alignment = std::max<std::size_t>(this->GetVRM().GetAlignment(reg), 1);

     const auto offset = (this->_localsSize + alignment - 1) / alignment * alignment;
     this->_localsSize = offset + size;

     const auto slot = static_cast<std::uint32_t>(offset);
     this->_stackSlots.emplace(reg.index, slot);
     return StackOperand{.offset = slot};
}

ecpps::abi::encoders::x8664::RegisterIndex ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     PhysicalRegisterOf(const ecpps::ir::abstract::VirtualRegister reg)
{
     runtime_assert(this->GetVRM().IsMaterialised(reg), "Register must be materialised");
     const auto& optional = this->GetVRM().GetMaterialisation(reg);
     runtime_assert(optional.has_value() && optional->type == ir::abstract::StateType::Allocation,
                    "Register must be materialised");

     const auto& base = *std::launder(reinterpret_cast<const MaterialisationBase*>(optional->data.data()));
     runtime_assert(base.type == materialisations::PhysicalRegister::ConstType,
                    "Register must have been assigned a physical register");

     const auto& physical =
          *std::launder(reinterpret_cast<const materialisations::PhysicalRegister*>(optional->data.data()));
     return std::get<0>(physical.parameters);
}

std::size_t ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::ConsumeUse(
     const ecpps::ir::abstract::VirtualRegister reg)
{
     const auto iterator = this->_remainingUses.find(reg.index);
     runtime_assert(iterator != this->_remainingUses.end() && iterator->second != 0,
                    "Virtual register consumed more often than it is used");
     return --iterator->second;
}

void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::ReleaseRegister(
     const ecpps::ir::abstract::VirtualRegister reg)
{
     if (this->IsSpilled(reg)) return;
     if (!this->GetVRM().IsMaterialised(reg)) return;

     this->_registerAllocator.Free(reg);
     this->GetVRM().ClearMaterialisation(reg);
}

void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::DereferenceAndMaybeFree(
     const ecpps::ir::abstract::VirtualRegister reg)
{
     if (this->ConsumeUse(reg) != 0) return;

     this->ReleaseRegister(reg);
}

void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::TransferRegister(
     const ecpps::ir::abstract::VirtualRegister from, const ecpps::ir::abstract::VirtualRegister to)
{
     this->_registerAllocator.Reassign(from, to);
     this->GetVRM().ClearMaterialisation(from);
}
ecpps::ir::abstract::Instruction ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::BuildMov(
     Width width, Operand destination, Operand source)
{
     ir::abstract::Instruction instruction{};
     instruction.opcode = X8664InstructionName::Mov;
     instruction.description.resize(sizeof(MovInstruction));

     new (instruction.description.data()) MovInstruction{.width = width, .destination = destination, .source = source};

     return instruction;
}

ecpps::ir::abstract::Instruction ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::BuildAdd(
     Width width, Operand modifiedDestination, Operand source)
{
     ir::abstract::Instruction instruction{};
     instruction.opcode = X8664InstructionName::Add;
     instruction.description.resize(sizeof(AddInstruction));

     new (instruction.description.data())
          AddInstruction{.width = width, .modifiedDestination = modifiedDestination, .source = source};

     return instruction;
}

ecpps::ir::abstract::Instruction ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::BuildSub(
     Width width, Operand modifiedDestination, Operand source)
{
     ir::abstract::Instruction instruction{};
     instruction.opcode = X8664InstructionName::Sub;
     instruction.description.resize(sizeof(SubInstruction));

     new (instruction.description.data())
          SubInstruction{.width = width, .modifiedDestination = modifiedDestination, .source = source};

     return instruction;
}

ecpps::ir::abstract::Instruction ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::BuildPush(
     RegisterOperand reg)
{
     ir::abstract::Instruction instruction{};
     instruction.opcode = X8664InstructionName::Push;
     instruction.description.resize(sizeof(PushInstruction));
     new (instruction.description.data()) PushInstruction{.reg = reg};
     return instruction;
}
ecpps::ir::abstract::Instruction ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::BuildPop(
     RegisterOperand reg)
{
     ir::abstract::Instruction instruction{};
     instruction.opcode = X8664InstructionName::Pop;
     instruction.description.resize(sizeof(PopInstruction));
     new (instruction.description.data()) PopInstruction{.reg = reg};
     return instruction;
}

std::vector<ecpps::ir::abstract::Instruction> ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::
     EnsureMaterialisation(ecpps::ir::abstract::VirtualRegister virtualRegister)
{
     if (this->IsSpilled(virtualRegister))
     {
          std::ignore = this->EnsureStackSlot(virtualRegister);
          return {};
     }

     if (this->GetVRM().IsMaterialised(virtualRegister)) return {};

     const auto& value = this->GetVRM().GetValue(virtualRegister);
     runtime_assert(value.type != ir::abstract::StateType::Unknown, "Cannot materialise a register with unknown value");
     runtime_assert(value.type != ir::abstract::StateType::Impossible,
                    "Cannot materialise a register with impossible state");

     const auto& valueBase = *std::launder(reinterpret_cast<const AssignedValueBase*>(value.data.data()));

     MaterialisationOutcome outcome;
     switch (valueBase.type)
     {
     case ecpps::abi::encoders::x8664::AssignedValueType::Copy:
          outcome = MaterialisationImplementation<ir::abstract::VirtualInstructionType::Copy>(
               virtualRegister, std::span<const std::byte>{value.data});
          break;
     case ecpps::abi::encoders::x8664::AssignedValueType::CopyInteger:
          outcome = MaterialisationImplementation<ir::abstract::VirtualInstructionType::CopyInteger>(
               virtualRegister, std::span<const std::byte>{value.data});
          break;
     case ecpps::abi::encoders::x8664::AssignedValueType::Add:
          outcome = MaterialisationImplementation<ir::abstract::VirtualInstructionType::Add>(
               virtualRegister, std::span<const std::byte>{value.data});
          break;
     case ecpps::abi::encoders::x8664::AssignedValueType::Sub:
          outcome = MaterialisationImplementation<ir::abstract::VirtualInstructionType::Sub>(
               virtualRegister, std::span<const std::byte>{value.data});
          break;
     default: throw TracedException("Invalid opcode");
     }

     ir::abstract::State materialisedState{};
     materialisedState.type = ir::abstract::StateType::Allocation;
     materialisedState.data.resize(sizeof(materialisations::PhysicalRegister));
     materialisations::PhysicalRegister& physical =
          *new (materialisedState.data.data()) materialisations::PhysicalRegister{};
     physical.parameters = std::make_tuple(outcome.assignedRegister);
     this->GetVRM().Materialise(virtualRegister, materialisedState);

     return std::move(outcome.instructions);
}

[[nodiscard]] static std::string FormatRegister(ecpps::abi::encoders::x8664::RegisterIndex index)
{
     std::string formattedRegister{};
     switch (index)
     {
     case ecpps::abi::encoders::x8664::RegisterIndex::Rax: formattedRegister = "rax"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rbx: formattedRegister = "rbx"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rcx: formattedRegister = "rcx"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rdx: formattedRegister = "rdx"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rsp: formattedRegister = "rsp"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rbp: formattedRegister = "rbp"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rsi: formattedRegister = "rsi"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rdi: formattedRegister = "rdi"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R8: formattedRegister = "r8"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R9: formattedRegister = "r9"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R10: formattedRegister = "r10"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R11: formattedRegister = "r11"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R12: formattedRegister = "r12"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R13: formattedRegister = "r13"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R14: formattedRegister = "r14"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::R15: formattedRegister = "r15"; break;
     case ecpps::abi::encoders::x8664::RegisterIndex::Rip: formattedRegister = "rip"; break;
     }
     return formattedRegister;
}

[[nodiscard]] std::string ecpps::abi::encoders::x8664::ToString(const Operand& operand)
{
     return std::visit(OverloadedVisitor{[](const RegisterOperand& reg)
                                         {
                                              return FormatRegister(reg.index);
                                         },
                                         [](const MemoryOperand& mem)
                                         {
                                              const auto base = FormatRegister(mem.relativeTo);
                                              if (mem.offset < 0)
                                                   return std::format("[{} - {}]", base,
                                                                      -static_cast<std::int64_t>(mem.offset));
                                              return std::format("[{} + {}]", base, mem.offset);
                                         },
                                         [](const IntegerOperand& integer)
                                         {
                                              return std::format("{}", integer.value);
                                         },
                                         [](const StackOperand& stack)
                                         {
                                              return std::format("[locals + {}]", stack.offset);
                                         }},
                       operand);
}
void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::Redefine(ecpps::ir::abstract::VirtualRegister reg,
                                                                           ecpps::ir::abstract::State value)
{
     if (this->GetVRM().IsMaterialised(reg))
     {
          this->_registerAllocator.Free(reg);
          this->GetVRM().ClearMaterialisation(reg);
     }

     this->GetVRM().UpdateValue(reg, std::move(value));
}

ecpps::abi::encoders::x8664::Operand ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::ResolveStackOperand(
     const Operand& operand) const
{
     const auto* stack = std::get_if<StackOperand>(&operand);
     if (stack == nullptr) return operand;

     const auto aboveRsp = this->_outgoingReserve + stack->offset;
     runtime_assert(aboveRsp < this->_stackFrameSize, "Stack slot lies outside of the stack frame");

     if (this->OmitsFramePointer())
          return MemoryOperand{.relativeTo = RegisterIndex::Rsp, .offset = static_cast<std::int32_t>(aboveRsp)};

     return MemoryOperand{.relativeTo = RegisterIndex::Rbp,
                          .offset =
                               static_cast<std::int32_t>(aboveRsp) - static_cast<std::int32_t>(this->_stackFrameSize)};
}

void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::ResolveStackOperands(
     std::vector<ir::abstract::Instruction>& instructions) const
{
     for (auto& instruction : instructions)
     {
          switch (instruction.opcode)
          {
          case X8664InstructionName::Mov:
          {
               auto* mov = std::launder(reinterpret_cast<MovInstruction*>(instruction.description.data()));
               mov->destination = this->ResolveStackOperand(mov->destination);
               mov->source = this->ResolveStackOperand(mov->source);
               break;
          }
          case X8664InstructionName::Add:
          {
               auto* add = std::launder(reinterpret_cast<AddInstruction*>(instruction.description.data()));
               add->modifiedDestination = this->ResolveStackOperand(add->modifiedDestination);
               add->source = this->ResolveStackOperand(add->source);
               break;
          }
          case X8664InstructionName::Sub:
          {
               auto* sub = std::launder(reinterpret_cast<SubInstruction*>(instruction.description.data()));
               sub->modifiedDestination = this->ResolveStackOperand(sub->modifiedDestination);
               sub->source = this->ResolveStackOperand(sub->source);
               break;
          }
          default: break;
          }
     }
}

void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::Finalise(
     std::vector<ir::abstract::Instruction>& instructions)
{
     const std::size_t rawSize = this->_outgoingReserve + this->_localsSize;
     this->_stackFrameSize = rawSize;

     if (rawSize != 0)
     {
          const std::size_t alignment = this->_target->platform->StackAlignment();
          if (alignment != 0)
          {
               const std::size_t alreadyPushed = this->OmitsFramePointer() ? 8 : 16;
               this->_stackFrameSize =
                    ((rawSize + alreadyPushed + alignment - 1) / alignment * alignment) - alreadyPushed;
          }
     }

     this->ResolveStackOperands(instructions);

     if (this->_stackFrameSize == 0) return;

     this->InsertPrologue(instructions);
     this->InsertEpilogue(instructions);
}

void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::InsertPrologue(
     std::vector<ir::abstract::Instruction>& instructions)
{
     std::vector<ir::abstract::Instruction> prologue{};

     if (!this->OmitsFramePointer())
     {
          prologue.push_back(BuildPush(RegisterOperand{RegisterIndex::Rbp}));
          prologue.push_back(
               BuildMov(Width::W64, RegisterOperand{RegisterIndex::Rbp}, RegisterOperand{RegisterIndex::Rsp}));
     }
     prologue.push_back(
          BuildSub(Width::W64, RegisterOperand{RegisterIndex::Rsp}, IntegerOperand{this->_stackFrameSize}));

     instructions.insert(instructions.begin(), prologue.begin(), prologue.end());
}

void ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder::InsertEpilogue(
     std::vector<ir::abstract::Instruction>& instructions)
{
     std::vector<ir::abstract::Instruction> epilogue{};
     epilogue.push_back(
          BuildAdd(Width::W64, RegisterOperand{RegisterIndex::Rsp}, IntegerOperand{this->_stackFrameSize}));
     if (!this->OmitsFramePointer()) epilogue.push_back(BuildPop(RegisterOperand{RegisterIndex::Rbp}));

     for (std::size_t index = instructions.size(); index-- > 0;)
     {
          if (instructions[index].opcode != X8664InstructionName::Ret) continue;

          instructions.insert(instructions.begin() + static_cast<std::ptrdiff_t>(index), epilogue.begin(),
                              epilogue.end());
     }
}

[[nodiscard]] std::string ecpps::abi::encoders::x8664::ToString(const Width width)
{
     switch (width)
     {
     case Width::W8: return "8";
     case Width::W16: return "16";
     case Width::W32: return "32";
     case Width::W64: return "64";
     }

     std::unreachable();
}
