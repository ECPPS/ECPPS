#include <RuntimeAssert.h>
#include <algorithm>
#include <bit>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "../TypeSystem/ArithmeticTypes.h"
#include "../TypeSystem/CompoundTypes.h"
#include "Constexpr.h"
#include "Context.h"
#include "Expressions.h"
#include "HighLevel.h"
#include "IR.h"
#include "NodeBase.h"
#include "Operations.h"
#include "Parsing/AST.h"
#include "Shared/BumpAllocator.h"
#include "Shared/Diagnostics.h"
#include "Shared/Error.h"
#include "TypeSystem/ClassInfo.h"

using ecpps::Expression;

namespace
{
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
} // namespace

Expression ecpps::ir::IR::ParseAggregateInitialisation(const ast::NodePointer& expression,
                                                       [[maybe_unused]] typeSystem::NonowningTypePointer desiredType)
{
     this->GetContext().diagnostics.get().diagnosticsList.push_back(
          diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
               "Aggregate class initialization must initialize an object directly", expression->Source()));
     return nullptr;
}

bool ecpps::ir::IR::IsAggregateClass(typeSystem::NonowningTypePointer type) const
{
     const auto* classType = type != nullptr ? type->CastTo<typeSystem::ClassType>() : nullptr;
     if (classType == nullptr || !classType->IsComplete() || classType->Info().kind == typeSystem::ClassKind::Union)
          return false;
     if (classType->Info().hasUserDeclaredConstructors) return false;
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

     if (classType->Info().hasUserDeclaredConstructors)
     {
          const auto classRecord = this->GetContext().classTable->classes.find(classType->Info().id);
          if (classRecord == this->GetContext().classTable->classes.end()) return false;
          bool hasUsableDefaultConstructor = false;
          for (const auto& constructor : classRecord->second.constructors)
          {
               if (!constructor.parameters.empty() || constructor.declaration == nullptr ||
                   constructor.declaration->functionBody == ast::FunctionBodyKind::Deleted)
                    continue;
               if (constructor.declaration->functionBody != ast::FunctionBodyKind::Defaulted) return true;
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
     runtime_assert(function != nullptr, "Object initialization must occur inside a function");
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
     runtime_assert(function != nullptr, "Member addressing must occur inside a function");
     if (function == nullptr || address == nullptr) return nullptr;

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
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
     if (address == nullptr || value == nullptr) return;
     auto& allocator = *this->GetContext().nodeAllocator;
     this->_built.push_back(std::unique_ptr<SSAIndirectStoreNode, IRDeleter>{
          new (allocator) SSAIndirectStoreNode(address, value, source)});
     (void)type;
}

void ecpps::ir::IR::InitialiseReferenceAtAddress(typeSystem::NonowningTypePointer type,
                                                 const SingleAssignRegisterNode* address,
                                                 const ast::NodePointer* initialiser,
                                                 ast::InitialisationType initialisationType, const Location& source,
                                                 const bool aggregateElement)
{
     const auto* referenceType = type->CastTo<typeSystem::ReferenceType>();
     runtime_assert(referenceType != nullptr, "Reference initialization requires a reference type");
     if (referenceType == nullptr || initialiser == nullptr || *initialiser == nullptr) return;

     const ast::NodePointer* binding = initialiser;
     if (const auto* list = dynamic_cast<const ast::InitialiserListNode*>(initialiser->get()))
     {
          if (list->Initialisers().size() != 1)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A reference member initializer must contain exactly one initializer", source));
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
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Narrowing conversion from '{}' to '{}' is not allowed in list initialization",
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
                                                                                  const bool materialize)
{
     const auto* referenceType = type != nullptr ? type->CastTo<typeSystem::ReferenceType>() : nullptr;
     runtime_assert(referenceType != nullptr, "Reference binding requires a reference type");
     if (referenceType == nullptr || referent == nullptr) return nullptr;

     const auto* targetQualified = referenceType->BaseType()->CastTo<typeSystem::QualifiedType>();
     const auto* sourceQualified = referent->Type()->CastTo<typeSystem::QualifiedType>();

     const auto sameUnqualifiedType = [](typeSystem::NonowningTypePointer left, typeSystem::NonowningTypePointer right)
     {
          const auto* leftIntegral = left->CastTo<typeSystem::IntegralType>();
          const auto* rightIntegral = right->CastTo<typeSystem::IntegralType>();
          if (leftIntegral != nullptr || rightIntegral != nullptr)
               return leftIntegral != nullptr && rightIntegral != nullptr &&
                      leftIntegral->Sign() == rightIntegral->Sign() && leftIntegral->Kind() == rightIntegral->Kind() &&
                      IsBoolean(left) == IsBoolean(right);
          const auto* leftClass = left->CastTo<typeSystem::ClassType>();
          const auto* rightClass = right->CastTo<typeSystem::ClassType>();
          if (leftClass != nullptr || rightClass != nullptr)
               return leftClass != nullptr && rightClass != nullptr && leftClass->Info().id == rightClass->Info().id;
          const auto* leftPointer = left->CastTo<typeSystem::PointerType>();
          const auto* rightPointer = right->CastTo<typeSystem::PointerType>();
          if (leftPointer != nullptr || rightPointer != nullptr)
               return leftPointer != nullptr && rightPointer != nullptr &&
                      leftPointer->BaseType() == rightPointer->BaseType();
          return left == right;
     };

     const bool sameUnqualified = sameUnqualifiedType(referenceType->BaseType(), referent->Type());
     const bool dropsConst = sameUnqualified && sourceQualified != nullptr && sourceQualified->IsConst() &&
                             (targetQualified == nullptr || !targetQualified->IsConst());
     const bool dropsVolatile = sameUnqualified && sourceQualified != nullptr && sourceQualified->IsVolatile() &&
                                (targetQualified == nullptr || !targetQualified->IsVolatile());
     const bool lvalueReference = referenceType->GetKind() == typeSystem::ReferenceType::Kind::LValue;
     const bool canConvertTemporary =
          !sameUnqualified && referenceType->BaseType()->CompareTo(referent->Type()).IsValid();
     const bool directCategory =
          lvalueReference ? referent->IsLValue() ||
                                 (targetQualified != nullptr && targetQualified->IsConst() && referent->IsXValue())
                          : referent->IsXValue();
     const bool directBinding = directCategory && sameUnqualified;
     const bool canBindTemporary = (!lvalueReference || (targetQualified != nullptr && targetQualified->IsConst())) &&
                                   (referent->IsRValue() || canConvertTemporary);

     if (dropsConst || dropsVolatile || (!directBinding && !canBindTemporary))
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Reference member of type '{}' cannot bind to an initializer of type '{}'",
                                referenceType->BaseType()->Name(), referent->Type()->Name()),
                    source));
          return nullptr;
     }

     if (!materialize) return nullptr; // Semantic-only validation of a default member initializer.

     if (directBinding)
     {
          auto referenceAddress = ParseAddressOfExpression(std::move(referent), source);
          if (referenceAddress == nullptr) return nullptr;
          return LowerExpressionLoaded(std::move(referenceAddress), this->_built);
     }

     auto converted = sameUnqualified ? std::move(referent) : ConvertTo(std::move(referent), referenceType->BaseType());
     if (converted == nullptr) return nullptr;
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
     runtime_assert(type != nullptr && address != nullptr, "Invalid initialization target");
     if (type == nullptr || address == nullptr) return;

     if (type->CastTo<typeSystem::ReferenceType>() != nullptr)
     {
          if (mode != ObjectInitialisationMode::Explicit || initialiser == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A reference member without a default member initializer must be explicitly initialized",
                         source));
               return;
          }
          this->InitialiseReferenceAtAddress(type, address, initialiser, initialisationType, source, aggregateElement);
          return;
     }

     if (type->CastTo<typeSystem::ArrayType>() != nullptr)
     {
          this->InitialiseArrayAtAddress(type, address, initialiser, initialisationType, mode, source);
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "A const-qualified scalar member requires an initializer during default-initialization",
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
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "Narrowing conversion from '" + value->Type()->Name() + "' to '" + type->Name() +
                                   "' is not allowed in aggregate initialization",
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Invalid initializer for aggregate member", source));
          return;
     }

     const auto* value = LowerExpressionLoaded(std::move(expression), this->_built);
     this->StoreAtAddress(address, value, type, source);
}

void ecpps::ir::IR::InitialiseArrayAtAddress(typeSystem::NonowningTypePointer type,
                                             const SingleAssignRegisterNode* address,
                                             const ast::NodePointer* initialiser,
                                             ast::InitialisationType initialisationType, ObjectInitialisationMode mode,
                                             const Location& source)
{
     const auto* arrayType = type->CastTo<typeSystem::ArrayType>();
     runtime_assert(arrayType != nullptr, "Array initialization requires an array type");
     if (arrayType == nullptr) return;

     const ast::InitialiserListNode* list = nullptr;
     if (mode == ObjectInitialisationMode::Explicit)
     {
          list = initialiser != nullptr ? dynamic_cast<const ast::InitialiserListNode*>(initialiser->get()) : nullptr;
          if (list == nullptr)
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "An array member requires a braced initializer", source));
               return;
          }
          if (std::ranges::any_of(list->Initialisers(),
                                  [](const ast::NodePointer& value)
                                  {
                                       return dynamic_cast<const ast::DesignatedInitialiserNode*>(value.get()) !=
                                              nullptr;
                                  }))
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Designated initialisers are only supported for direct class members", source));
               return;
          }
          if (list->Initialisers().size() > arrayType->ElementCount())
          {
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
               const auto& elementInitializer = list->Initialisers()[index];
               const auto elementInitType =
                    dynamic_cast<const ast::InitialiserListNode*>(elementInitializer.get()) != nullptr
                         ? ast::InitialisationType::BracedInitialiser
                         : ast::InitialisationType::Default;
               this->InitialiseAtAddress(arrayType->ElementType(), elementAddress, &elementInitializer, elementInitType,
                                         ObjectInitialisationMode::Explicit, elementInitializer->Source(), true);
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
     (void)initialisationType;
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         "Union initialization is not supported yet", source));
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
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Default member initializer for '{}' is unavailable", field.name), source));
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
     runtime_assert(classType != nullptr, "Class member initialization requires a class type");
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Reference member '{}' has no initializer", field.name), field.source));
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
     runtime_assert(classType != nullptr, "Class initialization requires a class type");
     if (classType == nullptr) return;
     if (classType->Info().kind == typeSystem::ClassKind::Union)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Union initialization is not supported yet", source));
          return;
     }

     const auto classIr = this->GetContext().classTable->classes.find(classType->Info().id);
     if (classIr != this->GetContext().classTable->classes.end() && !classIr->second.constructors.empty())
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
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
     if (parameterType->CastTo<typeSystem::ClassType>() != nullptr ||
         parameterType->CastTo<typeSystem::ArrayType>() != nullptr)
          return std::nullopt;

     if (const auto* reference = parameterType->CastTo<typeSystem::ReferenceType>())
     {
          const auto* target = reference->BaseType()->CastTo<typeSystem::QualifiedType>();
          const auto* sourceType = argument->Type()->CastTo<typeSystem::QualifiedType>();
          const auto sameUnqualifiedType =
               [](typeSystem::NonowningTypePointer left, typeSystem::NonowningTypePointer right)
          {
               const auto* leftIntegral = left->CastTo<typeSystem::IntegralType>();
               const auto* rightIntegral = right->CastTo<typeSystem::IntegralType>();
               if (leftIntegral != nullptr || rightIntegral != nullptr)
                    return leftIntegral != nullptr && rightIntegral != nullptr &&
                           leftIntegral->Sign() == rightIntegral->Sign() &&
                           leftIntegral->Kind() == rightIntegral->Kind() && IsBoolean(left) == IsBoolean(right);
               const auto* leftClass = left->CastTo<typeSystem::ClassType>();
               const auto* rightClass = right->CastTo<typeSystem::ClassType>();
               if (leftClass != nullptr || rightClass != nullptr)
                    return leftClass != nullptr && rightClass != nullptr &&
                           leftClass->Info().id == rightClass->Info().id;
               const auto* leftPointer = left->CastTo<typeSystem::PointerType>();
               const auto* rightPointer = right->CastTo<typeSystem::PointerType>();
               if (leftPointer != nullptr || rightPointer != nullptr)
                    return leftPointer != nullptr && rightPointer != nullptr &&
                           leftPointer->BaseType() == rightPointer->BaseType();
               return left == right;
          };
          const bool same = sameUnqualifiedType(reference->BaseType(), argument->Type());
          if (same && sourceType != nullptr &&
              ((sourceType->IsConst() && (target == nullptr || !target->IsConst())) ||
               (sourceType->IsVolatile() && (target == nullptr || !target->IsVolatile()))))
               return std::nullopt;

          const bool lvalueReference = reference->GetKind() == typeSystem::ReferenceType::Kind::LValue;
          const bool directCategory =
               lvalueReference
                    ? argument->IsLValue() || (target != nullptr && target->IsConst() && argument->IsXValue())
                    : argument->IsXValue();
          const bool convertible = !same && reference->BaseType()->CompareTo(argument->Type()).IsValid();
          const bool bindsTemporary =
               (!lvalueReference || (target != nullptr && target->IsConst())) && (argument->IsRValue() || convertible);
          if (!(same && directCategory) && !bindsTemporary) return std::nullopt;
          if (bindsTemporary) return 8;
          if (target != nullptr && sourceType != nullptr && target->qualifiers() != sourceType->qualifiers()) return 1;
          return 0;
     }

     const auto conversion = parameterType->CompareTo(argument->Type());
     if (!conversion.IsValid()) return std::nullopt;
     std::size_t score = 0;
     for (const auto kind : conversion.Sequence()) score += ConversionCost(kind);
     return score;
}

std::optional<ecpps::ir::IR::ConstructorSelection> ecpps::ir::IR::SelectConstructor(const typeSystem::ClassInfo& info,
                                                                                    std::vector<Expression> arguments,
                                                                                    const Location& source)
{
     const auto classIr = this->GetContext().classTable->classes.find(info.id);
     if (classIr == this->GetContext().classTable->classes.end() || classIr->second.constructors.empty())
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("No constructor of '{}' matches these arguments", info.DisplayName()), source));
          return std::nullopt;
     }

     const ClassConstructor* selected = nullptr;
     std::size_t bestScore = std::numeric_limits<std::size_t>::max();
     bool ambiguous = false;
     for (const auto& candidate : classIr->second.constructors)
     {
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
          }
          else if (score == bestScore)
               ambiguous = true;
     }

     if (selected == nullptr || ambiguous)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    ambiguous ? std::format("Constructor call for '{}' is ambiguous", info.DisplayName())
                              : std::format("No constructor of '{}' matches these arguments", info.DisplayName()),
                    source));
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
     const bool listInitialisation = mode == ObjectInitialisationMode::Explicit && list != nullptr;
     const bool copyInitialisation = mode == ObjectInitialisationMode::EmptyList ||
                                     initialisationType == ast::InitialisationType::EqualInitialiser;
     auto selection = SelectConstructor(info, std::move(*arguments), source);
     if (!selection.has_value()) return;

     if (listInitialisation)
          for (std::size_t index = 0; index < selection->arguments.size(); index++)
          {
               const auto* parameterType = selection->constructor->parameters[index].type;
               if (const auto* reference = parameterType->CastTo<typeSystem::ReferenceType>())
                    parameterType = reference->BaseType();
               if (!IsNarrowingConversion(selection->arguments[index], parameterType)) continue;
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Narrowing conversion from '{}' to '{}' is not allowed in list initialization",
                                     selection->arguments[index]->Type()->Name(), parameterType->Name()),
                         selection->arguments[index]->Value()->Source()));
               return;
          }
     if (copyInitialisation && selection->constructor->isExplicit)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Copy initialization cannot select an explicit constructor", source));
          return;
     }
     if (!this->CanAccess(info.id, selection->constructor->access))
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build("Selected constructor is not accessible",
                                                                               source));
          return;
     }
     if (selection->constructor->declaration == nullptr)
          throw TracedException("Constructor declaration metadata is missing");
     const auto bodyKind = selection->constructor->declaration->functionBody;
     if (bodyKind == ast::FunctionBodyKind::Deleted || bodyKind == ast::FunctionBodyKind::None)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    bodyKind == ast::FunctionBodyKind::Deleted
                         ? "The selected constructor is deleted"
                         : "The selected constructor is declared but has no definition",
                    source));
          return;
     }
     if (bodyKind == ast::FunctionBodyKind::Defaulted && !selection->arguments.empty())
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Only default constructors can be defaulted", source));
          return;
     }
     if (classType->IsConst() && bodyKind == ast::FunctionBodyKind::Defaulted && !IsConstDefaultConstructible(type))
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
     struct RestoreObjectStack
     {
          std::vector<ActiveObjectInitialisation>* stack;
          ~RestoreObjectStack(void)
          {
               stack->pop_back();
          }
     };

     const auto bodyKind = selection.constructor->declaration->functionBody;
     const auto* list = initialiser != nullptr && *initialiser != nullptr
                             ? dynamic_cast<const ast::InitialiserListNode*>(initialiser->get())
                             : nullptr;
     const bool emptyListInitialisation =
          mode == ObjectInitialisationMode::Explicit && list != nullptr && list->Initialisers().empty();
     if ((mode == ObjectInitialisationMode::Value || mode == ObjectInitialisationMode::EmptyList ||
          emptyListInitialisation) &&
         bodyKind == ast::FunctionBodyKind::Defaulted)
          ZeroInitialiseAtAddress(type, address, source);

     if (bodyKind == ast::FunctionBodyKind::Block)
     {
          InlineConstructor(type, address, info, selection, source);
          return;
     }

     this->_objectInitialisationStack.push_back({.type = type, .address = address, .allowLocalLookup = true});

     RestoreObjectStack restoreObjectStack{};
     restoreObjectStack.stack = &this->_objectInitialisationStack;
     InitialiseClassMembers(type, address, source);
}

void ecpps::ir::IR::InlineConstructor(typeSystem::NonowningTypePointer type, const SingleAssignRegisterNode* address,
                                      const typeSystem::ClassInfo& info, ConstructorSelection& selection,
                                      const Location& source)
{
     struct RestoreInlineObjectStack
     {
          std::vector<ActiveObjectInitialisation>* stack;
          ~RestoreInlineObjectStack(void)
          {
               stack->pop_back();
          }
     };

     const auto* selected = selection.constructor;
     const auto containsReturn = [&](const auto& self, const ast::NodePointer& statement) -> bool
     {
          if (dynamic_cast<const ast::ReturnNode*>(statement.get()) != nullptr) return true;
          if (const auto* conditional = dynamic_cast<const ast::IfStatementNode*>(statement.get()))
               return std::ranges::any_of(conditional->Body(),
                                          [&](const ast::NodePointer& child)
                                          {
                                               return self(self, child);
                                          }) ||
                      std::ranges::any_of(conditional->ElseBody(),
                                          [&](const ast::NodePointer& child)
                                          {
                                               return self(self, child);
                                          });
          if (const auto* loop = dynamic_cast<const ast::WhileStatementNode*>(statement.get()))
               return std::ranges::any_of(loop->Body(),
                                          [&](const ast::NodePointer& child)
                                          {
                                               return self(self, child);
                                          });
          if (const auto* loop = dynamic_cast<const ast::ForStatementNode*>(statement.get()))
               return std::ranges::any_of(loop->Body(),
                                          [&](const ast::NodePointer& child)
                                          {
                                               return self(self, child);
                                          });
          if (const auto* loop = dynamic_cast<const ast::DoWhileStatementNode*>(statement.get()))
               return std::ranges::any_of(loop->Body(),
                                          [&](const ast::NodePointer& child)
                                          {
                                               return self(self, child);
                                          });
          return false;
     };
     if (std::ranges::any_of(selected->declaration->body,
                             [&](const ast::NodePointer& statement)
                             {
                                  return containsReturn(containsReturn, statement);
                             }))
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Return statements in constructors are not supported yet", source));
          return;
     }

     auto* callerFunction = dynamic_cast<FunctionContext*>(this->GetContext().contextSequence.back().get());
     runtime_assert(callerFunction != nullptr, "Constructor invocation requires a function context");
     if (callerFunction == nullptr) return;

     std::vector<const SingleAssignRegisterNode*> argumentRegisters{};
     argumentRegisters.reserve(selected->parameters.size());
     for (std::size_t index = 0; index < selected->parameters.size(); index++)
     {
          const auto* parameterType = selected->parameters[index].type;
          if (parameterType->CastTo<typeSystem::ReferenceType>() != nullptr)
               argumentRegisters.push_back(
                    BindReferenceExpression(parameterType, std::move(selection.arguments[index]), source));
          else
          {
               auto converted = ConvertTo(std::move(selection.arguments[index]), parameterType);
               if (converted == nullptr) return;
               argumentRegisters.push_back(LowerExpressionLoaded(std::move(converted), this->_built));
          }
          if (argumentRegisters.back() == nullptr) return;
     }

     auto inlineFunctionScope = std::make_unique<FunctionScope>();
     inlineFunctionScope->SetName(std::format("{}::__constructor", info.DisplayName()));
     inlineFunctionScope->returnType = typeSystem::g_void.get();
     inlineFunctionScope->parameters = selected->parameters;
     auto locals = std::make_shared<std::vector<FunctionScope::LocalEntity>>();
     inlineFunctionScope->SetLocals(locals);
     std::vector<typeSystem::NonowningTypePointer> parameterTypes{};
     parameterTypes.reserve(selected->parameters.size());
     for (const auto& parameter : selected->parameters) parameterTypes.push_back(parameter.type);
     auto inlineFunctionContext =
          std::make_shared<FunctionContext>(inlineFunctionScope.get(), abi::CallingConventionName{},
                                            typeSystem::g_void.get(), "__inline_constructor", parameterTypes);
     inlineFunctionContext->nextRegisterIndex = callerFunction->nextRegisterIndex;

     Context inlineContext = this->GetContext();
     inlineContext.contextSequence.push_back(inlineFunctionContext);
     IR inlineIr{&inlineContext};
     inlineIr._objectInitialisationStack = this->_objectInitialisationStack;
     auto& allocator = *this->GetContext().nodeAllocator;
     for (std::size_t index = 0; index < selected->parameters.size(); index++)
     {
          const auto& parameter = selected->parameters[index];
          const std::string parameterName = parameter.name.empty() ? std::format("__arg{}", index) : parameter.name;
          locals->push_back(
               FunctionScope::LocalEntity{Variable{parameterName, parameter.type, StorageDuration::Automatic}});
          RegisterPriorityInfo parameterInfo{.regClass = RegisterClass::ParameterVariable};
          auto allocation = std::unique_ptr<SingleAssignRegisterNode, IRDeleter>{
               new (allocator) SingleAssignRegisterNode(inlineFunctionContext->GetNextRegisterIndex(), parameterInfo,
                                                        parameter.type->Width(), parameter.type->Alignment(), source)};
          const auto* allocationRegister = allocation.get();
          inlineFunctionContext->RegisterAllocReg(parameterName, allocationRegister);
          inlineIr._built.push_back(std::unique_ptr<AllocationNode, IRDeleter>{
               new (allocator) AllocationNode(std::move(allocation), source)});
          if (parameter.type->CastTo<typeSystem::ReferenceType>() != nullptr)
          {
               const auto* parameterAddress = inlineIr.AddressOfAllocation(allocationRegister, parameter.type, source);
               inlineIr.StoreAtAddress(parameterAddress, argumentRegisters[index], parameter.type, source);
               continue;
          }
          inlineIr._built.push_back(std::unique_ptr<SSAStoreNode, IRDeleter>{
               new (allocator) SSAStoreNode(allocationRegister, argumentRegisters[index], source)});
     }

     inlineIr._objectInitialisationStack.push_back({.type = type, .address = address, .allowLocalLookup = true});

     RestoreInlineObjectStack restoreInlineObjectStack{};
     restoreInlineObjectStack.stack = &inlineIr._objectInitialisationStack;
     inlineIr.InitialiseClassMembers(type, address, source);
     for (const auto& statement : selected->declaration->body) inlineIr.ParseNode(statement);
     callerFunction->nextRegisterIndex = inlineFunctionContext->nextRegisterIndex;
     this->_built.insert(this->_built.end(), std::make_move_iterator(inlineIr._built.begin()),
                         std::make_move_iterator(inlineIr._built.end()));
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Class '{}' cannot be initialized from this expression yet", info.DisplayName()),
                    source));
          return;
     }
     if (!IsAggregateClass(type))
     {
          if (!info.hasUserDeclaredConstructors && list->Initialisers().empty())
          {
               ZeroInitialiseAtAddress(type, address, source);
               InitialiseClassMembers(type, address, source);
               return;
          }
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    std::format("Class '{}' is not an aggregate", info.DisplayName()), source));
          return;
     }

     const bool hasDesignatedInitialiser =
          std::ranges::any_of(list->Initialisers(),
                              [](const ast::NodePointer& clause)
                              {
                                   return dynamic_cast<const ast::DesignatedInitialiserNode*>(clause.get()) != nullptr;
                              });
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
          if (!info.hasUserDeclaredConstructors)
          {
               ZeroInitialiseAtAddress(type, address, source);
               InitialiseClassMembers(type, address, source);
          }
          else
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Reference member '{}' has no initializer", field.name), source));
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

     std::function<void(typeSystem::NonowningTypePointer, const SingleAssignRegisterNode*, const typeSystem::ClassType*,
                        const SingleAssignRegisterNode*, std::size_t)>
          initialiseSubobject;
     std::function<void(const typeSystem::ClassType*, const SingleAssignRegisterNode*)> initialiseAggregate;

     initialiseAggregate = [&](const typeSystem::ClassType* aggregate, const SingleAssignRegisterNode* aggregateAddress)
     {
          for (std::size_t fieldIndex = 0; fieldIndex < aggregate->Info().fields.size(); fieldIndex++)
          {
               const auto& field = aggregate->Info().fields[fieldIndex];
               const auto* fieldAddress = AddByteOffset(aggregateAddress, field.offset, field.source);
               initialiseSubobject(field.type, fieldAddress, aggregate, aggregateAddress, fieldIndex);
          }
     };

     initialiseSubobject = [&](typeSystem::NonowningTypePointer subobjectType,
                               const SingleAssignRegisterNode* subobjectAddress,
                               const typeSystem::ClassType* owningClass,
                               const SingleAssignRegisterNode* owningObjectAddress, const std::size_t owningFieldIndex)
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
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "A reference member is omitted from the aggregate initializer",
                              owningField != nullptr ? owningField->source : source));
               else
                    InitialiseAtAddress(subobjectType, subobjectAddress, nullptr,
                                        ast::InitialisationType::EqualInitialiser, ObjectInitialisationMode::EmptyList,
                                        owningField != nullptr ? owningField->source : source);
               return;
          }

          const auto& clause = clauses[nextClause];
          if (dynamic_cast<const ast::InitialiserListNode*>(clause.get()) != nullptr)
          {
               nextClause++;
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
                    initialiseSubobject(arrayType->ElementType(), elementAddress, nullptr, nullptr, 0);
               }
               return;
          }

          if (const auto* nestedClass = subobjectType->CastTo<typeSystem::ClassType>();
              nestedClass != nullptr && IsAggregateClass(subobjectType))
          {
               if (nestedClass->Info().fields.empty())
               {
                    InitialiseAtAddress(subobjectType, subobjectAddress, nullptr,
                                        ast::InitialisationType::EqualInitialiser, ObjectInitialisationMode::EmptyList,
                                        clause->Source());
                    return;
               }
               initialiseAggregate(nestedClass, subobjectAddress);
               return;
          }

          nextClause++;
          InitialiseAtAddress(subobjectType, subobjectAddress, &clause, ast::InitialisationType::EqualInitialiser,
                              ObjectInitialisationMode::Explicit, clause->Source(), true);
     };

     initialiseAggregate(classType, address);
     if (nextClause < clauses.size())
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
               diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                    "Too many initialisers for aggregate class", clauses[nextClause]->Source()));
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
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              std::format("'{}' is not a direct data member of '{}'", designated->Member(),
                                          info.DisplayName()),
                              designated->Source()));
                    return;
               }
               if (explicitInitialisers[*fieldIndex] != nullptr)
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              std::format("Member '{}' is initialized more than once", designated->Member()),
                              designated->Source()));
                    return;
               }
               if (hasLastDesignatedIndex && *fieldIndex <= lastDesignatedIndex)
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "Positional and designated initialisers cannot be mixed",
                              clauses[clauseIndex]->Source()));
                    return;
               }
               if (clauseIndex >= info.fields.size())
               {
                    this->GetContext().diagnostics.get().diagnosticsList.push_back(
                         diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                              "Too many initialisers for aggregate class", clauses[clauseIndex]->Source()));
                    return;
               }
               explicitInitialisers[clauseIndex] = &clauses[clauseIndex];
          }
     }
     if (sawDesignator && sawPositional)
     {
          this->GetContext().diagnostics.get().diagnosticsList.push_back(
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
               this->GetContext().diagnostics.get().diagnosticsList.push_back(
                    diagnostics::DiagnosticsBuilder<diagnostics::TypeError>{}.Build(
                         std::format("Reference member '{}' is omitted from the aggregate initializer", field.name),
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
