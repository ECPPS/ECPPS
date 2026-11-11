#define CATCH_CONFIG_MAIN
#include <CodeGeneration/Emitters/x86_64/x86_64.h>
#include <CodeGeneration/PseudoAssembly.h>
#include <IRTestHelpers.h>
#include <Machine/Encoders/Backends/x86_64/Core/encoder.h>
#include <Machine/Encoders/Backends/x86_64/Platforms/Linux/Linux.h>
#include <catch_amalgamated.hpp>
#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

using namespace IRTestHelpers;

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
     auto nodes = fixture.Parse(std::move(ast));

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
