#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <variant>
#include <vector>
#include "../x86_64.h"
#include "CodeGeneration/Emitters/x86_64/Opcodes.h"
#include "Machine/Encoders/Backends/x86_64/Core/encoder.h"
#include "Parsing/Tokeniser.h"
#include "RuntimeAssert.h"
#include "Shared/Diagnostics.h"

using ecpps::abi::encoders::x8664::XchgInstruction;

std::vector<std::byte> ecpps::codegen::emitters::X8664Emitter::EmitXchg(
     const ir::abstract::DynamicBytecode& description)
{
     runtime_assert(description.size() == sizeof(XchgInstruction), "Invalid XCHG instruction");

     const auto& instruction = *std::launder(reinterpret_cast<const XchgInstruction*>(description.data()));
     const auto& source = instruction.modifiedSource;
     const auto width = instruction.width;

     return std::visit(
          OverloadedVisitor{
               [&](const abi::encoders::x8664::RegisterOperand destination) -> std::vector<std::byte>
               {
                    const auto a = std::to_underlying(destination.index);
                    return std::visit(
                         OverloadedVisitor{
                              [&](const abi::encoders::x8664::RegisterOperand other) -> std::vector<std::byte>
                              {
                                   return x86_64::GenerateXchgRegReg(width, a, std::to_underlying(other.index));
                              },
                              [&](const abi::encoders::x8664::MemoryOperand mem) -> std::vector<std::byte>
                              {
                                   return x86_64::GenerateXchgMemReg(
                                        width,
                                        x86_64::MemBase(std::to_underlying(mem.relativeTo),
                                                        static_cast<std::int32_t>(mem.offset)),
                                        a);
                              },
                              [](const abi::encoders::x8664::StackOperand&) -> std::vector<std::byte>
                              {
                                   throw TracedException("unresolved StackOperand: Finalise must run before emission");
                              },
                              [](auto&&...) -> std::vector<std::byte>
                              {
                                   throw TracedException("invalid XCHG source");
                              }},
                         source);
               },
               [&](const abi::encoders::x8664::MemoryOperand destination) -> std::vector<std::byte>
               {
                    const auto mem = x86_64::MemBase(std::to_underlying(destination.relativeTo),
                                                     static_cast<std::int32_t>(destination.offset));
                    return std::visit(
                         OverloadedVisitor{
                              [&](const abi::encoders::x8664::RegisterOperand other) -> std::vector<std::byte>
                              {
                                   return x86_64::GenerateXchgMemReg(width, mem, std::to_underlying(other.index));
                              },
                              [](const abi::encoders::x8664::StackOperand&) -> std::vector<std::byte>
                              {
                                   throw TracedException("unresolved StackOperand: Finalise must run before emission");
                              },
                              [](auto&&...) -> std::vector<std::byte>
                              {
                                   throw TracedException("XCHG cannot exchange memory with memory/immediate");
                              }},
                         source);
               },
               [](const abi::encoders::x8664::StackOperand&) -> std::vector<std::byte>
               {
                    throw TracedException("unresolved StackOperand: Finalise must run before emission");
               },
               [](auto&&...) -> std::vector<std::byte>
               {
                    throw TracedException("invalid XCHG destination");
               }},
          instruction.modifiedDestination);
}
