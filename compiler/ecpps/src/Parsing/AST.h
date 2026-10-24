#pragma once

#include <SBOVector.h>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "../Machine/ABI.h"
#include "../Shared/Diagnostics.h"
#include "../Shared/Error.h"
#include "ASTContext.h"
#include "Tokeniser.h"

namespace ecpps::ast
{
     constexpr std::size_t PrettyIndent = 5;

     class Node
     {
     public:
          explicit Node(Location location) : _location(location)
          {
          }
          virtual ~Node(void);

          [[nodiscard]] virtual std::string ToString(std::size_t indent) const = 0;

          [[nodiscard]] const Location& Source(void) const noexcept
          {
               return this->_location;
          }

     private:
          Location _location;
     };
     using ASTDeleter = BumpAllocator::Deleter<Node>;
     using NodePointer = std::unique_ptr<Node, ASTDeleter>;

     enum struct ConstantExpressionSpecifier : std::uint_fast8_t
     {
          None,
          Constexpr,
          Consteval,
          Constinit
     };
     enum struct StorageClassSpecifier : std::uint8_t
     {
          None = 0,
          Static = 1,
          ThreadLocal = 2,
          Extern = 4,
          Mutable = 8
     };
     constexpr bool operator&(StorageClassSpecifier a, StorageClassSpecifier b)
     {
          return (std::to_underlying(a) & std::to_underlying(b)) != 0;
     }
     constexpr void operator|=(StorageClassSpecifier& a, StorageClassSpecifier b)
     {
          a = static_cast<StorageClassSpecifier>(std::to_underlying(a) | std::to_underlying(b));
     }

     enum struct Operator : std::uint_fast8_t
     {
          Function,              // ()
          Subscript,             // []
          Ellipsis,              // ...
          QuestionMark,          // ?
          FullStop,              // .
          PointerToMemberObject, // .*
          Arrow,                 // ->
          PointerToMember,       // ->*
          Tilde,                 // ~
          Exclamation,           // !

          Plus,             // +
          Minus,            // -
          Asterisk,         // *
          Solidus,          // /
          Percent,          // %
          CircumflexAccent, // ^
          Ampersand,        // &
          VerticalLine,     // |
          Assignment,       // =
          PlusAssign,       // +=
          MinusAssign,      // -=
          MultiplyAssign,   // *=
          DivideAssign,     // /=
          ModuloAssign,     // %=
          XorAssign,        // ^=
          AndAssign,        // &=
          OrAssign,         // |=

          EqualsSign,       // =
          NotEqual,         // !=
          Less,             // <
          Greater,          // >
          LessEqual,        // <=
          GreaterEqual,     // >=
          Spaceship,        // <=>
          LogicalAnd,       // &&
          LogicalOr,        // ||
          LeftShift,        // <<
          RightShift,       // >>
          LeftShiftAssign,  // >>=
          RightShiftAssign, // <<=
          Increment,        // ++
          Decrement,        // --
          Comma             // ,
     };
     [[nodiscard]] constexpr std::string ToString(const Operator op) noexcept
     {
          switch (op)
          {
          case Operator::Function: return "()";
          case Operator::Subscript: return "[]";
          case Operator::Ellipsis: return "...";
          case Operator::QuestionMark: return "?";
          case Operator::FullStop: return ".";
          case Operator::PointerToMemberObject: return ".*";
          case Operator::Arrow: return "->";
          case Operator::PointerToMember: return "->*";
          case Operator::Tilde: return "~";
          case Operator::Exclamation: return "!";
          case Operator::Plus: return "+";
          case Operator::Minus: return "-";
          case Operator::Asterisk: return "*";
          case Operator::Solidus: return "/";
          case Operator::Percent: return "%";
          case Operator::CircumflexAccent: return "^";
          case Operator::Ampersand: return "&";
          case Operator::VerticalLine: return "|";
          case Operator::Assignment: return "=";
          case Operator::PlusAssign: return "+=";
          case Operator::MinusAssign: return "-=";
          case Operator::MultiplyAssign: return "*=";
          case Operator::DivideAssign: return "/=";
          case Operator::ModuloAssign: return "%=";
          case Operator::XorAssign: return "^=";
          case Operator::AndAssign: return "&=";
          case Operator::OrAssign: return "|=";
          case Operator::EqualsSign: return "==";
          case Operator::NotEqual: return "!=";
          case Operator::Less: return "<";
          case Operator::Greater: return ">";
          case Operator::LessEqual: return ">=";
          case Operator::GreaterEqual: return "<=";
          case Operator::Spaceship: return "<=>";
          case Operator::LogicalAnd: return "&&";
          case Operator::LogicalOr: return "||";
          case Operator::LeftShift: return "<<";
          case Operator::RightShift: return ">>";
          case Operator::LeftShiftAssign: return "<<=";
          case Operator::RightShiftAssign: return ">>=";
          case Operator::Increment: return "++";
          case Operator::Decrement: return "--";
          case Operator::Comma: return ",";
          default: return "";
          }
     }

     class AttributeNode : public Node
     {
     public:
          explicit AttributeNode(std::string name, SBOVector<Token> arguments, Location source)
              : Node(source), _name(std::move(name)), _arguments(std::move(arguments))
          {
          }
          [[nodiscard]] std::string ToString([[maybe_unused]] std::size_t indent) const override
          {
               std::string built = "[[" + this->_name;
               if (this->_arguments.Size() != 0)
               {
                    built += "(";

                    for (const auto& arg : this->_arguments)
                         built +=
                              (std::holds_alternative<std::string>(arg.value) ? std::get<std::string>(arg.value) : "") +
                              ", ";

                    built.pop_back();
                    built.pop_back();
                    built += ")";
               }
               return built + "]]";
          }
          [[nodiscard]] const std::string& Name(void) const noexcept
          {
               return this->_name;
          }
          [[nodiscard]] const SBOVector<Token>& Arguments(void) const noexcept
          {
               return this->_arguments;
          }

     private:
          std::string _name;
          SBOVector<Token> _arguments;
     };

     enum struct ExplicitThisSpecifier : bool
     {
          Yes = true,
          No = false
     };

     struct PtrOperator
     {
          enum struct Kind : std::uint8_t
          {
               Pointer,
               LValueReference,
               RValueReference,
               PointerToMember
          };

          Kind kind = Kind::Pointer;
          bool isConst = false;
          bool isVolatile = false;
          NodePointer nestedNameSpecifier{};
          std::vector<std::unique_ptr<AttributeNode, ecpps::ast::ASTDeleter>> attributes{};
          Location location;
     };
     enum class RefQualifier : std::uint8_t
     {
          None,
          LValue,
          RValue
     };

     struct NoexceptSpecifier
     {
          bool isPresent = false;
          NodePointer condition{};
     };
     struct ArrayBound
     {
          NodePointer size{}; // nullptr => unbounded: T[]
          std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>> attributes{};
          Location location;
     };

     struct FunctionParameter
     {
          SBOVector<std::unique_ptr<AttributeNode, ASTDeleter>> attributes{};
          ExplicitThisSpecifier explicitThis{};
          NodePointer type{};
          NodePointer name{};
          NodePointer defaultInitialiser{};
     };

     struct FunctionParameterList
     {
          std::vector<FunctionParameter> parameters{};
          [[nodiscard]] std::string ToString(void) const
          {
               using std::string_literals::operator""s;
               std::string built{};
               for (const auto& param : this->parameters)
               {
                    for (const auto& attr : param.attributes) built += "[["s + attr->ToString(0) + "]] ";
                    if (param.explicitThis == ExplicitThisSpecifier::Yes) built += "this ";
                    if (param.type != nullptr) built += param.type->ToString(0) + " ";
                    if (param.name != nullptr) built += param.name->ToString(0);
                    if (param.defaultInitialiser != nullptr) built += " = "s + param.defaultInitialiser->ToString(0);
                    built += ", ";
               }

               if (!built.empty()) // trailing comma
               {
                    built.pop_back();
                    built.pop_back();
               }
               return built;
          }
     };
     struct ParametersAndQualifiers
     {
          std::vector<FunctionParameter> parameters{};
          bool isVariadic = false;
          bool isConst = false;
          bool isVolatile = false;
          RefQualifier refQualifier = RefQualifier::None;
          NoexceptSpecifier noexceptSpecifier{};
          std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>> attributes{};
          NodePointer trailingReturnType{};
          Location location;
     };
     using DeclaratorSuffix = std::variant<ArrayBound, ParametersAndQualifiers>;

     enum struct DeclaratorKind : std::uint8_t
     {
          Plain,
          Pointer,
          Array,
          Function
     };

     struct Declarator
     {
          std::vector<PtrOperator> ptrOperators{};
          std::unique_ptr<Declarator> nested{};
          NodePointer declaredIdExpression{};
          bool isParameterPack = false;
          std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>> attributes{};
          std::vector<DeclaratorSuffix> suffixes{};
          Location location;

          [[nodiscard]] DeclaratorKind Kind(void) const noexcept
          {
               if (nested != nullptr)
               {
                    const auto inner = nested->Kind();
                    if (inner != DeclaratorKind::Plain) return inner;
               }
               if (!suffixes.empty())
                    return std::holds_alternative<ParametersAndQualifiers>(suffixes.front()) ? DeclaratorKind::Function
                                                                                             : DeclaratorKind::Array;
               if (!ptrOperators.empty()) return DeclaratorKind::Pointer;
               return DeclaratorKind::Plain;
          }

          [[nodiscard]] bool IsFunction(void) const noexcept
          {
               return Kind() == DeclaratorKind::Function;
          }
     };
     enum struct FunctionBodyKind : std::uint8_t
     {
          None,
          Block,     // { ... }
          Defaulted, // = default
          Deleted    // = delete
     };

     struct FunctionSignature
     {
          NodePointer type{};
          bool isFriend{};
          bool isInline{};
          bool isExtern{};
          std::optional<std::string> externOptional = std::nullopt;
          ConstantExpressionSpecifier constexprSpecifier{};
          SBOVector<std::unique_ptr<AttributeNode, ASTDeleter>> attributes{};
          NodePointer name;
          abi::CallingConventionName callingConvention;
          FunctionParameterList parameters{};
          // TODO: Trailing return types

          [[nodiscard]] std::string ToString(void) const
          {
               using std::string_literals::operator""s;
               std::string built{};
               for (const auto& attr : this->attributes)
                    built += (attr == nullptr ? "[[__unknown]]" : attr->ToString(0)) + " ";

               if (this->isFriend) built += "friend ";
               if (this->isInline) built += "inline ";
               const auto m = this->externOptional.has_value();
               if (this->isExtern) built += "extern " + (m ? ("\""s + this->externOptional.value() + "\" ") : "");
               switch (this->constexprSpecifier)
               {
               case ConstantExpressionSpecifier::Constexpr: built += "constexpr "; break;
               case ConstantExpressionSpecifier::Consteval: built += "consteval "; break;
               case ConstantExpressionSpecifier::Constinit: built += "constinit "; break;
               case ConstantExpressionSpecifier::None:
               default: break;
               }

               built += this->type == nullptr ? "__unknown" : this->type->ToString(0) + " ";
               built += ::ToString(this->callingConvention) + " ";
               built += this->name == nullptr ? "__unknown" : this->name->ToString(0);
               built += "(";
               built += this->parameters.ToString();
               built += ")";

               return built;
          }

          explicit FunctionSignature(NodePointer type, bool isFriend, bool isInline,
                                     ConstantExpressionSpecifier constexprSpecifier,
                                     SBOVector<std::unique_ptr<AttributeNode, ASTDeleter>> attributes, NodePointer name,
                                     const abi::CallingConventionName callingConvention)
              : type(std::move(type)), isFriend(isFriend), isInline(isInline), constexprSpecifier(constexprSpecifier),
                attributes(std::move(attributes)), name(std::move(name)), callingConvention(callingConvention)
          {
          }
     };

     class FunctionDeclarationNode final : public Node
     {
     public:
          explicit FunctionDeclarationNode(FunctionSignature signature, Location source)
              : Node(source), _signature(std::move(signature))
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               const std::string built = std::string(indent * PrettyIndent, ' ') + this->_signature.ToString();
               return built + ";";
          }

          [[nodiscard]] const FunctionSignature& Signature(void) const noexcept
          {
               return this->_signature;
          }

     private:
          FunctionSignature _signature;
     };

     class FunctionDefinitionNode final : public Node
     {
     public:
          explicit FunctionDefinitionNode(FunctionSignature signature, SBOVector<NodePointer> body, Location source)
              : Node(source), _signature(std::move(signature)), _body(std::move(body))
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built = std::string(indent * PrettyIndent, ' ') + this->_signature.ToString();
               built += "\n" + std::string(indent * PrettyIndent, ' ') + "{";

               for (const auto& line : this->_body)
               {
                    if (line == nullptr) continue;
                    built += "\n" + line->ToString(indent + 1) + ";";
               }

               return built + "\n" + std::string(indent * PrettyIndent, ' ') + "}";
          }

          [[nodiscard]] const FunctionSignature& Signature(void) const noexcept
          {
               return this->_signature;
          }
          [[nodiscard]] const SBOVector<NodePointer>& Body(void) const noexcept
          {
               return this->_body;
          }

     private:
          FunctionSignature _signature;
          SBOVector<NodePointer> _body;
     };

     // Not a template
     class CallOperatorNode final : public Node
     {
     public:
          explicit CallOperatorNode(NodePointer function, SBOVector<NodePointer> arguments, Location source)
              : Node(source), _function(std::move(function)), _arguments(std::move(arguments))
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string arguments{};
               for (const auto& arg : this->_arguments)
                    arguments += (arg == nullptr ? "__unknown" : arg->ToString(0)) + ", ";
               if (!arguments.empty()) // trailing comma
               {
                    arguments.pop_back();
                    arguments.pop_back();
               }

               return std::string(indent * PrettyIndent, ' ') + "(" +
                      (this->_function == nullptr ? "__unknown" : this->_function->ToString(0)) + ")(" + arguments +
                      ")";
          }

          [[nodiscard]] const NodePointer& Function(void) const noexcept
          {
               return this->_function;
          }
          [[nodiscard]] const SBOVector<NodePointer>& Arguments(void) const noexcept
          {
               return this->_arguments;
          }

     private:
          NodePointer _function;
          SBOVector<NodePointer> _arguments;
     };

     class IdentifierNode;

     class ThisNode final : public Node
     {
     public:
          explicit ThisNode(Location source) : Node(source)
          {
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "this";
          }
     };

     enum struct UnaryOperatorType : std::uint_fast8_t
     {
          Prefix,
          Postfix,
     };

     class UnaryOperatorNode final : public Node
     {
     public:
          explicit UnaryOperatorNode(Operator value, NodePointer operand, const UnaryOperatorType type, Location source)
              : Node(source), _value(value), _operand(std::move(operand)), _type(type)
          {
          }
          [[nodiscard]] const Operator& Value(void) const noexcept
          {
               return this->_value;
          }
          [[nodiscard]] const NodePointer& Operand(void) const noexcept
          {
               return this->_operand;
          }
          [[nodiscard]] UnaryOperatorType UnaryType(void) const noexcept
          {
               return this->_type;
          }
          [[nodiscard]] std::string ToString([[maybe_unused]] const std::size_t indent) const override
          {
               return this->_type == UnaryOperatorType::Postfix
                           ? (this->_operand->ToString(0) + ecpps::ast::ToString(this->_value))
                           : (ecpps::ast::ToString(this->_value) + this->_operand->ToString(0));
          }

     private:
          Operator _value;
          NodePointer _operand;
          UnaryOperatorType _type;
     };

     class BinaryOperatorNode final : public Node
     {
     public:
          explicit BinaryOperatorNode(NodePointer left, Operator value, NodePointer right, Location source)
              : Node(source), _left(std::move(left)), _value(value), _right(std::move(right))
          {
          }
          [[nodiscard]] const Operator& Value(void) const noexcept
          {
               return this->_value;
          }
          [[nodiscard]] const NodePointer& Left(void) const noexcept
          {
               return this->_left;
          }
          [[nodiscard]] const NodePointer& Right(void) const noexcept
          {
               return this->_right;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "(" + this->_left->ToString(0) + " " +
                      ecpps::ast::ToString(this->_value) + " " + this->_right->ToString(0) + ")";
          }

     private:
          NodePointer _left;
          Operator _value;
          NodePointer _right;
     };
     class IfStatementNode final : public Node
     {
     public:
          explicit IfStatementNode(NodePointer condition, std::vector<NodePointer> body,
                                   std::vector<NodePointer> elseBody, Location source)
              : Node(source), _condition(std::move(condition)), _body(std::move(body)), _elseBody(std::move(elseBody))
          {
          }
          [[nodiscard]] const NodePointer& Condition(void) const noexcept
          {
               return this->_condition;
          }
          [[nodiscard]] const std::vector<NodePointer>& Body(void) const noexcept
          {
               return this->_body;
          }
          [[nodiscard]] const std::vector<NodePointer>& ElseBody(void) const noexcept
          {
               return this->_elseBody;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built{};
               for (const auto& node : this->_body) built += node->ToString(indent + 1) + ";\n";
               built = std::string(indent * PrettyIndent, ' ') + "if (" + this->_condition->ToString(0) + ")\n" +
                       std::string(indent * PrettyIndent, ' ') + "{\n" + built +
                       std::string(indent * PrettyIndent, ' ') + "}";
               if (this->_elseBody.empty()) return built;

               std::string elseBuilt{};
               for (const auto& node : this->_elseBody) elseBuilt += node->ToString(indent + 1) + ";\n";
               return built + " else {\n" + elseBuilt + std::string(indent * PrettyIndent, ' ') + "}";
          }

     private:
          NodePointer _condition;
          std::vector<NodePointer> _body;
          std::vector<NodePointer> _elseBody;
     };
     class WhileStatementNode final : public Node
     {
     public:
          explicit WhileStatementNode(NodePointer condition, std::vector<NodePointer> body, Location source)
              : Node(source), _condition(std::move(condition)), _body(std::move(body))
          {
          }
          [[nodiscard]] const NodePointer& Condition(void) const noexcept
          {
               return this->_condition;
          }
          [[nodiscard]] const std::vector<NodePointer>& Body(void) const noexcept
          {
               return this->_body;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built{};
               for (const auto& node : this->_body) built += node->ToString(indent + 1) + ";\n";
               built = std::string(indent * PrettyIndent, ' ') + "while (" + this->_condition->ToString(0) + ")\n" +
                       std::string(indent * PrettyIndent, ' ') + "{\n" + built +
                       std::string(indent * PrettyIndent, ' ') + "}";
               return built;
          }

     private:
          NodePointer _condition;
          std::vector<NodePointer> _body;
     };
     class ForStatementNode final : public Node
     {
     public:
          explicit ForStatementNode(NodePointer init, NodePointer condition, NodePointer expression,
                                    std::vector<NodePointer> body, Location source)
              : Node(source), _init(std::move(init)), _condition(std::move(condition)),
                _expression(std::move(expression)), _body(std::move(body))
          {
          }
          [[nodiscard]] const NodePointer& InitStatement(void) const noexcept
          {
               return this->_init;
          }
          [[nodiscard]] const NodePointer& Condition(void) const noexcept
          {
               return this->_condition;
          }
          [[nodiscard]] const NodePointer& Expression(void) const noexcept
          {
               return this->_expression;
          }
          [[nodiscard]] const std::vector<NodePointer>& Body(void) const noexcept
          {
               return this->_body;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built{};
               for (const auto& node : this->_body) built += node->ToString(indent + 1) + ";\n";
               built = std::string(indent * PrettyIndent, ' ') + "for (" +
                       (this->_init == nullptr ? ";" : this->_init->ToString(0)) + "; " +
                       (this->_condition == nullptr ? "" : this->_condition->ToString(0)) + "; " +
                       (this->_expression == nullptr ? "" : this->_expression->ToString(0)) + ")\n" +
                       std::string(indent * PrettyIndent, ' ') + "{\n" + built +
                       std::string(indent * PrettyIndent, ' ') + "}";
               return built;
          }

     private:
          NodePointer _init;
          NodePointer _condition;
          NodePointer _expression;
          std::vector<NodePointer> _body;
     };
     class DoWhileStatementNode final : public Node
     {
     public:
          explicit DoWhileStatementNode(NodePointer condition, std::vector<NodePointer> body, Location source)
              : Node(source), _condition(std::move(condition)), _body(std::move(body))
          {
          }
          [[nodiscard]] const NodePointer& Condition(void) const noexcept
          {
               return this->_condition;
          }
          [[nodiscard]] const std::vector<NodePointer>& Body(void) const noexcept
          {
               return this->_body;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built{};
               for (const auto& node : this->_body) built += node->ToString(indent + 1) + ";\n";
               built = std::string(indent * PrettyIndent, ' ') + "do \n" + std::string(indent * PrettyIndent, ' ') +
                       "{\n" + built + std::string(indent * PrettyIndent, ' ') + "} while (" +
                       this->_condition->ToString(0) + ")\n";
               return built;
          }

     private:
          NodePointer _condition;
          std::vector<NodePointer> _body;
     };
     class LabelNode final : public Node
     {
     public:
          explicit LabelNode(std::string name, Location source) : Node(source), _name(std::move(name))
          {
          }
          [[nodiscard]] const std::string& Name(void) const noexcept
          {
               return this->_name;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + this->_name + ":";
          }

     private:
          std::string _name;
     };
     class GotoNode final : public Node
     {
     public:
          explicit GotoNode(std::string name, Location source) : Node(source), _name(std::move(name))
          {
          }
          [[nodiscard]] const std::string& Name(void) const noexcept
          {
               return this->_name;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "goto " + this->_name;
          }

     private:
          std::string _name;
     };

     class ReturnNode final : public Node
     {
     public:
          explicit ReturnNode(NodePointer expression, Location source)
              : Node(source), _expression(std::move(expression))
          {
          }

          [[nodiscard]] const NodePointer& Value(void) const noexcept
          {
               return this->_expression;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               if (this->_expression == nullptr) return std::string(indent * PrettyIndent, ' ') + "return";
               return std::string(indent * PrettyIndent, ' ') + "return " + this->_expression->ToString(0);
          }

     private:
          NodePointer _expression;
     };
     class BreakNode final : public Node
     {
     public:
          explicit BreakNode(Location source) : Node(source)
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "break";
          }
     };
     class ContinueNode final : public Node
     {
     public:
          explicit ContinueNode(Location source) : Node(source)
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "continue";
          }
     };

     class TypeAliasNode final : public Node
     {
     public:
          explicit TypeAliasNode(NodePointer aliasName, NodePointer targetType, Location source)
              : Node(source), _aliasName(std::move(aliasName)), _targetType(std::move(targetType))
          {
          }

          [[nodiscard]] const NodePointer& AliasName(void) const noexcept
          {
               return this->_aliasName;
          }
          [[nodiscard]] const NodePointer& TargetType(void) const noexcept
          {
               return this->_targetType;
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "using " + this->_aliasName->ToString(0) + " = " +
                      this->_targetType->ToString(0);
          }

     private:
          NodePointer _aliasName;
          NodePointer _targetType;
     };

     enum struct InitialisationType : std::uint8_t
     {
          ParenthesisedExpressionList,
          BracedInitialiser,
          EqualInitialiser,
          Error,
          Default
     };

     class VariableDeclarationNode final : public Node
     {
     public:
          struct Declarator
          {
               NodePointer name;
               NodePointer initialiser;
               InitialisationType initialiserType;
               std::vector<NodePointer> arrayLevels;
               explicit Declarator(NodePointer name, InitialisationType initialiserType,
                                   NodePointer initialiser = nullptr, std::vector<NodePointer> arrayLevels = {})
                   : name(std::move(name)), initialiser(std::move(initialiser)), initialiserType(initialiserType),
                     arrayLevels(std::move(arrayLevels))
               {
               }
          };

          struct Flags
          {
               bool isTypedef = false;
               bool isFriend = false;
               ConstantExpressionSpecifier constexprSpecifier = ConstantExpressionSpecifier::None;
               bool isInline = false;
               StorageClassSpecifier storageClassSpecifier = StorageClassSpecifier::None;
          };

          VariableDeclarationNode(NodePointer type, std::vector<Declarator> declarators, Flags flags,
                                  std::optional<NodePointer> explicitSpecifier, Location source)
              : Node(source), _type(std::move(type)), _declarators(std::move(declarators)), _flags(flags),
                _explicitSpecifier(std::move(explicitSpecifier))
          {
          }

          [[nodiscard]] const NodePointer& Type(void) const noexcept
          {
               return this->_type;
          }
          [[nodiscard]] const std::vector<Declarator>& Declarators(void) const noexcept
          {
               return this->_declarators;
          }
          [[nodiscard]] const Flags& GetFlags(void) const noexcept
          {
               return this->_flags;
          }
          [[nodiscard]] const std::optional<NodePointer>& ExplicitSpecifier(void) const noexcept
          {
               return this->_explicitSpecifier;
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built(indent * PrettyIndent, ' ');
               // add flags
               if (this->_flags.isTypedef) built += "typedef ";
               if (this->_flags.constexprSpecifier == ConstantExpressionSpecifier::Constexpr) built += "constexpr ";
               else if (this->_flags.constexprSpecifier == ConstantExpressionSpecifier::Consteval)
                    built += "consteval ";
               else if (this->_flags.constexprSpecifier == ConstantExpressionSpecifier::Constinit)
                    built += "constinit ";
               if (this->_flags.isInline) built += "inline ";
               if (this->_flags.storageClassSpecifier & StorageClassSpecifier::Static) built += "static ";
               if (this->_flags.storageClassSpecifier & StorageClassSpecifier::ThreadLocal) built += "thread_local ";
               if (this->_flags.storageClassSpecifier & StorageClassSpecifier::Extern) built += "extern ";
               if (this->_flags.storageClassSpecifier & StorageClassSpecifier::Mutable) built += "mutable ";
               if (this->_flags.isFriend) built += "friend ";
               if (this->_explicitSpecifier) built += "explicit ";

               built += this->_type->ToString(0) + " ";

               for (size_t i = 0; i < _declarators.size(); i++)
               {
                    const auto& declarator = this->_declarators[i];
                    built += declarator.name->ToString(0);
                    for (const auto& level : declarator.arrayLevels)
                         built += std::format("[{}]", level == nullptr ? "" : level->ToString(0));
                    if (declarator.initialiser) built += " = " + declarator.initialiser->ToString(0);
                    if (i + 1 < this->_declarators.size()) built += ", ";
               }

               return built;
          }

     private:
          NodePointer _type;
          std::vector<Declarator> _declarators;
          Flags _flags;
          std::optional<NodePointer> _explicitSpecifier;
     };

     class BooleanLiteralNode final : public Node
     {
     public:
          explicit BooleanLiteralNode(const bool value, Location source) : Node(source), _value(value)
          {
          }
          [[nodiscard]] bool Value(void) const noexcept
          {
               return this->_value;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + (this->_value ? "true" : "false");
          }

     private:
          bool _value;
     };
     class StringLiteralNode final : public Node
     {
     public:
          explicit StringLiteralNode(std::string value, Location source) : Node(source), _value(std::move(value))
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "\"" + this->_value + "\"";
          }
          [[nodiscard]] const std::string& Value(void) const noexcept
          {
               return this->_value;
          }

     private:
          std::string _value;
     };

     class IntegerLiteralNode final : public Node
     {
     public:
          explicit IntegerLiteralNode(const unsigned long long value, Location source) : Node(source), _value(value)
          {
          }
          [[nodiscard]] unsigned long long Value(void) const noexcept
          {
               return this->_value;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + std::to_string(this->_value);
          }

     private:
          unsigned long long _value;
     };

     class CharacterLiteralNode final : public Node
     {
     public:
          explicit CharacterLiteralNode(const char value, Location source) : Node(source), _value(value)
          {
          }
          [[nodiscard]] char Value(void) const noexcept
          {
               return this->_value;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + std::to_string(this->_value);
          }

     private:
          char _value;
     };

     class GlobalScopeNode final : public Node
     {
     public:
          explicit GlobalScopeNode(Location source) : Node(source)
          {
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' '); // NOLINT
          }
     };

     class SizeOfNode final : public Node
     {
     public:
          explicit SizeOfNode(NodePointer node, bool canBeTypeId, Location source)
              : Node(source), _node(std::move(node)), _canBeTypeId(canBeTypeId)
          {
          }
          [[nodiscard]] const NodePointer& Value(void) const noexcept
          {
               return this->_node;
          }
          [[nodiscard]] NodePointer&& Value(void) noexcept
          {
               return std::move(this->_node);
          }
          [[nodiscard]] bool CanBeTypeId(void) const noexcept
          {
               return this->_canBeTypeId;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "sizeof(" +
                      (this->_node == nullptr ? "__unknown" : this->_node->ToString(0)) + ")";
          }

     private:
          NodePointer _node;
          bool _canBeTypeId;
     };

     class AlignOfNode final : public Node
     {
     public:
          explicit AlignOfNode(NodePointer node, bool canBeTypeId, Location source)
              : Node(source), _node(std::move(node)), _canBeTypeId(canBeTypeId)
          {
          }
          [[nodiscard]] const NodePointer& Value(void) const noexcept
          {
               return this->_node;
          }
          [[nodiscard]] NodePointer&& Value(void) noexcept
          {
               return std::move(this->_node);
          }
          [[nodiscard]] bool CanBeTypeId(void) const noexcept
          {
               return this->_canBeTypeId;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "alignof(" +
                      (this->_node == nullptr ? "__unknown" : this->_node->ToString(0)) + ")";
          }

     private:
          NodePointer _node;
          bool _canBeTypeId;
     };

     class InitialiserListNode final : public Node
     {
     public:
          explicit InitialiserListNode(std::vector<NodePointer> initialisers, Location source)
              : Node(source), _initialisers(std::move(initialisers))
          {
          }
          [[nodiscard]] const std::vector<NodePointer>& Initialisers(void) const noexcept
          {
               return this->_initialisers;
          }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built = std::string(indent * PrettyIndent, ' ') + "{";
               for (const auto& init : this->_initialisers)
                    built += (init == nullptr ? std::string((indent * PrettyIndent) + PrettyIndent, ' ') + "__unknown"
                                              : init->ToString(indent + 1)) +
                             ",";
               if (!this->_initialisers.empty()) built.pop_back(); // trailing comma
               built += "}";
               return built;
          }

     private:
          std::vector<NodePointer> _initialisers;
     };

     /// <summary>
     /// Keep both operands for error recovery.
     /// </summary>
     struct ASTExpected
     {
          NodePointer value{};
          std::vector<diagnostics::DiagnosticsMessage> diagnostics{};
          bool HasValue(void) const noexcept
          {
               return this->value != nullptr;
          }
          bool WasSuccessful(void) const noexcept
          {
               return this->_wasSuccessful;
          }
          explicit ASTExpected(NodePointer value) : value(std::move(value)), _wasSuccessful(true)
          {
          }
          explicit ASTExpected(NodePointer value, std::vector<diagnostics::DiagnosticsMessage> diagnostics)
              : value(std::move(value)), diagnostics(std::move(diagnostics)), _wasSuccessful(false)
          {
          }

     private:
          bool _wasSuccessful;
     };

     struct FunctionSpecifier
     {
          bool isVirtual = false;
          std::optional<NodePointer> explicitSpecifier = std::nullopt;
     };

     struct DeclSpecifierSequence
     {
          bool isFriend = false;
          bool isTypedef = false;
          ConstantExpressionSpecifier constexprSpecifier = ConstantExpressionSpecifier::None;
          bool isInline = false;
          StorageClassSpecifier storageSpecifier = StorageClassSpecifier::None;
          NodePointer definingTypeSpecifier = nullptr;
          FunctionSpecifier functionSpecifier{};
     };

     struct MemberDeclarator : Declarator
     {
          bool isBitField = false;
          NodePointer bitFieldWidth{};

          InitialisationType initialisationType = InitialisationType::Default;
          NodePointer initialiser{};

          bool isOverride = false;
          bool isFinal = false;
          bool isPure = false;

          FunctionBodyKind functionBody = FunctionBodyKind::None;
          std::vector<NodePointer> body{};
     };

     class MemberDeclarationNode final : public Node
     {
     public:
          MemberDeclarationNode(
               std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>> attributes,
               DeclSpecifierSequence specifiers, std::vector<MemberDeclarator> declarators, Location source)
              : Node(source), _attributes(std::move(attributes)), _specifiers(std::move(specifiers)),
                _declarators(std::move(declarators))
          {
          }

          [[nodiscard]] const std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>>&
          Attributes(void) const noexcept
          {
               return _attributes;
          }
          [[nodiscard]] const DeclSpecifierSequence& Specifiers(void) const noexcept
          {
               return _specifiers;
          }
          [[nodiscard]] const std::vector<MemberDeclarator>& Declarators(void) const noexcept
          {
               return _declarators;
          }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               std::string result;

               for (const auto& attribute : _attributes) result += attribute->ToString(indent) + " ";

               if (_specifiers.isFriend) result += "friend ";
               if (_specifiers.isTypedef) result += "typedef ";

               switch (_specifiers.constexprSpecifier)
               {
               case ConstantExpressionSpecifier::Constexpr: result += "constexpr "; break;
               case ConstantExpressionSpecifier::Consteval: result += "consteval "; break;
               case ConstantExpressionSpecifier::Constinit: result += "constinit "; break;
               default: break;
               }

               if (_specifiers.isInline) result += "inline ";
               if (_specifiers.storageSpecifier & StorageClassSpecifier::Static) result += "static ";
               if (_specifiers.storageSpecifier & StorageClassSpecifier::ThreadLocal) result += "thread_local ";
               if (_specifiers.storageSpecifier & StorageClassSpecifier::Extern) result += "extern ";
               if (_specifiers.storageSpecifier & StorageClassSpecifier::Mutable) result += "mutable ";
               if (_specifiers.functionSpecifier.isVirtual) result += "virtual ";

               if (_specifiers.functionSpecifier.explicitSpecifier.has_value())
               {
                    const auto& condition = *_specifiers.functionSpecifier.explicitSpecifier;
                    result += condition == nullptr ? "explicit " : "explicit(" + condition->ToString(indent) + ") ";
               }

               if (_specifiers.definingTypeSpecifier != nullptr)
                    result += _specifiers.definingTypeSpecifier->ToString(indent) + " ";

               if (_declarators.empty() && !result.empty() && result.back() == ' ') result.pop_back();

               for (std::size_t index = 0; index < _declarators.size(); ++index)
               {
                    const auto& member = _declarators[index];
                    if (index != 0) result += ", ";

                    std::vector<const Declarator*> chain{};
                    for (const Declarator* level = &member; level != nullptr; level = level->nested.get())
                         chain.push_back(level);

                    std::string text;
                    for (auto& it : std::ranges::reverse_view(chain))
                    {
                         const Declarator& level = *it;
                         std::string current;

                         for (const auto& op : level.ptrOperators)
                         {
                              switch (op.kind)
                              {
                              case PtrOperator::Kind::Pointer: current += "*"; break;
                              case PtrOperator::Kind::LValueReference: current += "&"; break;
                              case PtrOperator::Kind::RValueReference: current += "&&"; break;
                              case PtrOperator::Kind::PointerToMember:
                                   current += op.nestedNameSpecifier->ToString(indent) + "::*";
                                   break;
                              }
                              for (const auto& attribute : op.attributes) current += " " + attribute->ToString(indent);
                              if (op.isConst) current += " const";
                              if (op.isVolatile) current += " volatile";
                              if (op.isConst || op.isVolatile || !op.attributes.empty()) current += " ";
                         }

                         if (level.nested != nullptr) current += "(" + text + ")";
                         else
                         {
                              if (level.isParameterPack) current += "...";
                              current += level.declaredIdExpression->ToString(indent);
                         }

                         for (const auto& attribute : level.attributes) current += " " + attribute->ToString(indent);

                         for (const auto& suffix : level.suffixes)
                         {
                              if (const auto* array = std::get_if<ArrayBound>(&suffix))
                              {
                                   current += "[";
                                   if (array->size != nullptr) current += array->size->ToString(indent);
                                   current += "]";
                                   for (const auto& attribute : array->attributes)
                                        current += " " + attribute->ToString(indent);
                                   continue;
                              }

                              const auto& function = std::get<ParametersAndQualifiers>(suffix);
                              current += "(";
                              for (std::size_t parameterIndex = 0; parameterIndex < function.parameters.size();
                                   ++parameterIndex)
                              {
                                   const auto& parameter = function.parameters[parameterIndex];
                                   if (parameterIndex != 0) current += ", ";
                                   if (parameter.type != nullptr) current += parameter.type->ToString(indent);
                                   if (parameter.name != nullptr) current += " " + parameter.name->ToString(indent);
                                   if (parameter.defaultInitialiser != nullptr)
                                        current += " = " + parameter.defaultInitialiser->ToString(indent);
                              }
                              if (function.isVariadic) current += function.parameters.empty() ? "..." : ", ...";
                              current += ")";

                              if (function.isConst) current += " const";
                              if (function.isVolatile) current += " volatile";
                              if (function.refQualifier == RefQualifier::LValue) current += " &";
                              if (function.refQualifier == RefQualifier::RValue) current += " &&";
                              if (function.noexceptSpecifier.isPresent)
                              {
                                   current += " noexcept";
                                   if (function.noexceptSpecifier.condition != nullptr)
                                        current += "(" + function.noexceptSpecifier.condition->ToString(indent) + ")";
                              }
                              for (const auto& attribute : function.attributes)
                                   current += " " + attribute->ToString(indent);
                              if (function.trailingReturnType != nullptr)
                                   current += " -> " + function.trailingReturnType->ToString(indent);
                         }

                         text = std::move(current);
                    }

                    if (member.isBitField)
                    {
                         if (!text.empty()) text += " ";
                         text += ": " + member.bitFieldWidth->ToString(indent);
                    }
                    if (member.isOverride) text += " override";
                    if (member.isFinal) text += " final";
                    if (member.isPure) text += " = 0";
                    if (member.functionBody == FunctionBodyKind::Defaulted) text += " = default";
                    if (member.functionBody == FunctionBodyKind::Deleted) text += " = delete";
                    if (member.functionBody == FunctionBodyKind::Block)
                    {
                         text += " {";
                         for (const auto& statement : member.body)
                              if (statement != nullptr) text += " " + statement->ToString(indent);
                         text += member.body.empty() ? "}" : " }";
                    }
                    if (member.initialiser != nullptr)
                    {
                         if (member.initialisationType == InitialisationType::EqualInitialiser) text += " = ";
                         else
                              text += " ";
                         text += member.initialiser->ToString(indent);
                    }

                    result += text;
               }

               const bool endsWithFunctionBody =
                    _declarators.size() == 1 && _declarators.front().functionBody == FunctionBodyKind::Block;
               if (!endsWithFunctionBody) result += ";";

               return result;
          }

     private:
          std::vector<std::unique_ptr<ecpps::ast::AttributeNode, ecpps::ast::ASTDeleter>> _attributes;
          DeclSpecifierSequence _specifiers;
          std::vector<MemberDeclarator> _declarators;
     };

     class AST
     {
     public:
          explicit AST(std::vector<Token> tokens, Diagnostics& diagnostics)
              : _tokens(std::move(tokens)), _diagnostics(std::ref(diagnostics))
          {
          }
          [[nodiscard]] std::vector<NodePointer> Parse(ASTContext& context);

     private:
          std::vector<Token> _tokens;
          std::size_t _position{};
          std::reference_wrapper<Diagnostics> _diagnostics;

          [[nodiscard]] bool AtEnd(void) const
          {
               return this->_position >= this->_tokens.size();
          }
          [[nodiscard]] const Token& Peek(const std::ptrdiff_t offset = 0) const noexcept
          {
               return this->_tokens[this->_position + static_cast<std::size_t>(offset)];
          }
          [[nodiscard]] Token& Peek(const std::ptrdiff_t offset = 0)
          {
               return this->_tokens[this->_position + static_cast<std::size_t>(offset)];
          }
          void Advance(void) noexcept
          {
               this->_position++;
          }
          void Retreat(void) noexcept
          {
               this->_position--;
          }
          [[nodiscard]] std::size_t CurrentPosition(void) const noexcept
          {
               return this->_position;
          }
          void SetPosition(const std::size_t newPosition) noexcept
          {
               this->_position = newPosition;
          }
          [[nodiscard]] bool Match(const TokenType type) noexcept
          {
               if (Peek().type != type) return false;
               Advance();
               return true;
          }

          // General
          std::unique_ptr<IdentifierNode, ASTDeleter> ParseIdentifier(ASTContext& context);
          NodePointer ParseSimpleTypeSpecifier(ASTContext& context, bool isConst = false, bool isVolatile = false);
          ASTExpected ParseTypeId(ASTContext& context);

          // Declarations
          NodePointer ParseDeclaration(ASTContext& context);
          NodePointer ParseBlockDeclaration(ASTContext& context);
          NodePointer ParseClassDeclaration(ASTContext& context, bool isConst, bool isVolatile);
          NodePointer ParseNameDeclaration(ASTContext& context);
          NodePointer ParseFunctionDefinition(ASTContext& context);
          NodePointer ParseMemberSpecification(ASTContext& context);
          NodePointer ParseMemberDeclaration(ASTContext& context);
          bool IsDeclarationStart(ASTContext& context);

          NodePointer ParseInitialiserClause(ASTContext& context);
          NodePointer ParseInitialiserList(ASTContext& context);
          std::tuple<NodePointer, InitialisationType> ParseInitialiser(ASTContext& context);

          [[nodiscard]] DeclSpecifierSequence ParseDeclSpecifiers(ASTContext& context);
          [[nodiscard]] std::optional<MemberDeclarator> ParseMemberDeclarator(ASTContext& context);
          [[nodiscard]] std::vector<MemberDeclarator> ParseMemberDeclarators(ASTContext& context);

          // simple-declaration
          NodePointer ParseSimpleDeclaration(ASTContext& context);               // simple-declaration
          static NodePointer TryParseDeclSpecifier(ASTContext& context);         // decl-specifier
          static NodePointer TryParseDefiningTypeSpecifier(ASTContext& context); // defining-type-specifier
          static NodePointer ParseInitDeclarator(ASTContext& context);           // init-declarator
          static NodePointer TryParseDeclarator(ASTContext& context);            // declarator
          static NodePointer TryParsePtrDeclarator(ASTContext& context);         // ptr-declarator
          static NodePointer TryParseNoPtrDeclarator(ASTContext& context);       // no-ptr-declarator

          // Expressions
          [[nodiscard]] NodePointer ParsePrimaryExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseIdExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParsePostfixExpresssion(ASTContext& context);
          [[nodiscard]] NodePointer ParseUnaryExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseCastExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParsePmExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseMultiplicativeExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseAdditiveExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseShiftExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseCompareExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseRelationalExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseEqualityExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseBinaryAndExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseBinaryExclusiveOrExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseBinaryInclusiveOrExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseLogicalAndExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseLogicalOrExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseConditionalExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseAssignmentExpression(ASTContext& context);
          [[nodiscard]] NodePointer ParseExpression(ASTContext& context);

          [[nodiscard]] NodePointer ParseCondition(ASTContext& context);

          // Statements
          NodePointer ParseStatement(ASTContext& context);
          NodePointer ParseIfStatement(ASTContext& context);
          NodePointer ParseWhileStatement(ASTContext& context);
          NodePointer ParseForStatement(ASTContext& context);
          NodePointer ParseDoWhileStatement(ASTContext& context);
          NodePointer ParseDeclarationStatement(ASTContext& context)
          {
               return ParseBlockDeclaration(context);
          }
          NodePointer ParseExpressionStatement(ASTContext& context);

          // Helpers (parse sub-components)
          FunctionParameter ParseFunctionParameter(ASTContext& context);

          void SynchroniseMember(void);
          std::vector<std::unique_ptr<AttributeNode, ASTDeleter>> ParseAttributeSpecifierSeq(ASTContext& context);
          void ParseCvQualifierSeq(bool& isConst, bool& isVolatile);
          std::vector<ecpps::ast::PtrOperator> ParsePtrOperators(ASTContext& context);
          NodePointer ParseDeclaratorId(ASTContext& context);
          std::optional<ParametersAndQualifiers> ParseParametersAndQualifiers(ASTContext& context);
          std::optional<Declarator> ParseDeclarator(ASTContext& context);
     };
} // namespace ecpps::ast
