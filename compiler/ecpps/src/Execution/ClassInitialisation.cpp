#include <RuntimeAssert.h>
#include <algorithm>
#include <bit>
#include <format>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "../TypeSystem/ArithmeticTypes.h"
#include "../TypeSystem/CompoundTypes.h"
#include "Constexpr.h"
#include "Context.h"
#include "Execution/Diagnostics.h"
#include "Execution/IR.Internal.h"
#include "Expressions.h"
#include "HighLevel.h"
#include "IR.h"
#include "NodeBase.h"
#include "Operations.h"
#include "OverloadDiagnostics.h"
#include "Parsing/AST.h"
#include "Procedural.h"
#include "Shared/BumpAllocator.h"
#include "Shared/Diagnostics.h"
#include "Shared/Error.h"
#include "TypeSystem/ClassInfo.h"

using ecpps::Expression;
using ClassProperty = ecpps::typeSystem::ClassInfo::Property;

namespace
{
     [[nodiscard]] std::string ConstructorAccessName(const ecpps::typeSystem::Access access)
     {
          switch (access)
          {
          case ecpps::typeSystem::Access::Public: return "public";
          case ecpps::typeSystem::Access::Protected: return "protected";
          case ecpps::typeSystem::Access::Private: return "private";
          }
          return "inaccessible";
     }

     [[nodiscard]] ecpps::ir::overloadDiagnostics::Candidate ConstructorCandidateDiagnostic(
          const ecpps::ir::ClassConstructor& constructor, const bool accessible)
     {
          ecpps::ir::overloadDiagnostics::Candidate candidate{
               .signature = ecpps::ir::detail::FormatFunctionSignature(constructor.function),
               .source = constructor.function->source,
               .isDeleted = constructor.bodyKind == ecpps::ast::FunctionBodyKind::Deleted};
          if (!accessible) candidate.access = ConstructorAccessName(constructor.access);
          return candidate;
     }

     [[nodiscard]] bool IsIgnoredDeletedMoveConstructor(const ecpps::ir::ClassConstructor& constructor)
     {
          return constructor.isMoveConstructor && constructor.function != nullptr &&
                 constructor.function->isDefaulted && constructor.bodyKind == ecpps::ast::FunctionBodyKind::Deleted;
     }

     [[nodiscard]] std::size_t ConversionCost(const ecpps::typeSystem::ConversionSequence::ConversionKind kind) noexcept
     {
          using Kind = ecpps::typeSystem::ConversionSequence::ConversionKind;
          switch (kind)
          {
          case Kind::LvalueToRValue:
          case Kind::ArrayToPointer:
          case Kind::FunctionToPointer:
          case Kind::Materialisation:
          case Kind::QualifierConversion: return 0;
          case Kind::IntegralPromotion:
          case Kind::FloatingPointPromotion: return 1;
          case Kind::IntegralConversion:
          case Kind::FloatingPointConversion:
          case Kind::FloatingIntegralConversion:
          case Kind::PointerConversion:
          case Kind::MemberFunctionConversion:
          case Kind::FunctionPointerConversion: return 2;
          case Kind::BooleanConversion: return 3;
          case Kind::UserDefined: return 4;
          case Kind::Ellipsis: return 5;
          }
          return 5;
     }

     ecpps::typeSystem::Qualifiers QualifiersFrom(const bool isConst, const bool isVolatile)
     {
          using ecpps::typeSystem::Qualifiers;
          if (isConst && isVolatile) return Qualifiers::ConstVolatile;
          return isConst ? Qualifiers::Const : isVolatile ? Qualifiers::Volatile : Qualifiers::None;
     }

     [[nodiscard]] bool IsSameUnqualifiedType(ecpps::typeSystem::NonowningTypePointer left,
                                              ecpps::typeSystem::NonowningTypePointer right)
     {
          if (left == right) return true;

          const auto* leftIntegral = left->CastTo<ecpps::typeSystem::IntegralType>();
          const auto* rightIntegral = right->CastTo<ecpps::typeSystem::IntegralType>();

          if (leftIntegral != nullptr || rightIntegral != nullptr)
               return leftIntegral != nullptr && rightIntegral != nullptr &&
                      leftIntegral->Sign() == rightIntegral->Sign() && leftIntegral->Kind() == rightIntegral->Kind() &&
                      ecpps::typeSystem::IsBoolean(left) == ecpps::typeSystem::IsBoolean(right);

          const auto* leftClass = left->CastTo<ecpps::typeSystem::ClassType>();
          const auto* rightClass = right->CastTo<ecpps::typeSystem::ClassType>();

          if (leftClass != nullptr || rightClass != nullptr)
               return leftClass != nullptr && rightClass != nullptr && leftClass->Info().id == rightClass->Info().id;

          const auto* leftPointer = left->CastTo<ecpps::typeSystem::PointerType>();
          const auto* rightPointer = right->CastTo<ecpps::typeSystem::PointerType>();

          if (leftPointer != nullptr || rightPointer != nullptr)
               return leftPointer != nullptr && rightPointer != nullptr &&
                      leftPointer->BaseType() == rightPointer->BaseType();

          return false;
     }

     struct ClassAssignmentRequirements
     {
          bool hasDeletedSubobject{};
          bool requiresMemberwiseAssignment{};
     };

     [[nodiscard]] ClassAssignmentRequirements AnalyseAssignmentSubobject(
          ecpps::typeSystem::NonowningTypePointer subobjectType)
     {
          if (subobjectType->CastTo<ecpps::typeSystem::ReferenceType>() != nullptr)
               return {.hasDeletedSubobject = true, .requiresMemberwiseAssignment = true};

          if (const auto* qualified = subobjectType->CastTo<ecpps::typeSystem::QualifiedType>();
              qualified != nullptr && qualified->IsConst())
               return {.hasDeletedSubobject = true, .requiresMemberwiseAssignment = true};

          if (const auto* arrayType = subobjectType->CastTo<ecpps::typeSystem::ArrayType>())
               return AnalyseAssignmentSubobject(arrayType->ElementType());

          const auto* subobjectClass = subobjectType->CastTo<ecpps::typeSystem::ClassType>();

          if (subobjectClass == nullptr) return {};

          if (subobjectClass->Info().Has(ClassProperty::UserDeclaredCopyAssignment) ||
              subobjectClass->Info().Has(ClassProperty::UserDeclaredMoveAssignment))
               return {.requiresMemberwiseAssignment = true};

          ClassAssignmentRequirements requirements{};
          for (const auto& field : subobjectClass->Info().fields)
          {
               const auto fieldRequirements = AnalyseAssignmentSubobject(field.type);
               requirements.hasDeletedSubobject =
                    requirements.hasDeletedSubobject || fieldRequirements.hasDeletedSubobject;
               requirements.requiresMemberwiseAssignment =
                    requirements.requiresMemberwiseAssignment || fieldRequirements.requiresMemberwiseAssignment;
          }
          return requirements;
     }

     [[nodiscard]] ClassAssignmentRequirements AnalyseClassAssignment(const ecpps::typeSystem::ClassInfo& info)
     {
          ClassAssignmentRequirements requirements{};
          for (const auto& field : info.fields)
          {
               const auto fieldRequirements = AnalyseAssignmentSubobject(field.type);
               requirements.hasDeletedSubobject =
                    requirements.hasDeletedSubobject || fieldRequirements.hasDeletedSubobject;
               requirements.requiresMemberwiseAssignment =
                    requirements.requiresMemberwiseAssignment || fieldRequirements.requiresMemberwiseAssignment;
          }
          return requirements;
     }

     [[nodiscard]] bool IsDesignatedInitialiserNode(const ecpps::ast::NodePointer& node)
     {
          return dynamic_cast<const ecpps::ast::DesignatedInitialiserNode*>(node.get()) != nullptr;
     }

     [[nodiscard]] bool IsFunctionContext(const ecpps::ir::ContextPointer& context)
     {
          return dynamic_cast<ecpps::ir::FunctionContext*>(context.get()) != nullptr;
     }
} // namespace

Expression ecpps::ir::IR::ParseAggregateInitialisation(const ast::NodePointer& expression,
                                                       [[maybe_unused]] typeSystem::NonowningTypePointer desiredType)
{
     DiagnosticSink::Report(
          this->GetContext(),
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               "Aggregate class initialisation must initialise an object directly", expression->Source()));
     return nullptr;
}

bool ecpps::ir::IR::IsAggregateClass(typeSystem::NonowningTypePointer type) const
{
     const auto* classType = type != nullptr ? type->CastTo<typeSystem::ClassType>() : nullptr;
     if (classType == nullptr || !classType->IsComplete() || classType->Info().kind == typeSystem::ClassKind::Union)
          return false;
     if (classType->Info().Has(ClassProperty::UserDeclaredConstructors)) return false;
     return std::ranges::all_of(classType->Info().fields,
                                [](const typeSystem::ClassField& field)
                                {
                                     return field.access == typeSystem::Access::Public && !field.isAnonymousUnion;
                                });
}

bool ecpps::ir::IR::IsConstDefaultConstructible(typeSystem::NonowningTypePointer type) const
{
     if (type == nullptr || type->CastTo<typeSystem::ReferenceType>() != nullptr) return false;
     if (const auto* array = type->CastTo<typeSystem::ArrayType>())
          return IsConstDefaultConstructible(array->ElementType());
     const auto* classType = type->CastTo<typeSystem::ClassType>();
     if (classType == nullptr || classType->Info().kind == typeSystem::ClassKind::Union) return false;

     if (classType->Info().Has(ClassProperty::UserDeclaredConstructors))
     {
          const auto classRecord = this->GetContext().classTable->classes.find(classType->Info().id);
          if (classRecord == this->GetContext().classTable->classes.end()) return false;
          bool hasUsableDefaultConstructor = false;
          for (const auto& constructor : classRecord->second.constructors)
          {
               if (!constructor.parameters.empty() || constructor.bodyKind == ast::FunctionBodyKind::Deleted) continue;
               if (constructor.bodyKind != ast::FunctionBodyKind::Defaulted) return true;
               hasUsableDefaultConstructor = true;
               break;
          }
          if (!hasUsableDefaultConstructor) return false;
     }

     return std::ranges::all_of(classType->Info().fields,
                                [this](const typeSystem::ClassField& field)
                                {
                                     return field.hasDefaultInitialiser || IsConstDefaultConstructible(field.type);
                                });
}

ecpps::typeSystem::NonowningTypePointer ecpps::ir::IR::PointerTo(ecpps::typeSystem::NonowningTypePointer type) const
{
     TypeRequest request{};
     request.kind = TypeKind::Compound;
     request.data = PointerRequest{.elementType = type};
     return GetTypeContext().Get(request);
}

const ecpps::ir::SingleAssignRegisterNode* ecpps::ir::IR::AddressOfAllocation(const SingleAssignRegisterNode* object,
                                                                              typeSystem::NonowningTypePointer type,
                                                                              const Location& source)
{
     runtime_assert(object != nullptr, "Invalid object allocation register");
     auto* function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(function != nullptr, "Object initialisation must occur inside a function");
     if (function == nullptr || object == nullptr) return nullptr;

     auto& allocator = *this->GetContext().nodeAllocator;
     RegisterPriorityInfo info{.regClass = RegisterClass::AddressTemporary};
     auto result = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
          function->GetNextRegisterIndex(), info, PointerTo(type)->Width(), PointerTo(type)->Alignment(), source)};
     const auto* resultPointer = result.get();
     this->_built.push_back(std::unique_ptr<SSAAddressOfNode, IRDeleter>{
          new (allocator) SSAAddressOfNode(std::move(result), object, source)});
     return resultPointer;
}

const ecpps::ir::SingleAssignRegisterNode* ecpps::ir::IR::AddByteOffset(const SingleAssignRegisterNode* address,
                                                                        const std::size_t offset,
                                                                        const Location& source)
{
     runtime_assert(address != nullptr, "Invalid object address register");
     auto* function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     if (function == nullptr)
          for (auto& context : std::ranges::reverse_view(this->GetContext().contextSequence))
          {
               function = dynamic_cast<FunctionContext*>(context.get());
               if (function != nullptr) break;
          }

     runtime_assert(function != nullptr, "Member addressing must occur inside a function");
     if (function == nullptr || address == nullptr) return nullptr;
     if (offset == 0) return address;

     TypeRequest sizeRequest{};
     sizeRequest.kind = TypeKind::Fundamental;
     sizeRequest.data = PlatformIntegerRequest{.kind = PlatformIntegerKind::Size};
     const auto* sizeType = GetTypeContext().Get(sizeRequest);
     auto& allocator = *this->GetContext().nodeAllocator;
     const RegisterPriorityInfo info{.regClass = RegisterClass::AddressTemporary};

     auto offsetValue = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
          function->GetNextRegisterIndex(), info, sizeType->Width(), sizeType->Alignment(), source)};
     const auto* offsetRegister = offsetValue.get();
     this->_built.push_back(
          std::unique_ptr<SSAImmNode, IRDeleter>{new (allocator) SSAImmNode(std::move(offsetValue), offset, source)});

     auto result = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
          function->GetNextRegisterIndex(), info, address->Width(), sizeType->Alignment(), source)};
     const auto* resultRegister = result.get();
     this->_built.push_back(std::unique_ptr<SSAAddNode, IRDeleter>{
          new (allocator) SSAAddNode(std::move(result), address, offsetRegister, source)});
     return resultRegister;
}

void ecpps::ir::IR::CopyClassAtAddress(typeSystem::NonowningTypePointer type,
                                       const SingleAssignRegisterNode* destination,
                                       const SingleAssignRegisterNode* source, const Location& location,
                                       const bool moveFromSource, const typeSystem::Qualifiers sourceQualifiers)
{
     if (type == nullptr || destination == nullptr || source == nullptr) return;
     auto& allocator = *this->GetContext().nodeAllocator;

     if (const auto* classType = type->CastTo<typeSystem::ClassType>())
     {
          const auto& info = classType->Info();
          if (info.triviallyCopyable)
          {
               CopyAggregateAtAddress(type, destination, source, location);
               return;
          }
          if (info.kind == typeSystem::ClassKind::Union)
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Copying a non-trivially-copyable union is not supported yet", location));
               return;
          }
          for (const auto& field : info.fields)
          {
               const auto* destinationField = AddByteOffset(destination, field.offset, field.source);
               const auto* sourceField = AddByteOffset(source, field.offset, field.source);
               const bool sourceIsConst =
                    (std::to_underlying(sourceQualifiers) & std::to_underlying(typeSystem::Qualifiers::Const)) != 0;
               const bool sourceIsVolatile =
                    (std::to_underlying(sourceQualifiers) & std::to_underlying(typeSystem::Qualifiers::Volatile)) != 0;
               const auto fieldSourceQualifiers = QualifiersFrom(sourceIsConst && !field.isMutable, sourceIsVolatile);
               const auto* fieldSourceType = AddQualifiers(field.type, fieldSourceQualifiers);
               if (typeSystem::IsClass(field.type))
               {
                    Expression fieldSource =
                         moveFromSource
                              ? Expression{std::make_unique<XValue>(
                                     fieldSourceType,
                                     std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
                                          new (allocator) SSARegisterReferenceNode(sourceField, field.source)},
                                     false)}
                              : Expression{std::make_unique<LValue>(
                                     fieldSourceType,
                                     std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
                                          new (allocator) SSARegisterReferenceNode(sourceField, field.source)},
                                     false)};
                    InitialiseClassFromExpression(field.type, destinationField, std::move(fieldSource), field.source);
               }
               else
                    CopyClassAtAddress(field.type, destinationField, sourceField, field.source, false,
                                       fieldSourceQualifiers);
          }
          return;
     }

     if (const auto* arrayType = type->CastTo<typeSystem::ArrayType>())
     {
          for (std::size_t index = 0; index < arrayType->ElementCount(); index++)
          {
               const auto offset = index * arrayType->ElementType()->Size();
               if (typeSystem::IsClass(arrayType->ElementType()))
               {
                    const auto* destinationElement = AddByteOffset(destination, offset, location);
                    const auto* sourceElement = AddByteOffset(source, offset, location);
                    const auto* elementSourceType = AddQualifiers(arrayType->ElementType(), sourceQualifiers);
                    Expression elementSource =
                         moveFromSource ? Expression{std::make_unique<XValue>(
                                               elementSourceType,
                                               std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
                                                    new (allocator) SSARegisterReferenceNode(sourceElement, location)},
                                               false)}
                                        : Expression{std::make_unique<LValue>(
                                               elementSourceType,
                                               std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
                                                    new (allocator) SSARegisterReferenceNode(sourceElement, location)},
                                               false)};
                    InitialiseClassFromExpression(arrayType->ElementType(), destinationElement,
                                                  std::move(elementSource), location);
               }
               else
                    CopyClassAtAddress(arrayType->ElementType(), AddByteOffset(destination, offset, location),
                                       AddByteOffset(source, offset, location), location, moveFromSource,
                                       sourceQualifiers);
          }
          return;
     }

     CopyAggregateAtAddress(type, destination, source, location);
}

void ecpps::ir::IR::CopyAggregateAtAddress(typeSystem::NonowningTypePointer type,
                                           const SingleAssignRegisterNode* destination,
                                           const SingleAssignRegisterNode* source, const Location& location)
{
     if (type == nullptr || destination == nullptr || source == nullptr) return;
     auto* const function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(function != nullptr, "Class copying must occur inside a function");
     if (function == nullptr) return;

     RegisterPriorityInfo info{.regClass = RegisterClass::Temporary};
     auto value = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{
          new (*this->GetContext().nodeAllocator) SingleAssignRegisterNode(function->GetNextRegisterIndex(), info,
                                                                           type->Width(), type->Alignment(), location)};
     auto* const valuePointer = value.get();
     this->_built.push_back(std::unique_ptr<SSADerefNode, IRDeleter>{
          new (*this->GetContext().nodeAllocator)
               SSADerefNode(std::move(value), source, detail::AccessFor(type), location)});
     this->_built.push_back(std::unique_ptr<SSAIndirectStoreNode, IRDeleter>{
          new (*this->GetContext().nodeAllocator)
               SSAIndirectStoreNode(destination, valuePointer, detail::AccessFor(type), location)});
}

void ecpps::ir::IR::InitialiseClassFromExpression(typeSystem::NonowningTypePointer type,
                                                  const SingleAssignRegisterNode* address, Expression initialiser,
                                                  const Location& source, const bool allowExplicit)
{
     const bool sourceIsRValue = initialiser != nullptr && initialiser->IsRValue();
     const auto* targetClass = type != nullptr ? type->CastTo<typeSystem::ClassType>() : nullptr;
     const auto* sourceClass = initialiser != nullptr ? initialiser->Type()->CastTo<typeSystem::ClassType>() : nullptr;
     if (targetClass == nullptr || sourceClass == nullptr || targetClass->Info().id != sourceClass->Info().id)
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      "Class copy initialisation requires a source of the same class type", source));
          return;
     }

     if (initialiser->IsPRValue())
          if (auto* call = dynamic_cast<FunctionCallNode*>(initialiser->Value().get());
              call != nullptr && call->Function()->returnsByPointer)
          {
               LowerClassFunctionCallInto(*call, address, source, this->_built);
               return;
          }

     const auto& info = targetClass->Info();
     const auto classIr = this->GetContext().classTable->classes.find(info.id);
     const ClassConstructor* selected{};
     std::vector<const ClassConstructor*> bestCandidates{};
     std::size_t bestScore = std::numeric_limits<std::size_t>::max();
     bool ambiguous = false;
     if (classIr != this->GetContext().classTable->classes.end())
          for (const auto& candidate : classIr->second.constructors)
          {
               if (!candidate.isCopyConstructor && !(initialiser->IsRValue() && candidate.isMoveConstructor)) continue;
               if (IsIgnoredDeletedMoveConstructor(candidate)) continue;
               if (candidate.isExplicit && !allowExplicit) continue;
               if (candidate.parameters.size() != 1) continue;
               const auto cost = ConstructorArgumentCost(candidate.parameters.front().type, initialiser);
               if (!cost.has_value()) continue;
               if (*cost < bestScore)
               {
                    selected = &candidate;
                    bestScore = *cost;
                    ambiguous = false;
                    bestCandidates.assign(1, &candidate);
               }
               else if (*cost == bestScore)
               {
                    ambiguous = true;
                    bestCandidates.push_back(&candidate);
               }
          }

     if (ambiguous)
     {
          std::vector<ecpps::ir::overloadDiagnostics::Candidate> candidates{};
          candidates.reserve(bestCandidates.size());
          for (const auto* candidate : bestCandidates)
               candidates.push_back(
                    ConstructorCandidateDiagnostic(*candidate, this->CanAccess(info.id, candidate->access)));
          DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::AmbiguousConstructor(
                                                          info.DisplayName(), source, candidates));
          return;
     }

     if (selected != nullptr)
     {
          const auto diagnosticCandidate =
               ConstructorCandidateDiagnostic(*selected, this->CanAccess(info.id, selected->access));
          if (!this->CanAccess(info.id, selected->access) || selected->bodyKind == ast::FunctionBodyKind::Deleted)
          {
               DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::SelectedConstructorFailure(
                                                               selected->bodyKind == ast::FunctionBodyKind::Deleted
                                                                    ? "The selected constructor is deleted"
                                                                    : "Selected constructor is not accessible",
                                                               source, diagnosticCandidate));
               return;
          }
          if (selected->bodyKind == ast::FunctionBodyKind::Defaulted)
          {
               if (sourceClass->IsVolatile())
               {
                    DiagnosticSink::Report(
                         this->GetContext(),
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "Defaulted copy or move construction from a volatile class is not supported yet",
                              source));
                    return;
               }
               auto sourceObject = initialiser->IsGLValue() ? ParseAddressOfExpression(std::move(initialiser), source)
                                                            : std::move(initialiser);
               const auto* sourceAddress = LowerExpressionLoaded(std::move(sourceObject), this->_built);
               CopyClassAtAddress(type, address, sourceAddress, source, selected->isMoveConstructor,
                                  sourceClass->qualifiers());
               return;
          }

          if (selected->function == nullptr)
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "The selected copy or move constructor has no definition", source));
               return;
          }
          auto sourceArgument =
               PrepareByValueArgument(std::move(initialiser), selected->parameters.front().type, source);
          if (sourceArgument == nullptr) return;
          const auto* unqualifiedClass =
               GetTypeContext().Get(TypeRequest{.kind = TypeKind::Compound, .data = ClassRequest{.id = info.id}});
          std::vector<Expression> arguments{};
          arguments.push_back(std::make_unique<PRValue>(
               PointerTo(unqualifiedClass),
               std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                                         SSARegisterReferenceNode(address, source)},
               false));
          arguments.push_back(std::move(sourceArgument));
          auto call = std::unique_ptr<FunctionCallNode, IRDeleter>{new (
               *this->GetContext().nodeAllocator) FunctionCallNode(selected->function, std::move(arguments), source)};
          [[maybe_unused]] const auto* copyConstructorCallResult = LowerExpression(
               std::make_unique<PRValue>(typeSystem::g_void.get(), std::move(call), false), this->_built);
          return;
     }

     if (info.Has(ClassProperty::UserDeclaredCopyConstructor) || info.Has(ClassProperty::UserDeclaredMoveConstructor) ||
         sourceClass->IsVolatile())
     {
          std::vector<ecpps::ir::overloadDiagnostics::Candidate> candidates{};
          if (classIr != this->GetContext().classTable->classes.end())
               for (const auto& candidate : classIr->second.constructors)
               {
                    if (!candidate.isCopyConstructor && !candidate.isMoveConstructor) continue;
                    auto diagnosticCandidate =
                         ConstructorCandidateDiagnostic(candidate, this->CanAccess(info.id, candidate.access));
                    if (!allowExplicit && candidate.isExplicit)
                         diagnosticCandidate.details.push_back(
                              {.message = "This explicit constructor is not considered by copy-initialisation",
                               .source = candidate.function->source});
                    candidates.push_back(std::move(diagnosticCandidate));
               }
          DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::NoViableCopyOrMoveConstructor(
                                                          info.DisplayName(), source, candidates));
          return;
     }
     auto sourceObject =
          initialiser->IsGLValue() ? ParseAddressOfExpression(std::move(initialiser), source) : std::move(initialiser);
     const auto* sourceAddress = LowerExpressionLoaded(std::move(sourceObject), this->_built);
     const bool implicitMove = sourceClass != nullptr && sourceIsRValue && !sourceClass->IsConst() &&
                               !sourceClass->IsVolatile() &&
                               !targetClass->Info().Has(ClassProperty::UserDeclaredCopyConstructor) &&
                               !targetClass->Info().Has(ClassProperty::UserDeclaredMoveConstructor) &&
                               !targetClass->Info().Has(ClassProperty::UserDeclaredCopyAssignment) &&
                               !targetClass->Info().Has(ClassProperty::UserDeclaredMoveAssignment) &&
                               !targetClass->Info().Has(ClassProperty::UserDeclaredDestructor);
     CopyClassAtAddress(type, address, sourceAddress, source, implicitMove, sourceClass->qualifiers());
}

ecpps::Expression ecpps::ir::IR::PrepareByValueArgument(Expression argument,
                                                        typeSystem::NonowningTypePointer parameterType,
                                                        const Location& source)
{
     if (parameterType != nullptr && parameterType->CastTo<typeSystem::ReferenceType>() != nullptr)
     {
          if (!ConstructorArgumentCost(parameterType, argument).has_value())
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         argument == nullptr
                              ? "Cannot bind an invalid argument to a reference parameter"
                              : std::format("Cannot bind argument of type '{}' to reference parameter '{}'",
                                            argument->Type()->Name(), parameterType->Name()),
                         source));
               return nullptr;
          }
          const auto* referenceAddress = BindReferenceExpression(parameterType, std::move(argument), source, true);
          if (referenceAddress == nullptr) return nullptr;
          return std::make_unique<PRValue>(parameterType,
                                           std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
                                                new (*this->GetContext().nodeAllocator)
                                                     SSARegisterReferenceNode(referenceAddress, source)},
                                           false);
     }
     if (!typeSystem::IsClass(parameterType)) return ConvertTo(std::move(argument), parameterType);
     if (argument == nullptr)
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "A class by-value argument has an invalid source", source));
          return nullptr;
     }

     const auto* parameterClass = parameterType->CastTo<typeSystem::ClassType>();
     const auto* argumentClass = argument->Type()->CastTo<typeSystem::ClassType>();
     if (parameterClass == nullptr)
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Invalid class by-value parameter type", source));
          return nullptr;
     }

     auto* function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(function != nullptr, "A class argument temporary requires a function context");
     if (function == nullptr) return nullptr;
     auto& allocator = *this->GetContext().nodeAllocator;
     RegisterPriorityInfo info{.regClass = RegisterClass::LocalVariable};
     auto storage = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
          function->GetNextRegisterIndex(), info, parameterType->Width(), parameterType->Alignment(), source)};
     auto* storagePointer = storage.get();
     this->_built.push_back(
          std::unique_ptr<AllocationNode, IRDeleter>{new (allocator) AllocationNode(std::move(storage), source)});
     const auto* address = AddressOfAllocation(storagePointer, parameterType, source);
     if (argumentClass != nullptr && parameterClass->Info().id == argumentClass->Info().id)
          InitialiseClassFromExpression(parameterType, address, std::move(argument), source, false);
     else
     {
          std::vector<Expression> constructorArguments{};
          constructorArguments.push_back(std::move(argument));
          auto selection = SelectConstructor(parameterClass->Info(), std::move(constructorArguments), source, false);
          if (!selection.has_value()) return nullptr;
          const bool accessible = this->CanAccess(parameterClass->Info().id, selection->constructor->access);
          if (!accessible || selection->constructor->bodyKind == ast::FunctionBodyKind::Deleted)
          {
               const auto diagnosticCandidate = ConstructorCandidateDiagnostic(*selection->constructor, accessible);
               DiagnosticSink::Report(this->GetContext(),
                                      ecpps::ir::overloadDiagnostics::SelectedConstructorFailure(
                                           selection->constructor->bodyKind == ast::FunctionBodyKind::Deleted
                                                ? "The selected constructor is deleted"
                                                : "Selected constructor is not accessible",
                                           source, diagnosticCandidate));
               return nullptr;
          }
          ExecuteConstructor(parameterType, address, parameterClass->Info(), std::move(*selection),
                             ObjectInitialisationMode::Explicit, nullptr, source);
     }
     return std::make_unique<PRValue>(parameterType,
                                      std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
                                           new (allocator) SSARegisterReferenceNode(address, source)},
                                      false);
}

void ecpps::ir::IR::AssignClassAtAddress(typeSystem::NonowningTypePointer type,
                                         const SingleAssignRegisterNode* destination, Expression sourceExpression,
                                         const Location& location)
{
     const bool sourceIsRValue = sourceExpression != nullptr && sourceExpression->IsRValue();
     const auto* classType = type != nullptr ? type->CastTo<typeSystem::ClassType>() : nullptr;
     const auto* sourceType =
          sourceExpression != nullptr ? sourceExpression->Type()->CastTo<typeSystem::ClassType>() : nullptr;
     if (classType == nullptr || sourceType == nullptr || classType->Info().id != sourceType->Info().id)
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      "Class assignment requires a source of the same class type", location));
          return;
     }

     const auto& classInfo = classType->Info();
     const auto classIr = this->GetContext().classTable->classes.find(classInfo.id);
     std::shared_ptr<FunctionScope> selected{};
     bool memberwiseMove = false;
     std::size_t bestScore = std::numeric_limits<std::size_t>::max();
     std::size_t bestObjectQualificationPenalty = std::numeric_limits<std::size_t>::max();
     bool ambiguous = false;
     std::vector<std::shared_ptr<FunctionScope>> tiedCandidates{};
     if (classIr != this->GetContext().classTable->classes.end() && classIr->second.scope != nullptr)
          for (const auto& candidate : classIr->second.scope->functions)
          {
               if (candidate->Name() != "operator=" || candidate->isStatic || candidate->parameters.size() != 1)
                    continue;
               if (classType->IsConst() && !candidate->memberFunctionInfo.isConst) continue;
               if (classType->IsVolatile() && !candidate->memberFunctionInfo.isVolatile) continue;
               const auto cost = ConstructorArgumentCost(candidate->parameters.front().type, sourceExpression);
               if (!cost.has_value()) continue;
               const auto objectQualificationPenalty =
                    static_cast<std::size_t>(!classType->IsConst() && candidate->memberFunctionInfo.isConst) +
                    static_cast<std::size_t>(!classType->IsVolatile() && candidate->memberFunctionInfo.isVolatile);
               if (*cost < bestScore ||
                   (*cost == bestScore && objectQualificationPenalty < bestObjectQualificationPenalty))
               {
                    selected = candidate;
                    bestScore = *cost;
                    bestObjectQualificationPenalty = objectQualificationPenalty;
                    ambiguous = false;
                    tiedCandidates.assign(1, candidate);
               }
               else if (*cost == bestScore && objectQualificationPenalty == bestObjectQualificationPenalty)
               {
                    ambiguous = true;
                    tiedCandidates.push_back(candidate);
               }
          }

     if (ambiguous)
     {
          std::vector<ecpps::ir::overloadDiagnostics::Candidate> candidates{};
          candidates.reserve(tiedCandidates.size());
          for (const auto& candidate : tiedCandidates)
          {
               ecpps::ir::overloadDiagnostics::Candidate diagnostic{
                    .signature = detail::FormatFunctionSignature(candidate), .source = candidate->source};
               if (!this->CanAccess(classInfo.id, candidate->memberAccess))
                    diagnostic.access = ConstructorAccessName(candidate->memberAccess);
               candidates.push_back(std::move(diagnostic));
          }
          DiagnosticSink::Report(this->GetContext(),
                                 ecpps::ir::overloadDiagnostics::AmbiguousOverload(
                                      classInfo.DisplayName() + "::operator=", location, candidates));
          return;
     }

     if (selected != nullptr)
     {
          if (selected->isDefaulted)
          {
               if (classType->IsVolatile() || sourceType->IsVolatile())
               {
                    DiagnosticSink::Report(
                         this->GetContext(),
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "Defaulted copy or move assignment for volatile class operands is not supported yet",
                              location));
                    return;
               }
               if (const auto* reference = selected->parameters.front().type->CastTo<typeSystem::ReferenceType>())
                    memberwiseMove = reference->GetKind() == typeSystem::ReferenceType::Kind::RValue;
          }
          ecpps::ir::overloadDiagnostics::Candidate diagnostic{.signature = detail::FormatFunctionSignature(selected),
                                                               .source = selected->source};
          const bool accessible = this->CanAccess(classInfo.id, selected->memberAccess);
          if (!accessible) diagnostic.access = ConstructorAccessName(selected->memberAccess);
          if (!accessible || selected->isDeleted)
          {
               DiagnosticSink::Report(this->GetContext(), ecpps::ir::overloadDiagnostics::SelectedOverloadFailure(
                                                               diagnostic.signature, location, !accessible,
                                                               selected->isDeleted, diagnostic));
               return;
          }
          if (!selected->isDefaulted)
          {
               auto sourceArgument =
                    PrepareByValueArgument(std::move(sourceExpression), selected->parameters.front().type, location);
               if (sourceArgument == nullptr) return;
               const auto* unqualifiedClass = GetTypeContext().Get(
                    TypeRequest{.kind = TypeKind::Compound, .data = ClassRequest{.id = classInfo.id}});
               std::vector<Expression> arguments{};
               arguments.push_back(std::make_unique<PRValue>(PointerTo(unqualifiedClass),
                                                             std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
                                                                  new (*this->GetContext().nodeAllocator)
                                                                       SSARegisterReferenceNode(destination, location)},
                                                             false));
               arguments.push_back(std::move(sourceArgument));
               auto call = std::unique_ptr<FunctionCallNode, IRDeleter>{
                    new (*this->GetContext().nodeAllocator) FunctionCallNode(selected, std::move(arguments), location)};
               [[maybe_unused]] const auto* assignmentCallResult = LowerExpression(
                    std::make_unique<PRValue>(selected->returnType, std::move(call), false), this->_built);
               return;
          }
     }
     else if (classInfo.Has(ClassProperty::UserDeclaredCopyAssignment) ||
              classInfo.Has(ClassProperty::UserDeclaredMoveAssignment))
     {
          DiagnosticSink::Report(
               this->GetContext(),
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("No viable copy or move assignment operator for '{}'", classInfo.DisplayName()),
                    location));
          return;
     }
     else
     {
          if (classType->IsConst())
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Implicit copy or move assignment cannot assign to a const class object", location));
               return;
          }
          if (classType->IsVolatile() || sourceType->IsVolatile())
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Implicit copy or move assignment cannot bind a volatile class operand", location));
               return;
          }
          memberwiseMove = sourceIsRValue && !classInfo.Has(ClassProperty::UserDeclaredCopyConstructor) &&
                           !sourceType->IsConst() && !sourceType->IsVolatile() &&
                           !classInfo.Has(ClassProperty::UserDeclaredMoveConstructor) &&
                           !classInfo.Has(ClassProperty::UserDeclaredCopyAssignment) &&
                           !classInfo.Has(ClassProperty::UserDeclaredMoveAssignment) &&
                           !classInfo.Has(ClassProperty::UserDeclaredDestructor);
     }

     auto sourceObject = sourceExpression->IsGLValue() ? ParseAddressOfExpression(std::move(sourceExpression), location)
                                                       : std::move(sourceExpression);
     const auto* sourceAddress = LowerExpressionLoaded(std::move(sourceObject), this->_built);
     if (sourceAddress == nullptr) return;
     const auto assignmentRequirements = AnalyseClassAssignment(classInfo);
     if (assignmentRequirements.hasDeletedSubobject)
     {
          DiagnosticSink::Report(
               this->GetContext(),
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Implicit copy assignment for '{}' is deleted", classInfo.DisplayName()), location));
          return;
     }
     if (classInfo.triviallyCopyable && !assignmentRequirements.requiresMemberwiseAssignment)
     {
          CopyClassAtAddress(type, destination, sourceAddress, location);
          return;
     }
     if (classInfo.kind == typeSystem::ClassKind::Union)
     {
          DiagnosticSink::Report(
               this->GetContext(),
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Copy assignment for a non-trivially-copyable union is not supported yet", location));
          return;
     }
     for (const auto& field : classInfo.fields)
     {
          const auto* destinationField = AddByteOffset(destination, field.offset, field.source);
          const auto* sourceField = AddByteOffset(sourceAddress, field.offset, field.source);
          const auto fieldSourceQualifiers =
               QualifiersFrom(sourceType->IsConst() && !field.isMutable, sourceType->IsVolatile());
          AssignClassSubobjectAtAddress(field.type, destinationField, sourceField, field.source, memberwiseMove,
                                        fieldSourceQualifiers);
     }
}

void ecpps::ir::IR::AssignClassSubobjectAtAddress(typeSystem::NonowningTypePointer type,
                                                  const SingleAssignRegisterNode* destination,
                                                  const SingleAssignRegisterNode* source, const Location& location,
                                                  const bool moveFromSource,
                                                  const typeSystem::Qualifiers sourceQualifiers)
{
     if (const auto* arrayType = type->CastTo<typeSystem::ArrayType>())
     {
          for (std::size_t index = 0; index < arrayType->ElementCount(); index++)
          {
               const auto offset = index * arrayType->ElementType()->Size();
               AssignClassSubobjectAtAddress(arrayType->ElementType(), AddByteOffset(destination, offset, location),
                                             AddByteOffset(source, offset, location), location, moveFromSource,
                                             sourceQualifiers);
          }
          return;
     }
     if (typeSystem::IsClass(type))
     {
          auto& allocator = *this->GetContext().nodeAllocator;
          const auto* sourceType = AddQualifiers(type, sourceQualifiers);
          auto reference = std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
               new (allocator) SSARegisterReferenceNode(source, location)};
          Expression sourceExpression =
               moveFromSource ? Expression{std::make_unique<XValue>(sourceType, std::move(reference), false)}
                              : Expression{std::make_unique<LValue>(sourceType, std::move(reference), false)};
          AssignClassAtAddress(type, destination, std::move(sourceExpression), location);
          return;
     }
     CopyClassAtAddress(type, destination, source, location, false, sourceQualifiers);
}

void ecpps::ir::IR::RegisterObjectForDestruction(typeSystem::NonowningTypePointer type,
                                                 const SingleAssignRegisterNode* address, const Location& source,
                                                 const ABICleanupKind abiCleanupKind)
{
     if (type == nullptr || address == nullptr) return;
     const auto* destructionType = type;
     while (const auto* arrayType = destructionType->CastTo<typeSystem::ArrayType>())
          destructionType = arrayType->ElementType();
     const auto* classType = destructionType->CastTo<typeSystem::ClassType>();
     if (classType == nullptr || !classType->Info().Has(ClassProperty::NontrivialDestructor)) return;
     auto* function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(function != nullptr, "Object destruction registration requires a function context");
     if (function != nullptr)
          function->automaticObjects.push_back(
               {.type = type, .address = address, .source = source, .abiCleanupKind = abiCleanupKind});
}

void ecpps::ir::IR::RegisterByValueParameterForDestruction(typeSystem::NonowningTypePointer type,
                                                           const SingleAssignRegisterNode* parameterStorage,
                                                           const Location& source)
{
     if (!typeSystem::IsClass(type) || parameterStorage == nullptr) return;
     auto* function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(function != nullptr, "Parameter destruction registration requires a function context");
     if (function == nullptr) return;
     auto& allocator = *this->GetContext().nodeAllocator;
     RegisterPriorityInfo info{.regClass = RegisterClass::AddressTemporary};
     auto pointer = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
          function->GetNextRegisterIndex(), info, abi::ABI::Current().PointerSize() * typeSystem::CharWidth,
          abi::ABI::Current().PointerSize(), source)};
     auto* pointerAddress = pointer.get();
     this->_built.push_back(std::unique_ptr<SSALoadNode, IRDeleter>{new (allocator) SSALoadNode(
          std::move(pointer), parameterStorage, detail::AccessFor(PointerTo(type)), source)});
     RegisterObjectForDestruction(type, pointerAddress, source, ABICleanupKind::ByValueParameter);
}

void ecpps::ir::IR::RegisterTemporaryForDestruction(typeSystem::NonowningTypePointer type,
                                                    const SingleAssignRegisterNode* address, const Location& source,
                                                    const ABICleanupKind abiCleanupKind)
{
     if (type == nullptr || address == nullptr) return;
     auto* function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(function != nullptr, "Temporary destruction registration requires a function context");
     if (function == nullptr) return;
     const auto* destructionType = type;
     while (const auto* arrayType = destructionType->CastTo<typeSystem::ArrayType>())
          destructionType = arrayType->ElementType();
     const auto* classType = destructionType->CastTo<typeSystem::ClassType>();
     if (classType == nullptr || !classType->Info().Has(ClassProperty::NontrivialDestructor)) return;
     function->temporaryObjects.push_back(
          {.type = type, .address = address, .source = source, .abiCleanupKind = abiCleanupKind});
}

void ecpps::ir::IR::EmitDestructorsFrom(const std::size_t depth)
{
     auto* function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(function != nullptr, "Object destruction requires a function context");
     if (function == nullptr) return;
     for (std::size_t index = function->automaticObjects.size(); index-- > depth;)
     {
          const auto& object = function->automaticObjects[index];
          DestroyObjectAtAddress(object.type, object.address, object.source, object.abiCleanupKind);
     }
}

void ecpps::ir::IR::EmitTemporaryDestructorsFrom(const std::size_t depth)
{
     auto* function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(function != nullptr, "Temporary destruction requires a function context");
     if (function == nullptr) return;
     for (std::size_t index = function->temporaryObjects.size(); index-- > depth;)
     {
          const auto& object = function->temporaryObjects[index];
          DestroyObjectAtAddress(object.type, object.address, object.source, object.abiCleanupKind);
     }
}

void ecpps::ir::IR::DestroyObjectAtAddress(typeSystem::NonowningTypePointer type,
                                           const SingleAssignRegisterNode* address, const Location& source,
                                           const ABICleanupKind abiCleanupKind)
{
     if (type == nullptr || address == nullptr) return;
     auto& allocator = *this->GetContext().nodeAllocator;
     if (const auto* arrayType = type->CastTo<typeSystem::ArrayType>())
     {
          for (std::size_t index = arrayType->ElementCount(); index-- > 0;)
               DestroyObjectAtAddress(arrayType->ElementType(),
                                      AddByteOffset(address, index * arrayType->ElementType()->Size(), source), source,
                                      abiCleanupKind);
          return;
     }

     const auto* classType = type->CastTo<typeSystem::ClassType>();
     if (classType == nullptr) return;
     const auto classIr = this->GetContext().classTable->classes.find(classType->Info().id);
     if (classIr != this->GetContext().classTable->classes.end() && classIr->second.destructor != nullptr &&
         classIr->second.destructorBodyKind != ast::FunctionBodyKind::Defaulted)
     {
          const auto& destructor = classIr->second.destructor;
          const bool isDeleted =
               classIr->second.destructorBodyKind == ast::FunctionBodyKind::Deleted || destructor->isDeleted;
          const bool accessible = this->CanAccess(classType->Info().id, classIr->second.destructorAccess);
          if (isDeleted || !accessible)
          {
               ecpps::ir::overloadDiagnostics::Candidate candidate{
                    .signature = ecpps::ir::detail::FormatFunctionSignature(destructor),
                    .source = destructor->source,
                    .isDeleted = isDeleted};
               if (!accessible) candidate.access = ConstructorAccessName(classIr->second.destructorAccess);
               DiagnosticSink::Report(this->GetContext(),
                                      ecpps::ir::overloadDiagnostics::SelectedDestructorFailure(
                                           classType->Info().DisplayName(), source, !accessible, isDeleted, candidate));
               return;
          }
          const auto* unqualifiedClass = GetTypeContext().Get(
               TypeRequest{.kind = TypeKind::Compound, .data = ClassRequest{.id = classType->Info().id}});
          std::vector<Expression> arguments{};
          arguments.push_back(std::make_unique<PRValue>(PointerTo(unqualifiedClass),
                                                        std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{
                                                             new (allocator) SSARegisterReferenceNode(address, source)},
                                                        false));
          auto call = std::unique_ptr<FunctionCallNode, IRDeleter>{
               new (allocator) FunctionCallNode(destructor, std::move(arguments), source, abiCleanupKind)};
          [[maybe_unused]] const auto* destructorCallResult = LowerExpression(
               std::make_unique<PRValue>(typeSystem::g_void.get(), std::move(call), false), this->_built);
          return;
     }
     if (classType->Info().kind == typeSystem::ClassKind::Union)
     {
          if (classType->Info().Has(ClassProperty::NontrivialDestructor))
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Implicit destruction of a union with non-trivial members is not supported", source));
          return;
     }
     DestroyClassMembersAtAddress(classType->Info(), address, abiCleanupKind);
}

void ecpps::ir::IR::DestroyClassMembersAtAddress(const typeSystem::ClassInfo& info,
                                                 const SingleAssignRegisterNode* address,
                                                 const ABICleanupKind abiCleanupKind)
{
     if (address == nullptr || info.kind == typeSystem::ClassKind::Union) return;
     const auto classIr = this->GetContext().classTable->classes.find(info.id);
     const bool addClassContext =
          classIr != this->GetContext().classTable->classes.end() && classIr->second.scope != nullptr;
     std::size_t classContextIndex = this->GetContext().contextSequence.size();
     if (addClassContext)
     {
          const auto functionContext =
               std::ranges::find_if(this->GetContext().contextSequence | std::views::reverse, IsFunctionContext);
          if (functionContext != (this->GetContext().contextSequence | std::views::reverse).end())
               classContextIndex = static_cast<std::size_t>(
                    std::distance(this->GetContext().contextSequence.begin(), functionContext.base()) - 1);
          this->GetContext().contextSequence.insert(this->GetContext().contextSequence.begin() +
                                                         static_cast<std::ptrdiff_t>(classContextIndex),
                                                    std::make_shared<ClassContext>(classIr->second.scope, info.id));
     }
     struct RestoreContext
     {
          Context* context{};
          std::size_t classContextIndex{};
          bool removeClassContext{};
          ~RestoreContext()
          {
               if (this->removeClassContext)
                    this->context->contextSequence.erase(this->context->contextSequence.begin() +
                                                         static_cast<std::ptrdiff_t>(this->classContextIndex));
          }
     } restoreContext{.context = &this->GetContext(),
                      .classContextIndex = classContextIndex,
                      .removeClassContext = addClassContext};

     for (const auto& field : info.fields | std::views::reverse)
          DestroyObjectAtAddress(field.type, AddByteOffset(address, field.offset, field.source), field.source,
                                 abiCleanupKind);
}

bool ecpps::ir::IR::CanAccess(const typeSystem::ClassId id, const typeSystem::Access access) const
{
     if (access == typeSystem::Access::Public) return true;
     return std::ranges::any_of(this->GetContext().contextSequence,
                                [id](const ContextPointer& context)
                                {
                                     const auto* classContext = dynamic_cast<const ClassContext*>(context.get());
                                     return classContext != nullptr && classContext->classId == id;
                                });
}

void ecpps::ir::IR::MarkDefaultedCopyMoveConstructorsDeleted(const typeSystem::ClassId id)
{
     auto classRecord = this->GetContext().classTable->classes.find(id);
     if (classRecord == this->GetContext().classTable->classes.end()) return;
     const auto& ownerInfo = GetTypeContext().Class(id);
     if (ownerInfo.kind == typeSystem::ClassKind::Union) return; // TODO: Implement

     for (auto& constructor : classRecord->second.constructors)
     {
          if (constructor.bodyKind != ast::FunctionBodyKind::Defaulted ||
              (!constructor.isCopyConstructor && !constructor.isMoveConstructor))
               continue;
          const auto* parameter = constructor.parameters.empty()
                                       ? nullptr
                                       : constructor.parameters.front().type->CastTo<typeSystem::ReferenceType>();
          if (parameter == nullptr)
          {
               constructor.bodyKind = ast::FunctionBodyKind::Deleted;
               if (constructor.function != nullptr) constructor.function->isDeleted = true;
               continue;
          }
          const auto* qualifiedSource = parameter->BaseType()->CastTo<typeSystem::QualifiedType>();
          bool isDeleted = false;
          for (const auto& field : ownerInfo.fields)
          {
               if (field.isAnonymousUnion) continue;
               if (constructor.isCopyConstructor)
                    if (const auto* reference = field.type->CastTo<typeSystem::ReferenceType>();
                        reference != nullptr && reference->GetKind() == typeSystem::ReferenceType::Kind::RValue)
                    {
                         isDeleted = true;
                         break;
                    }

               const auto* memberType = field.type;
               while (const auto* arrayType = memberType->CastTo<typeSystem::ArrayType>())
                    memberType = arrayType->ElementType();
               const auto* memberClass = memberType->CastTo<typeSystem::ClassType>();
               if (memberClass == nullptr) continue;
               const auto memberRecord = this->GetContext().classTable->classes.find(memberClass->Info().id);
               if (memberRecord == this->GetContext().classTable->classes.end())
               {
                    isDeleted = true;
                    break;
               }

               const auto fieldQualifiers =
                    QualifiersFrom(qualifiedSource != nullptr && qualifiedSource->IsConst() && !field.isMutable,
                                   qualifiedSource != nullptr && qualifiedSource->IsVolatile());
               const auto* sourceType = AddQualifiers(memberType, fieldQualifiers);
               Expression source = constructor.isMoveConstructor
                                        ? Expression{std::make_unique<XValue>(sourceType, nullptr, false)}
                                        : Expression{std::make_unique<LValue>(sourceType, nullptr, false)};
               const ClassConstructor* selected = nullptr;
               std::size_t bestCost = std::numeric_limits<std::size_t>::max();
               bool ambiguous = false;
               for (const auto& candidate : memberRecord->second.constructors)
               {
                    if (!candidate.isCopyConstructor && !candidate.isMoveConstructor) continue;
                    if (IsIgnoredDeletedMoveConstructor(candidate) || candidate.parameters.size() != 1) continue;
                    const auto cost = ConstructorArgumentCost(candidate.parameters.front().type, source);
                    if (!cost.has_value()) continue;
                    if (*cost < bestCost)
                    {
                         selected = &candidate;
                         bestCost = *cost;
                         ambiguous = false;
                    }
                    else if (*cost == bestCost)
                         ambiguous = true;
               }
               if (memberRecord->second.destructor != nullptr &&
                   (memberRecord->second.destructorBodyKind == ast::FunctionBodyKind::Deleted ||
                    memberRecord->second.destructor->isDeleted ||
                    !this->CanAccess(memberClass->Info().id, memberRecord->second.destructorAccess)))
               {
                    isDeleted = true;
                    break;
               }
               if (selected == nullptr || ambiguous || selected->bodyKind == ast::FunctionBodyKind::Deleted ||
                   !this->CanAccess(memberClass->Info().id, selected->access))
               {
                    isDeleted = true;
                    break;
               }
          }
          if (isDeleted)
          {
               constructor.bodyKind = ast::FunctionBodyKind::Deleted;
               if (constructor.function != nullptr) constructor.function->isDeleted = true;
          }
     }
}

ecpps::typeSystem::NonowningTypePointer ecpps::ir::IR::AddQualifiers(const typeSystem::NonowningTypePointer type,
                                                                     const typeSystem::Qualifiers qualifiers) const
{
     if (type == nullptr || qualifiers == typeSystem::Qualifiers::None) return type;
     const auto* existingQualifiers = type->CastTo<typeSystem::QualifiedType>();
     const auto combined = static_cast<typeSystem::Qualifiers>(
          std::to_underlying(qualifiers) |
          (existingQualifiers == nullptr ? std::uint_fast8_t{} : std::to_underlying(existingQualifiers->qualifiers())));
     TypeRequest request{};
     request.qualifiers = combined;

     if (const auto* classType = type->CastTo<typeSystem::ClassType>())
     {
          request.kind = TypeKind::Compound;
          request.data = ClassRequest{.id = classType->Info().id};
     }
     else if (const auto* pointerType = type->CastTo<typeSystem::PointerType>())
     {
          request.kind = TypeKind::Compound;
          request.data = PointerRequest{.elementType = pointerType->BaseType()};
     }
     else if (const auto* arrayType = type->CastTo<typeSystem::ArrayType>())
     {
          request.kind = TypeKind::Compound;
          request.qualifiers = typeSystem::Qualifiers::None;
          request.data = BoundedArrayRequest{.elementType = this->AddQualifiers(arrayType->ElementType(), qualifiers),
                                             .size = arrayType->ElementCount()};
     }
     else if (type->CastTo<typeSystem::VoidType>() != nullptr)
     {
          request.kind = TypeKind::Fundamental;
          request.data = VoidRequest{};
     }
     else if (type->CastTo<typeSystem::BooleanType>() != nullptr)
     {
          request.kind = TypeKind::Fundamental;
          request.data = BooleanRequest{};
     }
     else if (const auto* integralType = type->CastTo<typeSystem::IntegralType>())
     {
          request.kind = TypeKind::Fundamental;
          request.data = StandardSignedIntegerRequest{.size = integralType->Kind(),
                                                      .signedness = integralType->Sign(),
                                                      .isCharWithoutSign = type->RawName() == "char"};
     }
     else
          return type;

     return GetTypeContext().Get(request);
}

Expression ecpps::ir::IR::BuildMemberAccess(Expression objectPointer, const typeSystem::ClassInfo& info,
                                            const typeSystem::ClassField& field, const Location& source)
{
     if (objectPointer == nullptr) return nullptr;
     if (!this->CanAccess(info.id, field.access))
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      std::format("Member '{}' is not accessible", field.name), source));
          return nullptr;
     }

     TypeRequest sizeRequest{};
     sizeRequest.kind = TypeKind::Fundamental;
     sizeRequest.data = PlatformIntegerRequest{.kind = PlatformIntegerKind::Size};
     const auto* sizeType = GetTypeContext().Get(sizeRequest);
     const auto* fieldType = field.type;
     const auto* objectPointerType = objectPointer->Type()->CastTo<typeSystem::PointerType>();
     const auto* objectClassType =
          objectPointerType != nullptr ? objectPointerType->BaseType()->CastTo<typeSystem::ClassType>() : nullptr;
     if (objectClassType != nullptr && !typeSystem::IsReference(field.type))
          fieldType = this->AddQualifiers(field.type, QualifiersFrom(objectClassType->IsConst() && !field.isMutable,
                                                                     objectClassType->IsVolatile()));
     auto offset =
          std::make_unique<PRValue>(sizeType,
                                    std::unique_ptr<IntegralNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                                                  IntegralNode(field.offset, source)},
                                    true);
     auto address = std::make_unique<PRValue>(
          PointerTo(fieldType),
          std::unique_ptr<high::AdditionNode, IRDeleter>{new (*this->GetContext().nodeAllocator) high::AdditionNode(
               std::move(objectPointer), std::move(offset), source)},
          false);

     if (const auto* referenceType = field.type->CastTo<typeSystem::ReferenceType>())
     {
          auto storedReference = std::make_unique<LValue>(field.type,
                                                          std::unique_ptr<high::DereferenceNode, IRDeleter>{
                                                               new (*this->GetContext().nodeAllocator)
                                                                    high::DereferenceNode(std::move(address), source)},
                                                          false);
          return std::make_unique<LValue>(referenceType->BaseType(),
                                          std::unique_ptr<high::DereferenceNode, IRDeleter>{
                                               new (*this->GetContext().nodeAllocator)
                                                    high::DereferenceNode(std::move(storedReference), source)},
                                          false);
     }

     return std::make_unique<LValue>(
          fieldType,
          std::unique_ptr<high::DereferenceNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                                 high::DereferenceNode(std::move(address), source)},
          false);
}

void ecpps::ir::IR::StoreAtAddress(const SingleAssignRegisterNode* address, const SingleAssignRegisterNode* value,
                                   typeSystem::NonowningTypePointer type, const Location& source)
{
     if (address == nullptr || value == nullptr || type == nullptr) return;
     auto& allocator = *this->GetContext().nodeAllocator;

     const auto* accessType = type;
     if (const auto* reference = type->CastTo<typeSystem::ReferenceType>())
          accessType = this->PointerTo(reference->BaseType());

     this->_built.push_back(std::unique_ptr<SSAIndirectStoreNode, IRDeleter>{
          new (allocator) SSAIndirectStoreNode(address, value, detail::AccessFor(accessType), source)});
}

void ecpps::ir::IR::InitialiseReferenceAtAddress(typeSystem::NonowningTypePointer type,
                                                 const SingleAssignRegisterNode* address,
                                                 const ast::NodePointer* initialiser,
                                                 ast::InitialisationType initialisationType, const Location& source,
                                                 const bool aggregateElement)
{
     const auto* referenceType = type->CastTo<typeSystem::ReferenceType>();
     runtime_assert(referenceType != nullptr, "Reference initialisation requires a reference type");
     if (referenceType == nullptr || initialiser == nullptr || *initialiser == nullptr) return;

     const ast::NodePointer* binding = initialiser;
     if (const auto* list = dynamic_cast<const ast::InitialiserListNode*>(initialiser->get()))
     {
          if (list->Initialisers().size() != 1)
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A reference member initialiser must contain exactly one initialiser", source));
               return;
          }
          binding = &list->Initialisers().front();
     }

     auto referent = ParseExpression(*binding);
     if (referent == nullptr) return;

     if ((aggregateElement || initialisationType == ast::InitialisationType::BracedInitialiser ||
          dynamic_cast<const ast::InitialiserListNode*>(initialiser->get()) != nullptr) &&
         IsNarrowingConversion(referent, referenceType->BaseType()))
     {
          DiagnosticSink::Report(
               this->GetContext(),
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Narrowing conversion from '{}' to '{}' is not allowed in list initialisation",
                                referent->Type()->Name(), referenceType->BaseType()->Name()),
                    (*binding)->Source()));
          return;
     }

     const auto* referentAddress = BindReferenceExpression(type, std::move(referent), source, address != nullptr);
     if (address != nullptr && referentAddress != nullptr) this->StoreAtAddress(address, referentAddress, type, source);
}

const ecpps::ir::SingleAssignRegisterNode* ecpps::ir::IR::BindReferenceExpression(typeSystem::NonowningTypePointer type,
                                                                                  Expression referent,
                                                                                  const Location& source,
                                                                                  const bool materialise)
{
     const auto* referenceType = type != nullptr ? type->CastTo<typeSystem::ReferenceType>() : nullptr;
     runtime_assert(referenceType != nullptr, "Reference binding requires a reference type");
     if (referenceType == nullptr || referent == nullptr) return nullptr;

     const auto* targetQualified = referenceType->BaseType()->CastTo<typeSystem::QualifiedType>();
     const auto* sourceQualified = referent->Type()->CastTo<typeSystem::QualifiedType>();

     const bool sameUnqualified = IsSameUnqualifiedType(referenceType->BaseType(), referent->Type());
     const bool dropsConst = sameUnqualified && sourceQualified != nullptr && sourceQualified->IsConst() &&
                             (targetQualified == nullptr || !targetQualified->IsConst());
     const bool dropsVolatile = sameUnqualified && sourceQualified != nullptr && sourceQualified->IsVolatile() &&
                                (targetQualified == nullptr || !targetQualified->IsVolatile());
     const bool lvalueReference = referenceType->GetKind() == typeSystem::ReferenceType::Kind::LValue;
     const auto classConversionCost = !sameUnqualified && typeSystem::IsClass(referenceType->BaseType())
                                           ? ConstructorArgumentCost(referenceType->BaseType(), referent)
                                           : std::optional<std::size_t>{};
     const bool canConvertTemporary =
          !sameUnqualified &&
          (classConversionCost.has_value() || referenceType->BaseType()->CompareTo(referent->Type()).IsValid());
     const bool directCategory =
          lvalueReference ? referent->IsLValue() ||
                                 (targetQualified != nullptr && targetQualified->IsConst() && referent->IsXValue())
                          : referent->IsXValue();
     const bool directBinding = directCategory && sameUnqualified;
     const bool canBindTemporary = (!lvalueReference || (targetQualified != nullptr && targetQualified->IsConst())) &&
                                   (referent->IsRValue() || canConvertTemporary);

     if (dropsConst || dropsVolatile || (!directBinding && !canBindTemporary))
     {
          DiagnosticSink::Report(
               this->GetContext(),
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Reference member of type '{}' cannot bind to an initialiser of type '{}'",
                                referenceType->BaseType()->Name(), referent->Type()->Name()),
                    source));
          return nullptr;
     }

     if (!materialise) return nullptr; // Semantic-only validation of a default member initialiser.

     if (directBinding)
     {
          auto referenceAddress = ParseAddressOfExpression(std::move(referent), source);
          if (referenceAddress == nullptr) return nullptr;
          return LowerExpressionLoaded(std::move(referenceAddress), this->_built);
     }

     if (sameUnqualified && typeSystem::IsClass(referenceType->BaseType()) && referent->IsPRValue())
     {
          const auto* temporaryAddress = LowerExpressionLoaded(std::move(referent), this->_built);
          return temporaryAddress;
     }

     auto* function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(function != nullptr, "Temporary reference binding must occur inside a function");
     if (function == nullptr) return nullptr;

     auto& allocator = *this->GetContext().nodeAllocator;
     RegisterPriorityInfo info{.regClass = RegisterClass::LocalVariable};
     auto allocation = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{new (allocator) SingleAssignRegisterNode(
          function->GetNextRegisterIndex(), info, referenceType->BaseType()->Width(),
          referenceType->BaseType()->Alignment(), source)};
     const auto* allocationRegister = allocation.get();
     this->_built.push_back(
          std::unique_ptr<AllocationNode, IRDeleter>{new (allocator) AllocationNode(std::move(allocation), source)});
     const auto* temporaryAddress = AddressOfAllocation(allocationRegister, referenceType->BaseType(), source);
     if (typeSystem::IsClass(referenceType->BaseType()))
     {
          const auto* targetClass = referenceType->BaseType()->CastTo<typeSystem::ClassType>();
          const auto* sourceClass = referent->Type()->CastTo<typeSystem::ClassType>();
          if (targetClass != nullptr && sourceClass != nullptr && targetClass->Info().id == sourceClass->Info().id)
               InitialiseClassFromExpression(referenceType->BaseType(), temporaryAddress, std::move(referent), source);
          else if (targetClass != nullptr)
          {
               std::vector<Expression> constructorArguments{};
               constructorArguments.push_back(std::move(referent));
               auto selection = SelectConstructor(targetClass->Info(), std::move(constructorArguments), source, false);
               if (!selection.has_value()) return nullptr;
               const bool accessible = this->CanAccess(targetClass->Info().id, selection->constructor->access);
               if (!accessible || selection->constructor->bodyKind == ast::FunctionBodyKind::Deleted)
               {
                    const auto diagnosticCandidate =
                         ConstructorCandidateDiagnostic(*selection->constructor, accessible);
                    DiagnosticSink::Report(this->GetContext(),
                                           ecpps::ir::overloadDiagnostics::SelectedConstructorFailure(
                                                selection->constructor->bodyKind == ast::FunctionBodyKind::Deleted
                                                     ? "The selected constructor is deleted"
                                                     : "Selected constructor is not accessible",
                                                source, diagnosticCandidate));
                    return nullptr;
               }
               ExecuteConstructor(referenceType->BaseType(), temporaryAddress, targetClass->Info(),
                                  std::move(*selection), ObjectInitialisationMode::Explicit, nullptr, source);
          }
          RegisterTemporaryForDestruction(referenceType->BaseType(), temporaryAddress, source);
          return temporaryAddress;
     }

     auto converted = ConvertTo(std::move(referent), referenceType->BaseType());
     if (converted == nullptr) return nullptr;
     const auto* temporaryValue = LowerExpressionLoaded(std::move(converted), this->_built);
     if (temporaryValue == nullptr) return nullptr;
     StoreAtAddress(temporaryAddress, temporaryValue, referenceType->BaseType(), source);
     return temporaryAddress;
}

void ecpps::ir::IR::InitialiseAtAddress(typeSystem::NonowningTypePointer type, const SingleAssignRegisterNode* address,
                                        const ast::NodePointer* initialiser, ast::InitialisationType initialisationType,
                                        ObjectInitialisationMode mode, const Location& source,
                                        const bool aggregateElement)
{
     runtime_assert(type != nullptr && address != nullptr, "Invalid initialisation target");
     if (type == nullptr || address == nullptr) return;
     auto* function = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     const auto temporaryDepth = function == nullptr ? 0 : function->temporaryObjects.size();
     struct TemporaryDestructionGuard
     {
          IR* ir{};
          FunctionContext* function{};
          std::size_t depth{};
          ~TemporaryDestructionGuard(void)
          {
               if (this->function == nullptr) return;
               this->ir->EmitTemporaryDestructorsFrom(this->depth);
               this->function->temporaryObjects.resize(this->depth);
          }
     } temporaryDestructionGuard{.ir = this, .function = function, .depth = temporaryDepth};

     if (type->CastTo<typeSystem::ReferenceType>() != nullptr)
     {
          if (mode != ObjectInitialisationMode::Explicit || initialiser == nullptr)
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A reference member without a default member initialiser must be explicitly initialised",
                         source));
               return;
          }
          this->InitialiseReferenceAtAddress(type, address, initialiser, initialisationType, source, aggregateElement);
          return;
     }

     if (type->CastTo<typeSystem::ArrayType>() != nullptr)
     {
          this->InitialiseArrayAtAddress(type, address, initialiser, mode, source);
          return;
     }

     if (typeSystem::IsClass(type))
     {
          this->InitialiseClassAtAddress(type, address, initialiser, initialisationType, mode, source);
          return;
     }

     if (mode == ObjectInitialisationMode::Default)
     {
          const auto* qualifiedType = type->CastTo<typeSystem::QualifiedType>();
          if (qualifiedType != nullptr && qualifiedType->IsConst())
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A const-qualified scalar member requires an initialiser during default-initialisation",
                         source));
          return;
     }

     Expression expression{};
     if (mode == ObjectInitialisationMode::Value || mode == ObjectInitialisationMode::EmptyList)
          expression = ParseValueInitialisation(type, source);
     else if (initialiser != nullptr && *initialiser != nullptr)
     {
          if (initialisationType == ast::InitialisationType::Default)
               initialisationType = dynamic_cast<const ast::InitialiserListNode*>(initialiser->get()) != nullptr
                                         ? ast::InitialisationType::BracedInitialiser
                                         : ast::InitialisationType::EqualInitialiser;
          if (aggregateElement && initialisationType != ast::InitialisationType::BracedInitialiser)
          {
               auto value = ParseExpression(*initialiser);
               if (value == nullptr) return;
               if (IsNarrowingConversion(value, type))
               {
                    DiagnosticSink::Report(this->GetContext(),
                                           diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                "Narrowing conversion from '" + value->Type()->Name() + "' to '" +
                                                     type->Name() + "' is not allowed in aggregate initialisation",
                                                (*initialiser)->Source()));
                    return;
               }
               expression = ConvertTo(std::move(value), type);
          }
          else
               expression = PickInitialiser(*initialiser, type, initialisationType);
     }

     if (expression == nullptr)
     {
          if (mode == ObjectInitialisationMode::Explicit)
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Invalid initialiser for aggregate member", source));
          return;
     }

     const auto* value = LowerExpressionLoaded(std::move(expression), this->_built);
     this->StoreAtAddress(address, value, type, source);
}

void ecpps::ir::IR::InitialiseArrayAtAddress(typeSystem::NonowningTypePointer type,
                                             const SingleAssignRegisterNode* address,
                                             const ast::NodePointer* initialiser, ObjectInitialisationMode mode,
                                             const Location& source)
{
     const auto* arrayType = type->CastTo<typeSystem::ArrayType>();
     runtime_assert(arrayType != nullptr, "Array initialisation requires an array type");
     if (arrayType == nullptr) return;

     const ast::InitialiserListNode* list = nullptr;
     if (mode == ObjectInitialisationMode::Explicit)
     {
          list = initialiser != nullptr ? dynamic_cast<const ast::InitialiserListNode*>(initialiser->get()) : nullptr;
          if (list == nullptr)
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "An array member requires a braced initialiser", source));
               return;
          }
          if (std::ranges::any_of(list->Initialisers(), IsDesignatedInitialiserNode))
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Designated initialisers are only supported for direct class members", source));
               return;
          }
          if (list->Initialisers().size() > arrayType->ElementCount())
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Too many initialisers for array member", source));
               return;
          }
     }

     for (std::size_t index = 0; index < arrayType->ElementCount(); index++)
     {
          const auto* elementAddress = AddByteOffset(address, index * arrayType->ElementType()->Size(), source);
          if (mode == ObjectInitialisationMode::Explicit && index < list->Initialisers().size())
          {
               const auto& elementInitialiser = list->Initialisers()[index];
               const auto elementInitType =
                    dynamic_cast<const ast::InitialiserListNode*>(elementInitialiser.get()) != nullptr
                         ? ast::InitialisationType::BracedInitialiser
                         : ast::InitialisationType::Default;
               this->InitialiseAtAddress(arrayType->ElementType(), elementAddress, &elementInitialiser, elementInitType,
                                         ObjectInitialisationMode::Explicit, elementInitialiser->Source(), true);
          }
          else if (mode == ObjectInitialisationMode::Default)
               this->InitialiseAtAddress(arrayType->ElementType(), elementAddress, nullptr,
                                         ast::InitialisationType::Default, ObjectInitialisationMode::Default, source);
          else
               this->InitialiseAtAddress(arrayType->ElementType(), elementAddress, nullptr,
                                         ast::InitialisationType::BracedInitialiser,
                                         mode == ObjectInitialisationMode::Value ? ObjectInitialisationMode::Value
                                                                                 : ObjectInitialisationMode::EmptyList,
                                         source);
     }
}

void ecpps::ir::IR::ZeroInitialiseAtAddress(typeSystem::NonowningTypePointer type,
                                            const SingleAssignRegisterNode* address, const Location& source)
{
     if (type->CastTo<typeSystem::ReferenceType>() != nullptr) return;

     if (const auto* arrayType = type->CastTo<typeSystem::ArrayType>())
     {
          for (std::size_t index = 0; index < arrayType->ElementCount(); index++)
          {
               const auto* elementAddress = AddByteOffset(address, index * arrayType->ElementType()->Size(), source);
               ZeroInitialiseAtAddress(arrayType->ElementType(), elementAddress, source);
          }
          return;
     }

     if (const auto* classType = type->CastTo<typeSystem::ClassType>())
     {
          if (classType->Info().kind == typeSystem::ClassKind::Union)
          {
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "Union initialisation is not supported yet", source));
               return;
          }
          for (const auto& field : classType->Info().fields)
          {
               const auto* fieldAddress = AddByteOffset(address, field.offset, field.source);
               ZeroInitialiseAtAddress(field.type, fieldAddress, field.source);
          }
          return;
     }

     auto zero = ParseValueInitialisation(type, source);
     if (zero == nullptr) return;
     const auto* value = LowerExpressionLoaded(std::move(zero), this->_built);
     this->StoreAtAddress(address, value, type, source);
}

void ecpps::ir::IR::InitialiseDefaultMember(typeSystem::NonowningTypePointer classType,
                                            const typeSystem::ClassField& field, const std::size_t fieldIndex,
                                            const SingleAssignRegisterNode* objectAddress,
                                            const SingleAssignRegisterNode* memberAddress, const Location& source)
{
     const auto classId = classType->CastTo<typeSystem::ClassType>()->Info().id;
     const auto classRecord = this->GetContext().classTable->classes.find(classId);
     const DefaultMemberInitialiser* defaultInitialiser = nullptr;
     if (classRecord != this->GetContext().classTable->classes.end())
          for (const auto& candidate : classRecord->second.memberInitialisers)
               if (candidate.fieldIndex == fieldIndex)
               {
                    defaultInitialiser = &candidate;
                    break;
               }

     if (defaultInitialiser == nullptr || defaultInitialiser->invalid || defaultInitialiser->initialiser == nullptr)
     {
          DiagnosticSink::Report(
               this->GetContext(),
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Default member initialiser for '{}' is unavailable", field.name), source));
          return;
     }

     this->_objectInitialisationStack.push_back({.type = classType, .address = objectAddress});
     struct RestoreStack
     {
          std::vector<ActiveObjectInitialisation>* stack;
          ~RestoreStack()
          {
               stack->pop_back();
          }
     };
     RestoreStack restore{&this->_objectInitialisationStack};

     this->InitialiseAtAddress(field.type, memberAddress, defaultInitialiser->initialiser,
                               defaultInitialiser->initialisationType, ObjectInitialisationMode::Explicit,
                               defaultInitialiser->source);
}

void ecpps::ir::IR::InitialiseClassMembers(typeSystem::NonowningTypePointer type,
                                           const SingleAssignRegisterNode* address, const Location& source)
{
     const auto* classType = type->CastTo<typeSystem::ClassType>();
     runtime_assert(classType != nullptr, "Class member initialisation requires a class type");
     if (classType == nullptr) return;

     for (std::size_t index = 0; index < classType->Info().fields.size(); index++)
     {
          const auto& field = classType->Info().fields[index];
          const auto* fieldAddress = AddByteOffset(address, field.offset, field.source);
          if (field.hasDefaultInitialiser)
          {
               InitialiseDefaultMember(type, field, index, address, fieldAddress, field.source);
               continue;
          }
          if (field.type->CastTo<typeSystem::ReferenceType>() != nullptr)
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Reference member '{}' has no initialiser", field.name), field.source));
               continue;
          }
          this->InitialiseAtAddress(field.type, fieldAddress, nullptr, ast::InitialisationType::Default,
                                    ObjectInitialisationMode::Default, source);
     }
}

void ecpps::ir::IR::InitialiseClassAtAddress(typeSystem::NonowningTypePointer type,
                                             const SingleAssignRegisterNode* address,
                                             const ast::NodePointer* initialiser,
                                             ast::InitialisationType initialisationType, ObjectInitialisationMode mode,
                                             const Location& source)
{
     const auto* classType = type->CastTo<typeSystem::ClassType>();
     runtime_assert(classType != nullptr, "Class initialisation requires a class type");
     if (classType == nullptr) return;
     if (mode == ObjectInitialisationMode::Explicit && initialiser != nullptr && *initialiser != nullptr &&
         dynamic_cast<const ast::InitialiserListNode*>(initialiser->get()) == nullptr)
     {
          auto expression = ParseExpression(*initialiser);
          const auto* sourceClass =
               expression != nullptr ? expression->Type()->CastTo<typeSystem::ClassType>() : nullptr;
          if (sourceClass != nullptr && sourceClass->Info().id == classType->Info().id)
          {
               InitialiseClassFromExpression(type, address, std::move(expression), source,
                                             initialisationType != ast::InitialisationType::EqualInitialiser);
               return;
          }
     }
     if (classType->Info().kind == typeSystem::ClassKind::Union)
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Union initialisation is not supported yet", source));
          return;
     }

     const auto classIr = this->GetContext().classTable->classes.find(classType->Info().id);
     if (classType->Info().Has(ClassProperty::UserDeclaredConstructors) &&
         classIr != this->GetContext().classTable->classes.end() && !classIr->second.constructors.empty())
     {
          InitialiseClassWithConstructors(type, address, initialiser, initialisationType, mode, source, classType);
          return;
     }
     InitialiseClassWithoutConstructors(type, address, initialiser, mode, source, classType);
}

std::optional<std::vector<Expression>> ecpps::ir::IR::ParseConstructorArguments(const ast::NodePointer* initialiser,
                                                                                const ObjectInitialisationMode mode)
{
     std::vector<Expression> arguments{};
     const auto* list = initialiser != nullptr && *initialiser != nullptr
                             ? dynamic_cast<const ast::InitialiserListNode*>(initialiser->get())
                             : nullptr;
     if (mode != ObjectInitialisationMode::Explicit) return arguments;
     if (list != nullptr)
     {
          for (const auto& clause : list->Initialisers())
          {
               if (dynamic_cast<const ast::DesignatedInitialiserNode*>(clause.get()) != nullptr)
               {
                    DiagnosticSink::Report(
                         this->GetContext(),
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "Designated initialisers require an aggregate class", clause->Source()));
                    return std::nullopt;
               }
               auto argument = ParseExpression(clause);
               if (argument == nullptr) return std::nullopt;
               arguments.push_back(std::move(argument));
          }
     }
     else if (initialiser != nullptr && *initialiser != nullptr)
     {
          auto argument = ParseExpression(*initialiser);
          if (argument == nullptr) return std::nullopt;
          arguments.push_back(std::move(argument));
     }
     return arguments;
}

std::optional<std::size_t> ecpps::ir::IR::ConstructorArgumentCost(typeSystem::NonowningTypePointer parameterType,
                                                                  const Expression& argument) const
{
     if (parameterType == nullptr || argument == nullptr) return std::nullopt;

     if (const auto* reference = parameterType->CastTo<typeSystem::ReferenceType>())
     {
          const auto* target = reference->BaseType()->CastTo<typeSystem::QualifiedType>();
          const auto* sourceType = argument->Type()->CastTo<typeSystem::QualifiedType>();
          const bool same = IsSameUnqualifiedType(reference->BaseType(), argument->Type());
          if (same && sourceType != nullptr &&
              ((sourceType->IsConst() && (target == nullptr || !target->IsConst())) ||
               (sourceType->IsVolatile() && (target == nullptr || !target->IsVolatile()))))
               return std::nullopt;

          const bool lvalueReference = reference->GetKind() == typeSystem::ReferenceType::Kind::LValue;
          const bool directCategory = lvalueReference ? argument->IsLValue() : argument->IsRValue();
          const auto qualifierCost = static_cast<std::size_t>(target != nullptr && target->IsConst() &&
                                                              (sourceType == nullptr || !sourceType->IsConst())) +
                                     static_cast<std::size_t>(target != nullptr && target->IsVolatile() &&
                                                              (sourceType == nullptr || !sourceType->IsVolatile()));
          std::optional<std::size_t> conversionCost{};
          if (!same)
          {
               if (typeSystem::IsClass(reference->BaseType()))
                    conversionCost = ConstructorArgumentCost(reference->BaseType(), argument);
               else
               {
                    const auto conversion = reference->BaseType()->CompareTo(argument->Type());
                    if (conversion.IsValid())
                    {
                         conversionCost = 0;
                         for (const auto kind : conversion.Sequence()) *conversionCost += ConversionCost(kind);
                    }
               }
          }
          if (same && directCategory) return qualifierCost;
          if (lvalueReference && (target == nullptr || !target->IsConst()) && !argument->IsRValue())
               return std::nullopt;
          if (!lvalueReference && !argument->IsRValue() && !conversionCost.has_value()) return std::nullopt;
          if (lvalueReference && (target == nullptr || !target->IsConst())) return std::nullopt;
          if (!same && !conversionCost.has_value()) return std::nullopt;
          return conversionCost.value_or(0) + 2 + qualifierCost;
     }

     if (const auto* parameterClass = parameterType->CastTo<typeSystem::ClassType>())
     {
          const auto* argumentClass = argument->Type()->CastTo<typeSystem::ClassType>();
          if (argumentClass != nullptr && parameterClass->Info().id == argumentClass->Info().id) return 0;
          const auto classIr = this->GetContext().classTable->classes.find(parameterClass->Info().id);
          if (classIr == this->GetContext().classTable->classes.end()) return std::nullopt;
          std::optional<std::size_t> bestCost{};
          for (const auto& constructor : classIr->second.constructors)
          {
               if (constructor.isCopyConstructor || constructor.isMoveConstructor || constructor.isExplicit ||
                   constructor.parameters.size() != 1)
                    continue;
               if (const auto* constructorParameterClass =
                        constructor.parameters.front().type->CastTo<typeSystem::ClassType>();
                   constructorParameterClass != nullptr &&
                   constructorParameterClass->Info().id == parameterClass->Info().id)
                    continue;
               const auto conversionCost = ConstructorArgumentCost(constructor.parameters.front().type, argument);
               if (!conversionCost.has_value()) continue;
               const auto cost = *conversionCost + 4;
               if (!bestCost.has_value() || cost < *bestCost) bestCost = cost;
          }
          return bestCost;
     }
     if (parameterType->CastTo<typeSystem::ArrayType>() != nullptr) return std::nullopt;

     const auto conversion = parameterType->CompareTo(argument->Type());
     if (!conversion.IsValid()) return std::nullopt;
     std::size_t score = 0;
     for (const auto kind : conversion.Sequence()) score += ConversionCost(kind);
     return score;
}

std::optional<ecpps::ir::IR::ConstructorSelection> ecpps::ir::IR::SelectConstructor(const typeSystem::ClassInfo& info,
                                                                                    std::vector<Expression> arguments,
                                                                                    const Location& source,
                                                                                    const bool allowExplicit)
{
     const auto classIr = this->GetContext().classTable->classes.find(info.id);
     if (classIr == this->GetContext().classTable->classes.end() || classIr->second.constructors.empty())
     {
          DiagnosticSink::Report(this->GetContext(),
                                 ecpps::ir::overloadDiagnostics::NoMatchingConstructor(info.DisplayName(), source));
          return std::nullopt;
     }

     const ClassConstructor* selected = nullptr;
     std::vector<const ClassConstructor*> bestCandidates{};
     std::size_t bestScore = std::numeric_limits<std::size_t>::max();
     bool ambiguous = false;
     for (const auto& candidate : classIr->second.constructors)
     {
          if (IsIgnoredDeletedMoveConstructor(candidate)) continue;
          if (!allowExplicit && candidate.isExplicit) continue;
          if (candidate.parameters.size() != arguments.size()) continue;
          std::size_t score = 0;
          bool viable = true;
          for (std::size_t index = 0; index < arguments.size(); index++)
          {
               const auto conversionCost = ConstructorArgumentCost(candidate.parameters[index].type, arguments[index]);
               if (!conversionCost.has_value())
               {
                    viable = false;
                    break;
               }
               score += *conversionCost;
          }
          if (!viable) continue;
          if (score < bestScore)
          {
               selected = &candidate;
               bestScore = score;
               ambiguous = false;
               bestCandidates.assign(1, &candidate);
          }
          else if (score == bestScore)
          {
               ambiguous = true;
               bestCandidates.push_back(&candidate);
          }
     }

     if (selected == nullptr || ambiguous)
     {
          std::vector<ecpps::ir::overloadDiagnostics::Candidate> diagnosticCandidates{};
          if (ambiguous)
          {
               diagnosticCandidates.reserve(bestCandidates.size());
               for (const auto* candidate : bestCandidates)
                    diagnosticCandidates.push_back(
                         ConstructorCandidateDiagnostic(*candidate, this->CanAccess(info.id, candidate->access)));
          }
          else
          {
               diagnosticCandidates.reserve(classIr->second.constructors.size());
               for (const auto& candidate : classIr->second.constructors)
                    if (!candidate.isImplicitlyDeclared)
                         diagnosticCandidates.push_back(
                              ConstructorCandidateDiagnostic(candidate, this->CanAccess(info.id, candidate.access)));
          }
          auto diagnostic = ambiguous ? ecpps::ir::overloadDiagnostics::AmbiguousConstructor(info.DisplayName(), source,
                                                                                             diagnosticCandidates)
                                      : ecpps::ir::overloadDiagnostics::NoMatchingConstructor(
                                             info.DisplayName(), source, diagnosticCandidates);
          DiagnosticSink::Report(this->GetContext(), std::move(diagnostic));
          return std::nullopt;
     }
     ConstructorSelection selection;
     selection.constructor = selected;
     selection.arguments = std::move(arguments);
     return selection;
}

void ecpps::ir::IR::InitialiseClassWithConstructors(typeSystem::NonowningTypePointer type,
                                                    const SingleAssignRegisterNode* address,
                                                    const ast::NodePointer* initialiser,
                                                    ast::InitialisationType initialisationType,
                                                    ObjectInitialisationMode mode, const Location& source,
                                                    const typeSystem::ClassType* classType)
{
     const auto& info = classType->Info();
     auto arguments = ParseConstructorArguments(initialiser, mode);
     if (!arguments.has_value()) return;

     const auto* list = initialiser != nullptr && *initialiser != nullptr
                             ? dynamic_cast<const ast::InitialiserListNode*>(initialiser->get())
                             : nullptr;
     if (list == nullptr && arguments->size() == 1)
     {
          auto& argument = arguments->front();
          const auto* sourceClass = argument->Type()->CastTo<typeSystem::ClassType>();
          const auto* call = dynamic_cast<const FunctionCallNode*>(argument->Value().get());
          if (argument->IsPRValue() && sourceClass != nullptr && sourceClass->Info().id == info.id && call != nullptr &&
              call->Function()->returnsByPointer)
          {
               InitialiseClassFromExpression(type, address, std::move(argument), source, false);
               return;
          }
     }
     const bool listInitialisation = mode == ObjectInitialisationMode::Explicit && list != nullptr;
     const bool copyInitialisation = mode == ObjectInitialisationMode::EmptyList ||
                                     initialisationType == ast::InitialisationType::EqualInitialiser;
     auto selection = SelectConstructor(info, std::move(*arguments), source, !copyInitialisation);
     if (!selection.has_value()) return;

     if (listInitialisation)
          for (std::size_t index = 0; index < selection->arguments.size(); index++)
          {
               const auto* parameterType = selection->constructor->parameters[index].type;
               if (const auto* reference = parameterType->CastTo<typeSystem::ReferenceType>())
                    parameterType = reference->BaseType();
               if (!IsNarrowingConversion(selection->arguments[index], parameterType)) continue;
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Narrowing conversion from '{}' to '{}' is not allowed in list initialisation",
                                     selection->arguments[index]->Type()->Name(), parameterType->Name()),
                         selection->arguments[index]->Value()->Source()));
               return;
          }
     if (copyInitialisation && selection->constructor->isExplicit)
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      "Copy initialisation cannot select an explicit constructor", source));
          return;
     }
     if (!this->CanAccess(info.id, selection->constructor->access))
     {
          DiagnosticSink::Report(this->GetContext(),
                                 ecpps::ir::overloadDiagnostics::SelectedConstructorFailure(
                                      "Selected constructor is not accessible", source,
                                      ConstructorCandidateDiagnostic(*selection->constructor, false)));
          return;
     }
     const auto bodyKind = selection->constructor->bodyKind;
     if (bodyKind == ast::FunctionBodyKind::Deleted)
     {
          DiagnosticSink::Report(this->GetContext(),
                                 ecpps::ir::overloadDiagnostics::SelectedConstructorFailure(
                                      "The selected constructor is deleted", source,
                                      ConstructorCandidateDiagnostic(*selection->constructor, true)));
          return;
     }
     if (bodyKind == ast::FunctionBodyKind::Defaulted && !selection->arguments.empty())
     {
          DiagnosticSink::Report(this->GetContext(), diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                          "Only default constructors can be defaulted", source));
          return;
     }
     if (classType->IsConst() && bodyKind == ast::FunctionBodyKind::Defaulted && !IsConstDefaultConstructible(type))
     {
          DiagnosticSink::Report(
               this->GetContext(),
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Const class '{}' is not const-default-constructible", info.DisplayName()), source));
          return;
     }

     ExecuteConstructor(type, address, info, std::move(*selection), mode, initialiser, source);
}
void ecpps::ir::IR::ExecuteConstructor(typeSystem::NonowningTypePointer type, const SingleAssignRegisterNode* address,
                                       const typeSystem::ClassInfo& info, ConstructorSelection selection,
                                       const ObjectInitialisationMode mode, const ast::NodePointer* initialiser,
                                       const Location& source)
{
     const auto bodyKind = selection.constructor->bodyKind;
     const auto* list = initialiser != nullptr && *initialiser != nullptr
                             ? dynamic_cast<const ast::InitialiserListNode*>(initialiser->get())
                             : nullptr;
     const bool emptyListInitialisation =
          mode == ObjectInitialisationMode::Explicit && list != nullptr && list->Initialisers().empty();
     if ((mode == ObjectInitialisationMode::Value || mode == ObjectInitialisationMode::EmptyList ||
          emptyListInitialisation) &&
         bodyKind == ast::FunctionBodyKind::Defaulted)
          ZeroInitialiseAtAddress(type, address, source);

     if (selection.constructor->function == nullptr) throw TracedException("Constructor function metadata is missing");

     std::vector<Expression> evaluatedArguments{};
     evaluatedArguments.reserve(selection.arguments.size() + 1);
     const auto* classObjectType =
          GetTypeContext().Get(TypeRequest{.kind = TypeKind::Compound, .data = ClassRequest{.id = info.id}});
     evaluatedArguments.push_back(std::make_unique<PRValue>(
          PointerTo(classObjectType),
          std::unique_ptr<SSARegisterReferenceNode, IRDeleter>{new (*this->GetContext().nodeAllocator)
                                                                    SSARegisterReferenceNode(address, source)},
          false));
     for (std::size_t index = 0; index < selection.arguments.size(); index++)
     {
          auto converted = PrepareByValueArgument(std::move(selection.arguments[index]),
                                                  selection.constructor->parameters[index].type, source);
          if (converted == nullptr) return;
          evaluatedArguments.push_back(std::move(converted));
     }

     auto call = std::unique_ptr<FunctionCallNode, IRDeleter>{new (*this->GetContext().nodeAllocator) FunctionCallNode(
          selection.constructor->function, std::move(evaluatedArguments), source)};
     Expression callExpression = std::make_unique<PRValue>(typeSystem::g_void.get(), std::move(call), false);
     [[maybe_unused]] const auto* constructorCallResult = LowerExpression(std::move(callExpression), this->_built);
}

void ecpps::ir::IR::InitialiseClassWithoutConstructors(typeSystem::NonowningTypePointer type,
                                                       const SingleAssignRegisterNode* address,
                                                       const ast::NodePointer* initialiser,
                                                       ObjectInitialisationMode mode, const Location& source,
                                                       const typeSystem::ClassType* classType)
{
     const auto& info = classType->Info();

     if (mode == ObjectInitialisationMode::Default)
     {
          if (classType->IsConst() && !IsConstDefaultConstructible(type))
          {
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Const class '{}' is not const-default-constructible", info.DisplayName()),
                         source));
               return;
          }
          this->InitialiseClassMembers(type, address, source);
          return;
     }
     if (mode == ObjectInitialisationMode::Value)
     {
          if (!IsAggregateClass(type))
          {
               this->ZeroInitialiseAtAddress(type, address, source);
               this->InitialiseClassMembers(type, address, source);
               return;
          }
          mode = ObjectInitialisationMode::EmptyList;
     }
     if (mode == ObjectInitialisationMode::EmptyList)
     {
          InitialiseEmptyClassList(type, address, source, classType);
          return;
     }

     const auto* list =
          initialiser != nullptr ? dynamic_cast<const ast::InitialiserListNode*>(initialiser->get()) : nullptr;
     if (list == nullptr)
     {
          DiagnosticSink::Report(
               this->GetContext(),
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Class '{}' cannot be initialised from this expression yet", info.DisplayName()),
                    source));
          return;
     }
     if (!IsAggregateClass(type))
     {
          if (!info.Has(ClassProperty::UserDeclaredConstructors) && list->Initialisers().empty())
          {
               ZeroInitialiseAtAddress(type, address, source);
               InitialiseClassMembers(type, address, source);
               return;
          }
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      std::format("Class '{}' is not an aggregate", info.DisplayName()), source));
          return;
     }

     const bool hasDesignatedInitialiser = std::ranges::any_of(list->Initialisers(), IsDesignatedInitialiserNode);
     if (!hasDesignatedInitialiser)
     {
          InitialisePositionalAggregate(address, *list, source, classType);
          return;
     }
     InitialiseDesignatedAggregate(type, address, *list, source, classType);
}

void ecpps::ir::IR::InitialiseEmptyClassList(typeSystem::NonowningTypePointer type,
                                             const SingleAssignRegisterNode* address, const Location& source,
                                             const typeSystem::ClassType* classType)
{
     const auto& info = classType->Info();
     if (!IsAggregateClass(type))
     {
          if (!info.Has(ClassProperty::UserDeclaredConstructors))
          {
               ZeroInitialiseAtAddress(type, address, source);
               InitialiseClassMembers(type, address, source);
          }
          else
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("No default constructor of '{}' is available", info.DisplayName()), source));
          return;
     }

     for (std::size_t index = 0; index < info.fields.size(); index++)
     {
          const auto& field = info.fields[index];
          const auto* fieldAddress = AddByteOffset(address, field.offset, field.source);
          if (field.hasDefaultInitialiser)
               InitialiseDefaultMember(type, field, index, address, fieldAddress, field.source);
          else if (field.type->CastTo<typeSystem::ReferenceType>() != nullptr)
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Reference member '{}' has no initialiser", field.name), source));
          else
               InitialiseAtAddress(field.type, fieldAddress, nullptr, ast::InitialisationType::EqualInitialiser,
                                   ObjectInitialisationMode::EmptyList, source);
     }
}

void ecpps::ir::IR::InitialisePositionalAggregate(const SingleAssignRegisterNode* address,
                                                  const ast::InitialiserListNode& list, const Location& source,
                                                  const typeSystem::ClassType* classType)
{
     const auto& clauses = list.Initialisers();
     std::size_t nextClause = 0;
     InitialisePositionalAggregateFields(clauses, nextClause, classType, address, source);
     if (nextClause < clauses.size())
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      "Too many initialisers for aggregate class", clauses[nextClause]->Source()));
}

void ecpps::ir::IR::InitialisePositionalAggregateFields(const std::vector<ast::NodePointer>& clauses,
                                                        std::size_t& nextClause, const typeSystem::ClassType* aggregate,
                                                        const SingleAssignRegisterNode* aggregateAddress,
                                                        const Location& source)
{
     for (std::size_t fieldIndex = 0; fieldIndex < aggregate->Info().fields.size(); fieldIndex++)
     {
          const auto& field = aggregate->Info().fields[fieldIndex];
          const auto* fieldAddress = AddByteOffset(aggregateAddress, field.offset, field.source);
          InitialisePositionalAggregateSubobject(clauses, nextClause, field.type, fieldAddress, aggregate,
                                                 aggregateAddress, fieldIndex, source);
     }
}

void ecpps::ir::IR::InitialisePositionalAggregateSubobject(const std::vector<ast::NodePointer>& clauses,
                                                           std::size_t& nextClause,
                                                           typeSystem::NonowningTypePointer subobjectType,
                                                           const SingleAssignRegisterNode* subobjectAddress,
                                                           const typeSystem::ClassType* owningClass,
                                                           const SingleAssignRegisterNode* owningObjectAddress,
                                                           const std::size_t owningFieldIndex, const Location& source)
{
     const typeSystem::ClassField* owningField =
          owningClass != nullptr && owningFieldIndex < owningClass->Info().fields.size()
               ? &owningClass->Info().fields[owningFieldIndex]
               : nullptr;
     if (nextClause >= clauses.size())
     {
          if (owningField != nullptr && owningField->hasDefaultInitialiser)
               InitialiseDefaultMember(owningClass, *owningField, owningFieldIndex, owningObjectAddress,
                                       subobjectAddress, owningField->source);
          else if (subobjectType->CastTo<typeSystem::ReferenceType>() != nullptr)
               DiagnosticSink::Report(this->GetContext(),
                                      diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                           "A reference member is omitted from the aggregate initialiser",
                                           owningField != nullptr ? owningField->source : source));
          else
               InitialiseAtAddress(subobjectType, subobjectAddress, nullptr, ast::InitialisationType::EqualInitialiser,
                                   ObjectInitialisationMode::EmptyList,
                                   owningField != nullptr ? owningField->source : source);
          return;
     }

     const auto& clause = clauses[nextClause];
     if (dynamic_cast<const ast::InitialiserListNode*>(clause.get()) != nullptr)
     {
          nextClause += 1;
          InitialiseAtAddress(subobjectType, subobjectAddress, &clause, ast::InitialisationType::BracedInitialiser,
                              ObjectInitialisationMode::Explicit, clause->Source(), true);
          return;
     }

     if (const auto* arrayType = subobjectType->CastTo<typeSystem::ArrayType>())
     {
          for (std::size_t index = 0; index < arrayType->ElementCount(); index++)
          {
               const auto* elementAddress =
                    AddByteOffset(subobjectAddress, index * arrayType->ElementType()->Size(), clause->Source());
               InitialisePositionalAggregateSubobject(clauses, nextClause, arrayType->ElementType(), elementAddress,
                                                      nullptr, nullptr, 0, source);
          }
          return;
     }

     if (const auto* nestedClass = subobjectType->CastTo<typeSystem::ClassType>();
         nestedClass != nullptr && IsAggregateClass(subobjectType))
     {
          if (nestedClass->Info().fields.empty())
          {
               InitialiseAtAddress(subobjectType, subobjectAddress, nullptr, ast::InitialisationType::EqualInitialiser,
                                   ObjectInitialisationMode::EmptyList, clause->Source());
               return;
          }
          InitialisePositionalAggregateFields(clauses, nextClause, nestedClass, subobjectAddress, source);
          return;
     }

     nextClause += 1;
     InitialiseAtAddress(subobjectType, subobjectAddress, &clause, ast::InitialisationType::EqualInitialiser,
                         ObjectInitialisationMode::Explicit, clause->Source(), true);
}

void ecpps::ir::IR::InitialiseDesignatedAggregate(typeSystem::NonowningTypePointer type,
                                                  const SingleAssignRegisterNode* address,
                                                  const ast::InitialiserListNode& list, const Location& source,
                                                  const typeSystem::ClassType* classType)
{
     const auto& info = classType->Info();
     const auto& clauses = list.Initialisers();
     std::vector<const ast::NodePointer*> explicitInitialisers(info.fields.size(), nullptr);
     bool sawDesignator = false;
     bool sawPositional = false;
     std::size_t lastDesignatedIndex = 0;
     bool hasLastDesignatedIndex = false;
     for (std::size_t clauseIndex = 0; clauseIndex < clauses.size(); clauseIndex++)
     {
          if (const auto* designated = dynamic_cast<const ast::DesignatedInitialiserNode*>(clauses[clauseIndex].get()))
          {
               sawDesignator = true;
               std::optional<std::size_t> fieldIndex;
               for (std::size_t index = 0; index < info.fields.size(); index++)
                    if (info.fields[index].name == designated->Member())
                    {
                         fieldIndex = index;
                         break;
                    }
               if (!fieldIndex.has_value())
               {
                    DiagnosticSink::Report(this->GetContext(),
                                           diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                std::format("'{}' is not a direct data member of '{}'",
                                                            designated->Member(), info.DisplayName()),
                                                designated->Source()));
                    return;
               }
               if (explicitInitialisers[*fieldIndex] != nullptr)
               {
                    DiagnosticSink::Report(
                         this->GetContext(),
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              std::format("Member '{}' is initialised more than once", designated->Member()),
                              designated->Source()));
                    return;
               }
               if (hasLastDesignatedIndex && *fieldIndex <= lastDesignatedIndex)
               {
                    DiagnosticSink::Report(
                         this->GetContext(),
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "Designated initialisers must follow member declaration order", designated->Source()));
                    return;
               }
               explicitInitialisers[*fieldIndex] = &designated->Initialiser();
               lastDesignatedIndex = *fieldIndex;
               hasLastDesignatedIndex = true;
          }
          else
          {
               sawPositional = true;
               if (sawDesignator)
               {
                    DiagnosticSink::Report(this->GetContext(),
                                           diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                                "Positional and designated initialisers cannot be mixed",
                                                clauses[clauseIndex]->Source()));
                    return;
               }
               if (clauseIndex >= info.fields.size())
               {
                    DiagnosticSink::Report(
                         this->GetContext(),
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "Too many initialisers for aggregate class", clauses[clauseIndex]->Source()));
                    return;
               }
               explicitInitialisers[clauseIndex] = &clauses[clauseIndex];
          }
     }
     if (sawDesignator && sawPositional)
     {
          DiagnosticSink::Report(this->GetContext(),
                                 diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                                      "Positional and designated initialisers cannot be mixed", source));
          return;
     }

     for (std::size_t index = 0; index < info.fields.size(); index++)
     {
          const auto& field = info.fields[index];
          const auto* fieldAddress = AddByteOffset(address, field.offset, field.source);
          if (explicitInitialisers[index] != nullptr)
          {
               const auto* value = explicitInitialisers[index]->get();
               const auto memberInitType = dynamic_cast<const ast::InitialiserListNode*>(value) != nullptr
                                                ? ast::InitialisationType::BracedInitialiser
                                                : ast::InitialisationType::Default;
               this->InitialiseAtAddress(field.type, fieldAddress, explicitInitialisers[index], memberInitType,
                                         ObjectInitialisationMode::Explicit, value->Source(), true);
          }
          else if (field.hasDefaultInitialiser)
               this->InitialiseDefaultMember(type, field, index, address, fieldAddress, field.source);
          else if (field.type->CastTo<typeSystem::ReferenceType>() != nullptr)
               DiagnosticSink::Report(
                    this->GetContext(),
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Reference member '{}' is omitted from the aggregate initialiser", field.name),
                         source));
          else
               this->InitialiseAtAddress(field.type, fieldAddress, nullptr, ast::InitialisationType::EqualInitialiser,
                                         ObjectInitialisationMode::EmptyList, source);
     }
}

bool ecpps::ir::IR::IsNarrowingConversion([[maybe_unused]] const Expression& expression,
                                          typeSystem::NonowningTypePointer toType) const
{
     if (expression == nullptr || toType == nullptr) return false;

     const auto* fromIntegral = expression->Type()->CastTo<typeSystem::IntegralType>();
     const auto* toIntegral = toType->CastTo<typeSystem::IntegralType>();
     if (IsBoolean(toType))
     {
          if (IsBoolean(expression->Type())) return false;
          if (expression->Type()->CastTo<typeSystem::PointerType>() != nullptr) return true;
          if (fromIntegral == nullptr) return false;

          std::optional<std::uint64_t> value{};
          if (expression->IsConstantExpression())
          {
               const auto constant = expression->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
               if (constant.has_value() && std::holds_alternative<std::uint64_t>(constant->variant))
                    value = std::get<std::uint64_t>(constant->variant);
          }
          return !value.has_value() || *value > 1;
     }
     if (fromIntegral == nullptr || toIntegral == nullptr) return false;

     const auto sourceWidth = fromIntegral->Width();
     const auto targetWidth = toIntegral->Width();
     if (sourceWidth == 0 || targetWidth == 0) return true;

     const bool sourceSigned = fromIntegral->Sign() == typeSystem::Signedness::Signed;
     const bool targetSigned = toIntegral->Sign() == typeSystem::Signedness::Signed;
     std::optional<std::uint64_t> constantValue;
     if (expression->IsConstantExpression())
     {
          const auto constant = expression->Value()->TryConstantEvaluate(EvaluationContext{.currentDepth = 0});
          if (constant.has_value() && std::holds_alternative<std::uint64_t>(constant->variant))
               constantValue = std::get<std::uint64_t>(constant->variant);
     }

     if (constantValue.has_value())
     {
          const auto rawValue = *constantValue;
          std::uint64_t extendedValue = rawValue;
          if (sourceSigned && sourceWidth < 64)
          {
               const auto valueMask = (std::uint64_t{1} << sourceWidth) - 1;
               extendedValue &= valueMask;
               const auto signBit = std::uint64_t{1} << (sourceWidth - 1);
               if ((extendedValue & signBit) != 0) extendedValue |= ~valueMask;
          }

          const auto signedValue = std::bit_cast<std::int64_t>(extendedValue);
          std::uint64_t targetUnsignedMax = std::numeric_limits<std::uint64_t>::max();
          std::int64_t targetSignedMin = std::numeric_limits<std::int64_t>::min();
          std::int64_t targetSignedMax = std::numeric_limits<std::int64_t>::max();
          if (targetWidth < 64)
          {
               targetUnsignedMax = (std::uint64_t{1} << targetWidth) - 1;
               targetSignedMin = -(std::int64_t{1} << (targetWidth - 1));
               targetSignedMax = (std::int64_t{1} << (targetWidth - 1)) - 1;
          }

          if (targetSigned)
          {
               if (sourceSigned) return signedValue < targetSignedMin || signedValue > targetSignedMax;
               return std::cmp_greater(rawValue, targetSignedMax);
          }
          if (sourceSigned) return signedValue < 0 || std::cmp_greater(signedValue, targetUnsignedMax);
          return rawValue > targetUnsignedMax;
     }

     if (sourceSigned) return !targetSigned || targetWidth < sourceWidth;
     return targetSigned ? targetWidth <= sourceWidth : targetWidth < sourceWidth;
}
