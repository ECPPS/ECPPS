#pragma once

#include "Execution/Expressions.h"
#include "Operations.h"

namespace ecpps::ir::high
{
     class AdditionNode final : public NodeBase
     {
     public:
          explicit AdditionNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Addition, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] Expression Left(void) && noexcept { return std::move(this->_left); }
          [[nodiscard]] Expression Right(void) && noexcept { return std::move(this->_right); }

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
              : NodeBase(NodeKind::Addition, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] Expression Left(void) && noexcept { return std::move(this->_left); }
          [[nodiscard]] Expression Right(void) && noexcept { return std::move(this->_right); }

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
              : NodeBase(NodeKind::Addition, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] Expression Left(void) && noexcept { return std::move(this->_left); }
          [[nodiscard]] Expression Right(void) && noexcept { return std::move(this->_right); }

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
              : NodeBase(NodeKind::Addition, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] Expression Left(void) && noexcept { return std::move(this->_left); }
          [[nodiscard]] Expression Right(void) && noexcept { return std::move(this->_right); }

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
              : NodeBase(NodeKind::Addition, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] Expression Left(void) && noexcept { return std::move(this->_left); }
          [[nodiscard]] Expression Right(void) && noexcept { return std::move(this->_right); }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) + " % " +
                      this->_right->Value()->ToString(0);
          }

     private:
          Expression _left;
          Expression _right;
     };

     class ConvertNode final : public NodeBase
     {
     public:
          ConvertNode(Expression operand, ecpps::typeSystem::NonowningTypePointer targetType, Location source)
              : NodeBase(NodeKind::Convert, source), _operand(std::move(operand)), _targetType(targetType)
          {
          }

          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }
          [[nodiscard]] Expression Operand(void) && noexcept { return std::move(this->_operand); }
          [[nodiscard]] ecpps::typeSystem::NonowningTypePointer TargetType(void) const noexcept
          {
               return this->_targetType;
          }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "__convert<" + _targetType->RawName() + ">(" +
                      _operand->Value()->ToString(0) + ")";
          }

     private:
          Expression _operand;
          ecpps::typeSystem::NonowningTypePointer _targetType;
     };

     class PointerConversionNode final : public NodeBase
     {
     public:
          PointerConversionNode(Expression operand, ecpps::typeSystem::NonowningTypePointer targetType, Location source)
              : NodeBase(NodeKind::PointerConversion, source), _operand(std::move(operand)), _targetType(targetType)
          {
          }

          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }
          [[nodiscard]] Expression Operand(void) && noexcept { return std::move(this->_operand); }
          [[nodiscard]] ecpps::typeSystem::NonowningTypePointer TargetType(void) const noexcept
          {
               return this->_targetType;
          }

          [[nodiscard]] std::string ToString(std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "__pointer_cast<" + _targetType->RawName() + ">(" +
                      _operand->Value()->ToString(0) + ")";
          }

     private:
          Expression _operand;
          ecpps::typeSystem::NonowningTypePointer _targetType;
     };

     class AddressOfNode final : public NodeBase
     {
     public:
          explicit AddressOfNode(Expression operand, Location source)
              : NodeBase(NodeKind::AddressOf, source), _operand(std::move(operand))
          {
          }

          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }
          [[nodiscard]] Expression Operand(void) && noexcept { return std::move(this->_operand); }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "__address_of(" +
                      this->_operand->Value()->ToString(0) + ")";
          }

     private:
          Expression _operand;
     };

     class DereferenceNode final : public NodeBase
     {
     public:
          explicit DereferenceNode(Expression operand, Location source)
              : NodeBase(NodeKind::Dereference, source), _operand(std::move(operand))
          {
          }

          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }
          [[nodiscard]] Expression Operand(void) && noexcept { return std::move(this->_operand); }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + "__dereference(" +
                      this->_operand->Value()->ToString(0) + ")";
          }

     private:
          Expression _operand;
     };
     class AdditionAssignNode final : public NodeBase
     {
     public:
          explicit AdditionAssignNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Addition, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] Expression Left(void) && noexcept { return std::move(this->_left); }
          [[nodiscard]] Expression Right(void) && noexcept { return std::move(this->_right); }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) +
                      " += " + this->_right->Value()->ToString(0);
          }

     private:
          Expression _left;
          Expression _right;
     };
     class SubtractionAssignNode final : public NodeBase
     {
     public:
          explicit SubtractionAssignNode(Expression left, Expression right, Location source)
              : NodeBase(NodeKind::Addition, source), _left(std::move(left)), _right(std::move(right))
          {
          }
          [[nodiscard]] const Expression& Left(void) const noexcept { return this->_left; }
          [[nodiscard]] const Expression& Right(void) const noexcept { return this->_right; }

          [[nodiscard]] Expression Left(void) && noexcept { return std::move(this->_left); }
          [[nodiscard]] Expression Right(void) && noexcept { return std::move(this->_right); }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_left->Value()->ToString(0) +
                      " -= " + this->_right->Value()->ToString(0);
          }

     private:
          Expression _left;
          Expression _right;
     };

     class PostIncrementNode final : public NodeBase
     {
     public:
          explicit PostIncrementNode(Expression operand, std::size_t increment, Location source)
              : NodeBase(NodeKind::Addition, source), _operand(std::move(operand)), _increment(increment)
          {
          }
          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }
          [[nodiscard]] Expression Operand(void) && noexcept { return std::move(this->_operand); }
          [[nodiscard]] std::size_t IncrementValue(void) const noexcept { return this->_increment; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_operand->Value()->ToString(0) + " ++ " +
                      std::to_string(this->_increment);
          }

     private:
          Expression _operand;
          std::size_t _increment;
     };

     class PostDecrementNode final : public NodeBase
     {
     public:
          explicit PostDecrementNode(Expression operand, std::size_t increment, Location source)
              : NodeBase(NodeKind::Addition, source), _operand(std::move(operand)), _increment(increment)
          {
          }
          [[nodiscard]] const Expression& Operand(void) const noexcept { return this->_operand; }
          [[nodiscard]] Expression Operand(void) && noexcept { return std::move(this->_operand); }
          [[nodiscard]] std::size_t IncrementValue(void) const noexcept { return this->_increment; }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * ast::PrettyIndent, ' ') + this->_operand->Value()->ToString(0) + " -- " +
                      std::to_string(this->_increment);
          }

     private:
          Expression _operand;
          std::size_t _increment;
     };
} // namespace ecpps::ir::high
