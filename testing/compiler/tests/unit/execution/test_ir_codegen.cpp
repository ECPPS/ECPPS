#define CATCH_CONFIG_MAIN
#include <CodeGeneration/Emitters/x86_64/x86_64.h>
#include <CodeGeneration/PseudoAssembly.h>
#include <IRTestHelpers.h>
#include <Machine/Encoders/Backends/x86_64/Core/encoder.h>
#include <Machine/Encoders/Backends/x86_64/Platforms/Linux/Linux.h>
#include <Optimiser/passes/deadCode.h>
#include <catch_amalgamated.hpp>
#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

using namespace irTestHelpers;

namespace
{
     ecpps::ir::SSAPointer MakeSSA(ecpps::BumpAllocator& allocator, const std::size_t index)
     {
          return ecpps::ir::SSAPointer{new (allocator) ecpps::ir::SingleAssignRegisterNode(
               index, ecpps::ir::RegisterPriorityInfo{.regClass = ecpps::ir::RegisterClass::Temporary}, 32, 4, Source)};
     }
} // namespace

TEST_CASE("IR - Addition lowers to SSA and returns the result", "[unit][ir][codegen]")
{
     Fixture fixture{};
     auto addition = MakeASTNode<ecpps::ast::BinaryOperatorNode>(fixture.astAllocator, fixture.MakeIdentifier("lhs"),
                                                                 ecpps::ast::Operator::Plus,
                                                                 fixture.MakeIdentifier("rhs"), Source);

     ecpps::SBOVector<ecpps::ast::NodePointer> body{};
     body.Push(MakeASTNode<ecpps::ast::ReturnNode>(fixture.astAllocator, std::move(addition), Source));

     std::vector<ecpps::ast::NodePointer> ast{};
     ast.push_back(fixture.MakeFunctionDefinition(
          fixture.MakeFunctionSignature("int", "sum", {{"int", "lhs"}, {"int", "rhs"}}), std::move(body)));
     auto nodes = fixture.Parse(ast);

     const auto* procedure = FindProcedure(nodes, "sum");
     REQUIRE((procedure != nullptr));
     REQUIRE(fixture.diagnostics.diagnosticsList.empty());

     const auto* lhsParameter = FindNode<ecpps::ir::ParameterNode>(procedure->Body());
     REQUIRE((lhsParameter != nullptr));
     const ecpps::ir::ParameterNode* rhsParameter = nullptr;
     for (const auto& node : procedure->Body())
          if (const auto* parameter = dynamic_cast<const ecpps::ir::ParameterNode*>(node.get());
              parameter != nullptr && parameter->Index() == 1)
               rhsParameter = parameter;
     REQUIRE((lhsParameter->Index() == 0));
     REQUIRE((rhsParameter != nullptr));

     const ecpps::ir::SSALoadNode* lhsLoad = nullptr;
     const ecpps::ir::SSALoadNode* rhsLoad = nullptr;
     for (const auto& node : procedure->Body())
          if (const auto* load = dynamic_cast<const ecpps::ir::SSALoadNode*>(node.get()))
          {
               if (&load->Address() == lhsParameter->Result()) lhsLoad = load;
               if (&load->Address() == rhsParameter->Result()) rhsLoad = load;
          }
     REQUIRE((lhsLoad != nullptr));
     REQUIRE((rhsLoad != nullptr));

     const auto* additionNode = FindNode<ecpps::ir::SSAAddNode>(procedure->Body());
     REQUIRE((additionNode != nullptr));
     REQUIRE((&additionNode->Left() == &lhsLoad->Result()));
     REQUIRE((&additionNode->Right() == &rhsLoad->Result()));

     const auto* returnNode = FindNode<ecpps::ir::SSAReturnNode>(procedure->Body());
     REQUIRE((returnNode != nullptr));
     REQUIRE((returnNode->Operand() == &additionNode->Result()));
}

TEST_CASE("IR - Unreachable node emits x86-64 UD2", "[unit][ir][codegen]")
{
     ecpps::ir::SSAUnreachableNode unreachable{Source};
     ecpps::codegen::ParsingContext context{ecpps::abi::ABI::Current()};
     context.ParseNode(&unreachable);

     REQUIRE((context.instructions.size() == 1));
     CHECK((context.instructions.front().type == ecpps::ir::abstract::VirtualInstructionType::Unreachable));

     ecpps::abi::api::Target target{};
     target.EnsureVRM();
     target.platform = std::make_unique<ecpps::abi::encoders::x8664::LinuxPlatform>(nullptr);
     ecpps::abi::encoders::x8664::X8664VirtualInstructionEncoder encoder{target};
     const auto encoded = encoder.Encode(context.instructions);

     REQUIRE((encoded.size() == 1));
     CHECK((encoded.front().opcode == ecpps::abi::encoders::x8664::X8664InstructionName::UD2));

     ecpps::codegen::emitters::X8664Emitter emitter{};
     const auto bytes = emitter.EmitInstruction(encoded.front());
     REQUIRE((bytes.size() == 2));
     CHECK((bytes[0] == std::byte{0x0f}));
     CHECK((bytes[1] == std::byte{0x0b}));
}

TEST_CASE("Codegen exact string pooling accepts external literal storage", "[unit][ir][codegen]")
{
     std::array<char, 6> executable{'e', 'c', 'p', 'p', 's', '\0'};
     std::array<char, 11> source{'s', 'o', 'u', 'r', 'c', 'e', '.', 'c', 'p', 'p', '\0'};
     std::array<char*, 2> arguments{executable.data(), source.data()};
     ecpps::CompilerConfig config{2, arguments.data()};
     ecpps::codegen::AssemblyContext assembly{config};
     const std::array<ecpps::codegen::Byte, 5> value{u8'h', u8'e', u8'l', u8'l', u8'o'};

     const auto first = assembly.AddString(value);
     const auto duplicate = assembly.AddString(value);

     CHECK((first.indexInTable == duplicate.indexInTable));
     CHECK((first.offset == duplicate.offset));
     CHECK((assembly.GetString(first) == std::u8string_view{value.data(), value.size()}));
}

TEST_CASE("VInst DCE preserves volatile loads and stores", "[unit][ir][codegen][dce]")
{
     Fixture fixture{};
     ecpps::abi::api::Target target{};
     target.EnsureVRM();
     ecpps::codegen::ParsingContext volatileContext{ecpps::abi::ABI::Current()};
     volatileContext.target = &target;

     auto volatileStorage = MakeSSA(fixture.irAllocator, 0);
     const auto* volatileStoragePointer = volatileStorage.get();
     ecpps::ir::AllocationNode volatileAllocation{std::move(volatileStorage), Source};
     volatileContext.ParseAllocateNode(volatileAllocation);
     target.registerMap->ReferenceRegister(ecpps::ir::abstract::VirtualRegister{0});

     const ecpps::ir::MemoryAccess volatileAccess{.isVolatile = true, .alignment = 4};
     ecpps::ir::SSALoadNode volatileLoad{MakeSSA(fixture.irAllocator, 1), volatileStoragePointer, volatileAccess,
                                         Source};
     volatileContext.ParseLoadNode(volatileLoad);
     ecpps::optimisations::OptimiseVirtualInstructions(volatileContext.instructions, *target.registerMap, false, true);

     REQUIRE((volatileContext.instructions.size() == 1));
     REQUIRE((volatileContext.instructions[0].memoryEffect.has_value()));
     CHECK((volatileContext.instructions[0].memoryEffect->access.isVolatile));

     target.registerMap->Reset();
     ecpps::codegen::ParsingContext volatileStoreContext{ecpps::abi::ABI::Current()};
     volatileStoreContext.target = &target;
     auto storeStorage = MakeSSA(fixture.irAllocator, 0);
     const auto* storeStoragePointer = storeStorage.get();
     ecpps::ir::AllocationNode storeAllocation{std::move(storeStorage), Source};
     volatileStoreContext.ParseAllocateNode(storeAllocation);
     ecpps::ir::SSAStoreIntegerNode volatileStore{storeStoragePointer, 7, volatileAccess, Source};
     volatileStoreContext.ParseStoreIntNode(volatileStore);
     ecpps::optimisations::OptimiseVirtualInstructions(volatileStoreContext.instructions, *target.registerMap, false,
                                                       true);

     REQUIRE((volatileStoreContext.instructions.size() == 1));
     REQUIRE((volatileStoreContext.instructions[0].memoryEffect.has_value()));
     CHECK((volatileStoreContext.instructions[0].memoryEffect->isWrite));
     CHECK((volatileStoreContext.instructions[0].memoryEffect->access.isVolatile));
}
