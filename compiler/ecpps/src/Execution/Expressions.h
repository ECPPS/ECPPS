#pragma once
#include <memory>

namespace ecpps
{
     class ExpressionBase // dummy class
     {
     public:
          virtual ~ExpressionBase(void) = default;

     private:
     };
     using Expression = std::unique_ptr<ExpressionBase>;
} // namespace ecpps