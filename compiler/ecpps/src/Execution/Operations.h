#pragma once

#include "../Parsing/AST.h"
#include "Expressions.h"
#include "NodeBase.h"

namespace ecpps::ir
{
     enum struct BinaryOperationLevel
     {
          None,         // +            => add tmp
          Atomic,       // (atomic) +   => lock add tmp
          Assign,       // +=           => add
          AtomicAssign, // (atomic) +=  => lock add
     };

     enum struct MemoryOrdering
     {
          Acquire,
          Release,
          Relaxed,
          Sequenced,
     };

     /// <summary>
     /// add 2 (hopefully) integral operands. No checking as I can't predict them really, the optimiser might clash 2
     /// classes with that btw, if they are layout compatible, have an overloaded operator+= that adds all members 1:1
     /// to each other and each of them is possible to add using that node Example: struct C
     /// {
     ///      C& operator+(const C& other)
     ///      {
     ///           this->a += other.a;
     ///           this->b += other.b;
     ///           return *this;
     ///      }
     ///
     ///      int a;
     ///      int b;
     /// };
     ///
     /// This function supports vectorisation
     /// </summary>
     class AdditionNode final : public NodeBase
     {
     public:
          explicit AdditionNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Addition, std::move(source)), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) + " + " +
                      this->_right->Value()->ToString(0);
          }

     private:
          Expression _left;
          Expression _right;
     };

     class SubtractionNode final : public NodeBase
     {

     public:
          explicit SubtractionNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Subtraction, std::move(source)), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) + " - " +
                      this->_right->Value()->ToString(0);
          }

     private:
          Expression _left;
          Expression _right;
     };

     class MultiplicationNode final : public NodeBase
     {

     public:
          explicit MultiplicationNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Subtraction, std::move(source)), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) + " * " +
                      this->_right->Value()->ToString(0);
          }

     private:
          Expression _left;
          Expression _right;
     };

     class DivideNode final : public NodeBase
     {

     public:
          explicit DivideNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Subtraction, std::move(source)), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) + " / " +
                      this->_right->Value()->ToString(0);
          }

     private:
          Expression _left;
          Expression _right;
     };

     class ModuloNode final : public NodeBase
     {

     public:
          explicit ModuloNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Subtraction, std::move(source)), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) + " % " +
                      this->_right->Value()->ToString(0);
          }

     private:
          Expression _left;
          Expression _right;
     };

     class CompareExchangeNode final : public NodeBase
     {
     public:
          explicit CompareExchangeNode(Expression address, Expression expected, Expression replacement, bool isWeak,
                                       MemoryOrdering ordering, Location source)
              : NodeBase(NodeKind::CompareExchange, std::move(source)), _address(std::move(address)),
                _expected(std::move(expected)), _replacement(std::move(replacement)), _isWeak(isWeak),
                _ordering(ordering)
          {
          }

          [[nodiscard]] const Expression& Address(void) const noexcept { return _address; }
          [[nodiscard]] const Expression& Expected(void) const noexcept { return _expected; }
          [[nodiscard]] const Expression& Replacement(void) const noexcept { return _replacement; }
          [[nodiscard]] bool IsWeak(void) const noexcept { return _isWeak; }
          [[nodiscard]] MemoryOrdering OrderingMode(void) const noexcept { return _ordering; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "__cmpxchg(...)";
          }

     private:
          Expression _address;
          Expression _expected;
          Expression _replacement;
          bool _isWeak;
          MemoryOrdering _ordering;
     };

     class LoadNode final : public NodeBase
     {
     public:
          explicit LoadNode(Expression address, Location source)
              : NodeBase(NodeKind::Load, std::move(source)), _address(std::move(address))
          {
          }

          [[nodiscard]] const Expression& Address(void) const noexcept { return this->_address; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "*" + this->_address->Value()->ToString(0);
          }

     private:
          Expression _address;
     };

     class StoreNode final : public NodeBase
     {
     public:
          explicit StoreNode(std::string address, Expression value, Location source)
              : NodeBase(NodeKind::Store, std::move(source)), _address(std::move(address)), _value(std::move(value))
          {
          }

          [[nodiscard]] const std::string& Address(void) const noexcept { return this->_address; }
          [[nodiscard]] const Expression& Value(void) const noexcept { return this->_value; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_address + " = " +
                      this->_value->Value()->ToString(0);
          }

     private:
          std::string _address;
          Expression _value;
     };

     class AddressOfNode final : public NodeBase
     {
     public:
          explicit AddressOfNode(Expression operand, Location source)
              : NodeBase(NodeKind::AddressOf, std::move(source)), _operand(std::move(operand))
          {
          }

          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "&" + this->_operand->Value()->ToString(0);
          }

     private:
          Expression _operand;
     };

     class DereferenceNode final : public NodeBase
     {
     public:
          explicit DereferenceNode(Expression operand, Location source)
              : NodeBase(NodeKind::Dereference, std::move(source)), _operand(std::move(operand))
          {
          }

          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "*" + this->_operand->Value()->ToString(0);
          }

     private:
          Expression _operand;
     };

} // namespace ecpps::ir