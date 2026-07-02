#include "Context.h"

ecpps::ir::Scope::~Scope(void) = default;
ecpps::ir::ContextBase::~ContextBase(void) = default;

ecpps::ir::TypeContext& ecpps::ir::GetTypeContext(void)
{
     static TypeContext typeContext{};
     return typeContext;
}

void ecpps::ir::FunctionContext::RegisterAllocReg(std::string registerName, const ir::SingleAssignRegisterNode* node)
{
     SSANode ssa{.pointer = node, .type = FunctionContext::NodeType::Local, .key = node->Index()};
     this->_nameToSSAMapping.emplace(std::move(registerName), ssa);
     this->_indexToSSAMapping.emplace(ssa.key, node);
}
void ecpps::ir::FunctionContext::RegisterParamAllocReg(std::string registerName,
                                                       const ir::SingleAssignRegisterNode* node)
{
     SSANode ssa{.pointer = node, .type = FunctionContext::NodeType::Parameter, .key = node->Index()};
     this->_nameToSSAMapping.emplace(std::move(registerName), ssa);
     this->_indexToSSAMapping.emplace(ssa.key, node);
}
[[nodiscard]] const ecpps::ir::SingleAssignRegisterNode* ecpps::ir::FunctionContext::GetParameterRegister(
    std::uint64_t index) const
{ return this->_indexToSSAMapping.at(index); }
[[nodiscard]] const ecpps::ir::SingleAssignRegisterNode* ecpps::ir::FunctionContext::GetAllocRegForName(
    const std::string& registerName) const
{ return this->_nameToSSAMapping.at(registerName).pointer; }
