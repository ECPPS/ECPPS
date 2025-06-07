#pragma once
#include <memory>
#include <string>

namespace ecpps::typeSystem
{
     /// <summary>
     /// Base type construct
     /// N4950/6.8.1 [basic.types.general]
     /// Note 1 : 6.8 and the subclauses thereof impose requirements on implementations regarding the representation of
     /// types. There are two kinds of types : fundamental types and compound types. Types describe objects(6.7.2),
     /// references(9.3.4.3), or functions(9.3.4.6).- end note]
     /// </summary>
     class TypeBase
     {
     public:
          virtual ~TypeBase(void) = default;
          explicit TypeBase(std::string name) : _name(std::move(name)) {}
          [[nodiscard]] const std::string& Name(void) const noexcept { return this->_name; }
          [[nodiscard]] virtual std::string RawName(void) const noexcept = 0;
          [[nodiscard]] virtual std::shared_ptr<TypeBase> Clone(void) const = 0;

     private:
          std::string _name;
     };

     using TypePointer = std::shared_ptr<TypeBase>;
} // namespace ecpps::typeSystem