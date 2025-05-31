#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include "SourceMap.h"
#include "Tokeniser.h"

namespace ecpps::ast
{
     constexpr std::size_t PrettyIndent = 5;

     class Node
     {
     public:
          explicit Node(Location location) : _location(std::move(location)) {}
          virtual ~Node(void) = default;

          [[nodiscard]] virtual std::string ToString(std::size_t indent) const = 0;

     private:
          Location _location;
     };
     using NodePointer = std::unique_ptr<Node>;

     enum struct ConstantExpressionSpecifier
     {
          None,
          Constexpr,
          Consteval,
          Constinit
     };
     enum struct Operator : std::uint8_t
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
          }
          return "";
     }

     class AttributeNode : public Node
     {
     };

     struct FunctionSignature
     {
          NodePointer type{};
          bool isFriend{};
          bool isInline{};
          bool isExtern{};
          std::optional<std::string> externOptional = std::nullopt;
          ConstantExpressionSpecifier constexprSpecifier{};
          std::vector<AttributeNode> attributes{};
          NodePointer name;
          // TODO: Trailing return types

          [[nodiscard]] std::string ToString(void) const
          {
               using std::string_literals::operator""s;
               std::string built{};
               for (const auto& attr : this->attributes) built += "[["s + attr.ToString(0) + "]] ";

               if (this->isFriend) built += "friend ";
               if (this->isInline) built += "inline ";
               const auto m = this->externOptional.has_value();
               if (this->isExtern) built += "extern " + (m ? ("\""s + this->externOptional.value() + "\"") : "");
               switch (this->constexprSpecifier)
               {
               case ConstantExpressionSpecifier::None: break;
               case ConstantExpressionSpecifier::Constexpr: built += "constexpr "; break;
               case ConstantExpressionSpecifier::Consteval: built += "consteval "; break;
               case ConstantExpressionSpecifier::Constinit: built += "constinit "; break;
               }

               built += this->type->ToString(0) + " ";
               built += this->name->ToString(0);
               built += "(";
               // TODO: Parameters
               built += ")";

               return built;
          }

          explicit FunctionSignature(NodePointer type, bool isFriend, bool isInline,
                                     ConstantExpressionSpecifier constexprSpecifier,
                                     std::vector<AttributeNode> attributes, NodePointer name)
              : type(std::move(type)), isFriend(isFriend), isInline(isInline), constexprSpecifier(constexprSpecifier),
                attributes(std::move(attributes)), name(std::move(name))
          {
          }
     };

     class FunctionDefinitionNode final : public Node
     {
     public:
          explicit FunctionDefinitionNode(FunctionSignature signature, std::vector<NodePointer> body, Location source)
              : Node(std::move(source)), _signature(std::move(signature)), _body(std::move(body))
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
          [[nodiscard]] const std::vector<NodePointer>& Body(void) const noexcept { return this->_body; }

     private:
          FunctionSignature _signature;
          std::vector<NodePointer> _body;
     };

     class IdentifierNode;

     class ThisNode final : public Node
     {
     public:
          explicit ThisNode(Location source) : Node(std::move(source)) {}
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "this";
          }
     };

     enum struct UnaryOperatorType : std::uint8_t
     {
          Prefix,
          Postfix,
     };

     class UnaryOperatorNode final : public Node
     {
     public:
          explicit UnaryOperatorNode(Operator value, NodePointer operand, const UnaryOperatorType type, Location source)
              : Node(std::move(source)), _value(std::move(value)), _operand(std::move(operand)), _type(type)
          {
          }
          [[nodiscard]] const Operator& value(void) const noexcept { return this->_value; }
          [[nodiscard]] const NodePointer& operand(void) const noexcept { return this->_operand; }
          [[nodiscard]] UnaryOperatorType unaryType(void) const noexcept { return this->_type; }
          [[nodiscard]] std::string ToString(const std::size_t indent) const override
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
              : Node(std::move(source)), _left(std::move(left)), _value(std::move(value)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Operator& value(void) const noexcept { return this->_value; }
          [[nodiscard]] const NodePointer& left(void) const noexcept { return this->_left; }
          [[nodiscard]] const NodePointer& right(void) const noexcept { return this->_right; }
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
              : Node(std::move(source)), _expression(std::move(expression))
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

     class BooleanLiteralNode final : public Node
     {
     public:
          explicit BooleanLiteralNode(const bool value, Location source) : Node(std::move(source)), _value(value) {}
          [[nodiscard]] bool value(void) const noexcept { return this->_value; }
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
          explicit StringLiteralNode(std::string value, Location source)
              : Node(std::move(source)), _value(std::move(value))
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + "\"" + this->_value + "\"";
          }
          [[nodiscard]] const std::string& value(void) const noexcept { return this->_value; }

     private:
          std::string _value;
     };

     class IntegerLiteralNode final : public Node
     {
     public:
          explicit IntegerLiteralNode(const unsigned long long value, Location source)
              : Node(std::move(source)), _value(value)
          {
          }
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
          explicit CharacterLiteralNode(const char value, Location source) : Node(std::move(source)), _value(value) {}
          [[nodiscard]] char value(void) const noexcept { return this->_value; }
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
          [[nodiscard]] std::vector<NodePointer> Parse(void);

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
          [[nodiscard]] void Advance(void) noexcept { this->_position++; }
          [[nodiscard]] void Retreat(void) noexcept { this->_position--; }
          [[nodiscard]] bool Match(const TokenType type) noexcept
          {
               if (Peek().type != type) return false;
               Advance();
               return true;
          }

          // General
          std::unique_ptr<IdentifierNode> ParseIdentifier(void);
          NodePointer ParseSimpleTypeSpecifier(void);

          // Declarations
          NodePointer ParseDeclaration(void);
          NodePointer ParseBlockDeclaration(void);
          NodePointer ParseNameDeclaration(void);
          NodePointer ParseFunctionDefinition(void);

          // Expressions
          [[nodiscard]] NodePointer ParsePrimaryExpression(void);
          [[nodiscard]] NodePointer ParseIdExpression(void);
          [[nodiscard]] NodePointer ParsePostfixExpresssion(void);
          [[nodiscard]] NodePointer ParseUnaryExpression(void);
          [[nodiscard]] NodePointer ParseCastExpression(void);
          [[nodiscard]] NodePointer ParsePmExpression(void);
          [[nodiscard]] NodePointer ParseMultiplicativeExpression(void);
          [[nodiscard]] NodePointer ParseAdditiveExpression(void);
          [[nodiscard]] NodePointer ParseShiftExpression(void);
          [[nodiscard]] NodePointer ParseCompareExpression(void);
          [[nodiscard]] NodePointer ParseRelationalExpression(void);
          [[nodiscard]] NodePointer ParseEqualityExpression(void);
          [[nodiscard]] NodePointer ParseBinaryAndExpression(void);
          [[nodiscard]] NodePointer ParseBinaryExclusiveOrExpression(void);
          [[nodiscard]] NodePointer ParseBinaryInclusiveOrExpression(void);
          [[nodiscard]] NodePointer ParseLogicalAndExpression(void);
          [[nodiscard]] NodePointer ParseLogicalOrExpression(void);
          [[nodiscard]] NodePointer ParseConditionalExpression(void);
          [[nodiscard]] NodePointer ParseAssignmentExpression(void);
          [[nodiscard]] NodePointer ParseExpression(void);

          // Statements
          NodePointer ParseStatement(void);
          NodePointer ParseExpressionStatement(void);
     };
} // namespace ecpps::ast