#pragma once
#include <memory>
#include "../AST.h"
#include "General.h"

namespace ecpps::ast
{
     class NamespaceAliasNode final : public Node
     {
     public:
          explicit NamespaceAliasNode(
              std::unique_ptr<IdentifierNode, ecpps::ast::ASTContext::Deleter> name,
              SBOVector<std::unique_ptr<IdentifierNode, ecpps::ast::ASTContext::Deleter>> aliasedNamespace,
              Location source)
              : Node(source), _aliasName(std::move(name)), _aliasedNamespace(std::move(aliasedNamespace))
          {
          }

          [[nodiscard]] const std::unique_ptr<IdentifierNode, ecpps::ast::ASTContext::Deleter>& Name(
              void) const noexcept
          {
               return this->_aliasName;
          }
          [[nodiscard]] const SBOVector<std::unique_ptr<IdentifierNode, ecpps::ast::ASTContext::Deleter>>&
          AliasedNamespace(void) const noexcept
          {
               return this->_aliasedNamespace;
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
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
          std::unique_ptr<IdentifierNode, ecpps::ast::ASTContext::Deleter> _aliasName;
          SBOVector<std::unique_ptr<IdentifierNode, ecpps::ast::ASTContext::Deleter>> _aliasedNamespace;
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
               return std::string(indent * PrettyIndent, ' ') + this->_name;
          }
          [[nodiscard]] const std::string& Value(void) const noexcept { return this->_name; }
          [[nodiscard]] bool IsConst(void) const noexcept { return this->_isConst; }
          [[nodiscard]] bool IsVolatile(void) const noexcept { return this->_isVolatile; }

     private:
          std::string _name;
          bool _isConst;
          bool _isVolatile;
     };

     class PointerType final : public Node
     {
     public:
          explicit PointerType(NodePointer baseType, Location source) : Node(source), _baseType(std::move(baseType)) {}

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + _baseType->ToString(0) + "*";
          }

          [[nodiscard]] const NodePointer& BaseType() const noexcept { return _baseType; }

     private:
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

          [[nodiscard]] const NodePointer& BaseType() const noexcept { return _baseType; }
          [[nodiscard]] Kind GetKind() const noexcept { return _kind; }

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
               explicit Section(NodePointer node, bool isTemplate) : node(std::move(node)), isTemplate(isTemplate) {}
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
          [[nodiscard]] const SBOVector<Section>& Sections(void) const noexcept { return this->_sections; }
          [[nodiscard]] const NodePointer& UnqualifiedType(void) const noexcept { return this->_unqualifiedType; }

     private:
          SBOVector<Section> _sections;
          NodePointer _unqualifiedType;
     };
} // namespace ecpps::ast
