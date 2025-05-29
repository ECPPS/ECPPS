#pragma once
#include "../AST.h"
#include "General.h"

namespace ecpps::ast
{
     class NamespaceAliasNode final : public Node
     {
     public:
          explicit NamespaceAliasNode(std::unique_ptr<IdentifierNode> name,
                                      std::vector<std::unique_ptr<IdentifierNode>> aliasedNamespace, Location source)
              : Node(std::move(source)), _aliasName(std::move(name)), _aliasedNamespace(std::move(aliasedNamespace))
          {
          }

          [[nodiscard]] const std::unique_ptr<IdentifierNode>& name(void) const noexcept { return this->_aliasName; }
          [[nodiscard]] const std::vector<std::unique_ptr<IdentifierNode>>& aliasedNamespace(void) const noexcept
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
          std::unique_ptr<IdentifierNode> _aliasName;
          std::vector<std::unique_ptr<IdentifierNode>> _aliasedNamespace;
     };

     class BasicType final : public Node
     {
     public:
          explicit BasicType(std::string name, Location source) : Node(std::move(source)), _name(std::move(name)) {}

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               return std::string(indent * PrettyIndent, ' ') + this->_name;
          }
          [[nodiscard]] const std::string& Value(void) const noexcept { return this->_name; }

     private:
          std::string _name;
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

          explicit QualifiedType(std::vector<Section> sections, NodePointer unqualified, Location source)
              : Node(std::move(source)), _sections(std::move(sections)), _unqualifiedType(std::move(unqualified))
          {
          }

          [[nodiscard]] std::string ToString(const std::size_t indent) const override
          {
               std::string built = std::string(indent * PrettyIndent, ' ');
               for (const auto& section : this->_sections)
                    built += section.node->ToString(0) + (section.isTemplate ? "::template " : "::");

               return built + this->_unqualifiedType->ToString(0);
          }
          [[nodiscard]] const std::vector<Section>& Sections(void) const noexcept { return this->_sections; }
          [[nodiscard]] const NodePointer& UnqualifiedType(void) const noexcept { return this->_unqualifiedType; }

     private:
          std::vector<Section> _sections;
          NodePointer _unqualifiedType;
     };
} // namespace ecpps::ast