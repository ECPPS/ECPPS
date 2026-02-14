#pragma once

#include <SBOVector.h>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
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
          explicit Node(Location location) : _location(location) {}
          virtual ~Node(void) = default;

          [[nodiscard]] virtual std::string ToString(std::size_t indent) const = 0;

          [[nodiscard]] const Location& Source(void) const noexcept { return this->_location; }

     private:
          Location _location;
     };
     using NodePointer = std::unique_ptr<Node, ASTContext::Deleter>;

     enum struct ConstantExpressionSpecifier : std::uint_fast8_t
     {
          None,
          Constexpr,
          Consteval,
          Constinit
     };
     enum struct Operator : std::uint_fast8_t
     {
          Function,              // ()
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
          [[nodiscard]] const std::string& Name(void) const noexcept { return this->_name; }
          [[nodiscard]] const SBOVector<Token>& Arguments(void) const noexcept { return this->_arguments; }

     private:
          std::string _name;
          SBOVector<Token> _arguments;
     };

     enum struct ExplicitThisSpecifier : bool
     {
          Yes = true,
          No = false
     };

     struct FunctionParameter
     {
          SBOVector<std::unique_ptr<AttributeNode, ASTContext::Deleter>> attributes{};
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

     struct FunctionSignature
     {
          NodePointer type{};
          bool isFriend{};
          bool isInline{};
          bool isExtern{};
          std::optional<std::string> externOptional = std::nullopt;
          ConstantExpressionSpecifier constexprSpecifier{};
          SBOVector<std::unique_ptr<AttributeNode, ASTContext::Deleter>> attributes{};
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

               built += this->type->ToString(0) + " ";
               built += ::ToString(this->callingConvention) + " ";
               built += this->name->ToString(0);
               built += "(";
               built += this->parameters.ToString();
               built += ")";

               return built;
          }

          explicit FunctionSignature(NodePointer type, bool isFriend, bool isInline,
                                     ConstantExpressionSpecifier constexprSpecifier,
                                     SBOVector<std::unique_ptr<AttributeNode, ASTContext::Deleter>> attributes,
                                     NodePointer name, const abi::CallingConventionName callingConvention)
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

          [[nodiscard]] const FunctionSignature& Signature(void) const noexcept { return this->_signature; }

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

          [[nodiscard]] const FunctionSignature& Signature(void) const noexcept { return this->_signature; }
          [[nodiscard]] const SBOVector<NodePointer>& Body(void) const noexcept { return this->_body; }

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
               for (const auto& arg : this->_arguments) arguments += arg->ToString(0) + ", ";
               if (!arguments.empty()) // trailing comma
               {
                    arguments.pop_back();
                    arguments.pop_back();
               }

               return std::string(indent * PrettyIndent, ' ') + "(" + this->_function->ToString(0) + ")(" + arguments +
                      ")";
          }

          [[nodiscard]] const NodePointer& Function(void) const noexcept { return this->_function; }
          [[nodiscard]] const SBOVector<NodePointer>& Arguments(void) const noexcept { return this->_arguments; }

     private:
          NodePointer _function;
          SBOVector<NodePointer> _arguments;
     };

     class IdentifierNode;

     class ThisNode final : public Node
     {
     public:
          explicit ThisNode(Location source) : Node(source) {}
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
          [[nodiscard]] const Operator& Value(void) const noexcept { return this->_value; }
          [[nodiscard]] const NodePointer& Operand(void) const noexcept { return this->_operand; }
          [[nodiscard]] UnaryOperatorType UnaryType(void) const noexcept { return this->_type; }
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
          [[nodiscard]] const Operator& Value(void) const noexcept { return this->_value; }
          [[nodiscard]] const NodePointer& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const NodePointer& Right(void) const noexcept { return this->_right; }
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

     class ReturnNode final : public Node
     {
     public:
          explicit ReturnNode(NodePointer expression, Location source)
              : Node(source), _expression(std::move(expression))
          {
          }

          [[nodiscard]] const NodePointer& Value(void) const noexcept { return this->_expression; }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               if (this->_expression == nullptr) return std::string(indent * PrettyIndent, ' ') + "return";
               return std::string(indent * PrettyIndent, ' ') + "return " + this->_expression->ToString(0);
          }

     private:
          NodePointer _expression;
     };

     class VariableDeclarationNode final : public Node
     {
     public:
          struct Declarator
          {
               NodePointer name;
               NodePointer initialiser;
               std::vector<NodePointer> arrayLevels;
               explicit Declarator(NodePointer name, NodePointer initialiser = nullptr,
                                   std::vector<NodePointer> arrayLevels = {})
                   : name(std::move(name)), initialiser(std::move(initialiser)), arrayLevels(std::move(arrayLevels))
               {
               }
          };

          struct Flags
          {
               bool isTypedef = false;
               bool isFriend = false;
               bool isConstexpr = false;
               bool isConsteval = false;
               bool isConstinit = false;
               bool isInline = false;
               bool isStatic = false;
               bool isThreadLocal = false;
               bool isExtern = false;
               bool isMutable = false;
          };

          VariableDeclarationNode(NodePointer type, std::vector<Declarator> declarators, Flags flags,
                                  std::optional<NodePointer> explicitSpecifier, Location source)
              : Node(source), _type(std::move(type)), _declarators(std::move(declarators)), _flags(flags),
                _explicitSpecifier(std::move(explicitSpecifier))
          {
          }

          [[nodiscard]] const NodePointer& Type(void) const noexcept { return this->_type; }
          [[nodiscard]] const std::vector<Declarator>& Declarators(void) const noexcept { return this->_declarators; }
          [[nodiscard]] const Flags& GetFlags(void) const noexcept { return this->_flags; }
          [[nodiscard]] const std::optional<NodePointer>& ExplicitSpecifier(void) const noexcept
          {
               return this->_explicitSpecifier;
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built(indent * PrettyIndent, ' ');
               // add flags
               if (this->_flags.isTypedef) built += "typedef ";
               if (this->_flags.isConstexpr) built += "constexpr ";
               if (this->_flags.isConsteval) built += "consteval ";
               if (this->_flags.isConstinit) built += "constinit ";
               if (this->_flags.isInline) built += "inline ";
               if (this->_flags.isStatic) built += "static ";
               if (this->_flags.isThreadLocal) built += "thread_local ";
               if (this->_flags.isExtern) built += "extern ";
               if (this->_flags.isMutable) built += "mutable ";
               if (this->_flags.isFriend) built += "friend ";
               if (this->_explicitSpecifier) built += "explicit ";

               built += this->_type->ToString(0) + " ";

               for (size_t i = 0; i < _declarators.size(); ++i)
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
          explicit BooleanLiteralNode(const bool value, Location source) : Node(source), _value(value) {}
          [[nodiscard]] bool Value(void) const noexcept { return this->_value; }
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
          explicit StringLiteralNode(std::string value, Location source) : Node(source), _value(std::move(value)) {}

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "\"" + this->_value + "\"";
          }
          [[nodiscard]] const std::string& Value(void) const noexcept { return this->_value; }

     private:
          std::string _value;
     };

     class IntegerLiteralNode final : public Node
     {
     public:
          explicit IntegerLiteralNode(const unsigned long long value, Location source) : Node(source), _value(value) {}
          [[nodiscard]] unsigned long long Value(void) const noexcept { return this->_value; }
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
          explicit CharacterLiteralNode(const char value, Location source) : Node(source), _value(value) {}
          [[nodiscard]] char Value(void) const noexcept { return this->_value; }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + std::to_string(this->_value);
          }

     private:
          char _value;
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

          [[nodiscard]] bool AtEnd(void) const { return this->_position >= this->_tokens.size(); }
          [[nodiscard]] const Token& Peek(const std::ptrdiff_t offset = 0) const noexcept
          {
               return this->_tokens[this->_position + offset];
          }
          [[nodiscard]] Token& Peek(const std::ptrdiff_t offset = 0) { return this->_tokens[this->_position + offset]; }
          void Advance(void) noexcept { this->_position++; }
          void Retreat(void) noexcept { this->_position--; }
          [[nodiscard]] bool Match(const TokenType type) noexcept
          {
               if (Peek().type != type) return false;
               Advance();
               return true;
          }

          // General
          std::unique_ptr<IdentifierNode, ASTContext::Deleter> ParseIdentifier(ASTContext& context);
          NodePointer ParseSimpleTypeSpecifier(ASTContext& context);

          // Declarations
          NodePointer ParseDeclaration(ASTContext& context);
          NodePointer ParseBlockDeclaration(ASTContext& context);
          NodePointer ParseNameDeclaration(ASTContext& context);
          NodePointer ParseFunctionDefinition(ASTContext& context);
          bool IsDeclarationStart(ASTContext& context);

          // simple-declaration
          NodePointer ParseSimpleDeclaration(ASTContext& context);               // simple-declaration
          static NodePointer TryParseDeclSpecifier(ASTContext& context);         // decl-specifier
          static NodePointer TryParseDefiningTypeSpecifier(ASTContext& context); // defining-type-specifier
          static NodePointer ParseInitDeclarator(ASTContext& context);           // init-declarator
          static NodePointer TryParseDeclarator(ASTContext& context);            // declarator
          static NodePointer TryParsePtrDeclarator(ASTContext& context);         // ptr-declarator
          static NodePointer TryParseNoPtrDeclarator(ASTContext& context);       // no-ptr-declarator
          static NodePointer ParseInitialiser(ASTContext& context);              // initialiser

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

          // Statements
          NodePointer ParseStatement(ASTContext& context);
          NodePointer ParseDeclarationStatement(ASTContext& context) { return ParseBlockDeclaration(context); }
          NodePointer ParseExpressionStatement(ASTContext& context);

          // Helpers (parse sub-components)
          FunctionParameter ParseFunctionParameter(ASTContext& context);
     };
} // namespace ecpps::ast
