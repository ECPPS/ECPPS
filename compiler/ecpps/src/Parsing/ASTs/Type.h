#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../AST.h"
#include "General.h"

namespace ecpps::ast
{
     class NamespaceAliasNode final : public Node
     {
     public:
          explicit NamespaceAliasNode(std::unique_ptr<IdentifierNode, ASTDeleter> name,
                                      SBOVector<std::unique_ptr<IdentifierNode, ASTDeleter>> aliasedNamespace,
                                      Location source)
              : Node(source), _aliasName(std::move(name)), _aliasedNamespace(std::move(aliasedNamespace))
          {
          }

          [[nodiscard]] const std::unique_ptr<IdentifierNode, ASTDeleter>& Name(void) const noexcept
          {
               return this->_aliasName;
          }
          [[nodiscard]] const SBOVector<std::unique_ptr<IdentifierNode, ASTDeleter>>& AliasedNamespace(
               void) const noexcept
          {
               return this->_aliasedNamespace;
          }

          [[nodiscard]] std::string ToString([[maybe_unused]] const std::size_t indent) const override
          {
               if (this->_aliasName == nullptr) return "";

               std::string aliased{};
               for (const auto& node : this->_aliasedNamespace) aliased += node->ToString(0) + "::";
               if (!aliased.empty())
               {
                    aliased.pop_back();
                    aliased.pop_back();
               }
               return "namespace " + this->_aliasName->ToString(0) + " = " + aliased;
          }

     private:
          std::unique_ptr<IdentifierNode, ASTDeleter> _aliasName;
          SBOVector<std::unique_ptr<IdentifierNode, ASTDeleter>> _aliasedNamespace;
     };

     class NamespaceNode final : public Node
     {
     public:
          explicit NamespaceNode(std::unique_ptr<IdentifierNode, ASTDeleter> name, SBOVector<NodePointer> declarations,
                                 Location source)
              : Node(source), _name(std::move(name)), _declarations(std::move(declarations))
          {
          }

          [[nodiscard]] const std::unique_ptr<IdentifierNode, ASTDeleter>& Name(void) const noexcept
          {
               return this->_name;
          }
          [[nodiscard]] const SBOVector<NodePointer>& Declarations(void) const noexcept
          {
               return this->_declarations;
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built = std::string(indent * PrettyIndent, ' ') + "namespace ";
               if (this->_name != nullptr) built += this->_name->ToString(0);
               built += "\n" + std::string(indent * PrettyIndent, ' ') + "{";

               for (const auto& decl : this->_declarations)
               {
                    if (decl != nullptr) built += "\n" + decl->ToString(indent + 1) + ";";
               }

               return built + "\n" + std::string(indent * PrettyIndent, ' ') + "}";
          }

     private:
          std::unique_ptr<IdentifierNode, ASTDeleter> _name;
          SBOVector<NodePointer> _declarations;
     };

     class BasicType final : public Node
     {
     public:
          explicit BasicType(std::string name, Location source, const bool isConst, const bool isVolatile)
              : Node(source), _name(std::move(name)), _isConst(isConst), _isVolatile(isVolatile)
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string cv{};
               if (this->_isConst) cv += "const ";
               if (this->_isVolatile) cv += "volatile ";
               return std::string(indent * PrettyIndent, ' ') + cv + this->_name;
          }
          [[nodiscard]] const std::string& Value(void) const noexcept
          {
               return this->_name;
          }
          [[nodiscard]] bool IsConst(void) const noexcept
          {
               return this->_isConst;
          }
          [[nodiscard]] bool IsVolatile(void) const noexcept
          {
               return this->_isVolatile;
          }
          void SetConst(const bool newValue) noexcept
          {
               this->_isConst = newValue;
          }
          void SetVolatile(const bool newValue) noexcept
          {
               this->_isVolatile = newValue;
          }

     private:
          std::string _name;
          bool _isConst;
          bool _isVolatile;
     };
     enum struct AccessSpecifier : std::uint8_t
     {
          Public,
          Private,
          Protected
     };
     enum struct ClassKey : std::uint8_t
     {
          Class,
          Struct,
          Union
     };
     class ClassDefNode;
     class ClassType : public Node
     {
     public:
          explicit ClassType(std::string name, ClassKey key, const bool isConst, const bool isVolatile,
                             Location source) // TODO: attributes
              : Node(source), _name(std::move(name)), _key(key), _isConst(isConst), _isVolatile(isVolatile)
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string cv{};
               if (this->_isConst) cv += "const ";
               if (this->_isVolatile) cv += "volatile ";
               std::string keyStr{};
               switch (this->_key)
               {
               case ClassKey::Class: keyStr = "class "; break;
               case ClassKey::Struct: keyStr = "struct "; break;
               case ClassKey::Union: keyStr = "union "; break;
               }
               return std::string(indent * PrettyIndent, ' ') + cv + keyStr + this->_name;
          }
          [[nodiscard]] const std::string& Value(void) const noexcept
          {
               return this->_name;
          }
          [[nodiscard]] bool IsConst(void) const noexcept
          {
               return this->_isConst;
          }
          [[nodiscard]] bool IsVolatile(void) const noexcept
          {
               return this->_isVolatile;
          }
          [[nodiscard]] ClassKey Key(void) const noexcept
          {
               return this->_key;
          }
          void SetConst(const bool newValue) noexcept
          {
               this->_isConst = newValue;
          }
          void SetVolatile(const bool newValue) noexcept
          {
               this->_isVolatile = newValue;
          }

     private:
          std::string _name;
          ClassKey _key;
          bool _isConst;
          bool _isVolatile;

          friend class ClassDefNode;
     };
     class AccessSpecifierNode final : public Node
     {
     public:
          explicit AccessSpecifierNode(AccessSpecifier specifier, Location source) : Node(source), _specifier(specifier)
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               switch (this->_specifier)
               {
               case AccessSpecifier::Public: return std::string(indent * PrettyIndent, ' ') + "public:";
               case AccessSpecifier::Private: return std::string(indent * PrettyIndent, ' ') + "private:";
               case AccessSpecifier::Protected: return std::string(indent * PrettyIndent, ' ') + "protected:";
               }
               return "__unknown_accessibility:";
          }
          [[nodiscard]] AccessSpecifier Value(void) const noexcept
          {
               return this->_specifier;
          }

     private:
          AccessSpecifier _specifier;

          friend class ClassDefNode;
     };
     class ClassDefNode : public ClassType
     {
     public:
          explicit ClassDefNode(std::string name, ClassKey key, std::vector<NodePointer> memberSpecifiers,
                                const bool isConst, const bool isVolatile,
                                Location source) // TODO: base classes, final & attributes
              : ClassType(std::move(name), key, isConst, isVolatile, source),
                _memberSpecifiers(std::move(memberSpecifiers))
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               const auto indentString = std::string(indent * PrettyIndent, ' ');
               auto base = ClassType::ToString(indent);
               base += "\n";
               base += indentString;
               base += "{";
               base += "\n";
               for (const auto& spec : this->_memberSpecifiers) base += spec->ToString(indent) + "\n";
               return base + indentString + "}";
          }

     private:
          std::vector<NodePointer> _memberSpecifiers;
     };

     class PointerType final : public Node
     {
     public:
          explicit PointerType(NodePointer baseType, const bool isConst, const bool isVolatile, Location source)
              : Node(source), _isConst(isConst), _isVolatile(isVolatile), _baseType(std::move(baseType))
          {
          }
          explicit PointerType(NodePointer baseType, Location source)
              : Node(source), _isConst(false), _isVolatile(false), _baseType(std::move(baseType))
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string cv{};
               if (this->_isConst) cv += " const";
               if (this->_isVolatile) cv += " volatile";
               return std::string(indent * PrettyIndent, ' ') + _baseType->ToString(0) + "*" + cv;
          }

          [[nodiscard]] const NodePointer& BaseType(void) const noexcept
          {
               return _baseType;
          }

          [[nodiscard]] bool IsConst(void) const noexcept
          {
               return this->_isConst;
          }
          [[nodiscard]] bool IsVolatile(void) const noexcept
          {
               return this->_isVolatile;
          }
          void SetConst(const bool newValue) noexcept
          {
               this->_isConst = newValue;
          }
          void SetVolatile(const bool newValue) noexcept
          {
               this->_isVolatile = newValue;
          }

     private:
          bool _isConst;
          bool _isVolatile;
          NodePointer _baseType;
     };

     class ReferenceType final : public Node
     {
     public:
          enum class Kind : std::uint_fast8_t
          {
               LValue,
               RValue
          };

          explicit ReferenceType(NodePointer baseType, Kind kind, Location source)
              : Node(source), _baseType(std::move(baseType)), _kind(kind)
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + _baseType->ToString(0) +
                      (_kind == Kind::LValue ? "&" : "&&");
          }

          [[nodiscard]] const NodePointer& BaseType() const noexcept
          {
               return _baseType;
          }
          [[nodiscard]] Kind GetKind() const noexcept
          {
               return _kind;
          }

     private:
          NodePointer _baseType;
          Kind _kind;
     };

     class QualifiedType final : public Node
     {
     public:
          struct Section
          {
               NodePointer node;
               bool isTemplate;
               explicit Section(NodePointer node, bool isTemplate) : node(std::move(node)), isTemplate(isTemplate)
               {
               }
          };

          explicit QualifiedType(SBOVector<Section> sections, NodePointer unqualified, Location source)
              : Node(source), _sections(std::move(sections)), _unqualifiedType(std::move(unqualified))
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built = std::string(indent * PrettyIndent, ' ');
               for (const auto& section : this->_sections)
                    built += section.node->ToString(0) + (section.isTemplate ? "::template " : "::");

               return built + this->_unqualifiedType->ToString(0);
          }
          [[nodiscard]] const SBOVector<Section>& Sections(void) const noexcept
          {
               return this->_sections;
          }
          [[nodiscard]] const NodePointer& UnqualifiedType(void) const noexcept
          {
               return this->_unqualifiedType;
          }

     private:
          SBOVector<Section> _sections;
          NodePointer _unqualifiedType;
     };
} // namespace ecpps::ast
