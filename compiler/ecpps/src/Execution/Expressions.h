#pragma once
#include <memory>
#include <utility>
#include "..\TypeSystem\TypeBase.h"
#include "NodeBase.h"

namespace ecpps
{
     class ExpressionBase
     {
     public:
          explicit ExpressionBase(typeSystem::TypePointer type, ir::NodePointer value, const bool isConstexpr)
              : _type(std::move(type)), _value(std::move(value)), _isConstantExpression(isConstexpr)
          {
          }

          virtual ~ExpressionBase(void) = default;

          [[nodiscard]] constexpr bool IsExpression(void) const noexcept { return true; }

          [[nodiscard]] virtual bool IsLValue(void) const noexcept { return false; }
          [[nodiscard]] virtual bool IsXValue(void) const noexcept { return false; }
          [[nodiscard]] virtual bool IsPRValue(void) const noexcept { return false; }

          [[nodiscard]] bool IsGLValue(void) const noexcept { return this->IsLValue() || this->IsXValue(); }
          [[nodiscard]] bool IsRValue(void) const noexcept { return this->IsXValue() || this->IsPRValue(); }

          [[nodiscard]] bool IsConstantExpression(void) const noexcept { return this->_isConstantExpression; }
          [[nodiscard]] const ir::NodePointer& Value(void) const& noexcept { return this->_value; }
          [[nodiscard]] ir::NodePointer&& Value(void) && noexcept { return std::move(this->_value); }
          [[nodiscard]] const typeSystem::TypePointer& Type(void) const noexcept { return this->_type; }

     private:
          typeSystem::TypePointer _type;
          ir::NodePointer _value;
          bool _isConstantExpression;
     };
     using Expression = std::unique_ptr<ExpressionBase>;

     class LValue final : public ExpressionBase
     {
     public:
          [[nodiscard]] bool IsLValue(void) const noexcept override { return true; }
     };

     class XValue final : public ExpressionBase
     {
     public:
          [[nodiscard]] bool IsXValue(void) const noexcept override { return true; }
     };
     class PRValue final : public ExpressionBase
     {
     public:
          using ExpressionBase::ExpressionBase;

          [[nodiscard]] bool IsPRValue(void) const noexcept override { return true; }
     };
} // namespace ecpps