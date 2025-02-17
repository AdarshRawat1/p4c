/*
Copyright 2013-present Barefoot Networks, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "validateParsedProgram.h"

namespace P4 {

/// Check that the type of a constant is either bit<>, int<> or int
void ValidateParsedProgram::postorder(const IR::Constant *c) {
    if (c->type != nullptr && !c->type->is<IR::Type_Unknown>() && !c->type->is<IR::Type_Bits>() &&
        !c->type->is<IR::Type_InfInt>())
        BUG("Invalid type %1% for constant %2%", c->type, c);
}

/// Check that underscore is not a method name
/// Check that constructors do not have a return type
/// Check that extern constructor names match the enclosing extern
void ValidateParsedProgram::postorder(const IR::Method *m) {
    if (m->name.isDontCare())
        ::P4::error(ErrorType::ERR_INVALID, "%1%: invalid method/function name.", m->name);
    if (auto ext = findContext<IR::Type_Extern>()) {
        if (m->name == ext->name && m->type->returnType != nullptr)
            ::P4::error(ErrorType::ERR_INVALID,
                        "%1%: invalid constructor; cannot have a return type.", m);
        if (m->type->returnType == nullptr) {
            if (m->name != ext->name) {
                ::P4::error(ErrorType::ERR_INVALID, "%1%: invalid type; method has no return type.",
                            m);
                return;
            }
            for (auto p : *m->type->parameters)
                if (p->direction != IR::Direction::None)
                    ::P4::error(ErrorType::ERR_INVALID,
                                "%1%: Constructor parameters cannot have a direction.", p);
        }
    }
}

/// Structured annotations cannot reuse names
void ValidateParsedProgram::postorder(const IR::Annotation *annotation) {
    auto [it, inserted] =
        annNames.emplace(std::pair(getContext()->node, annotation->name), annotation->structured);
    if (!inserted && (it->second || annotation->structured))
        ::P4::error(ErrorType::ERR_DUPLICATE, "%1%: duplicate name for structured annotation",
                    annotation);
}

/// Struct field names cannot be underscore
void ValidateParsedProgram::postorder(const IR::StructField *f) {
    if (f->name.isDontCare())
        ::P4::error(ErrorType::ERR_INVALID, "%1%: invalid field name", f->name);
}

/// Width of a bit<> or int<> type is greater than 0
void ValidateParsedProgram::postorder(const IR::Type_Bits *type) {
    if (type->expression)
        // cannot validate yet
        return;
    if (type->size < 0) ::P4::error(ErrorType::ERR_INVALID, "%1%: invalid type size", type);
    if (type->size == 0 && type->isSigned)
        ::P4::error(ErrorType::ERR_INVALID, "%1%: invalid type size", type);
}

void ValidateParsedProgram::postorder(const IR::Type_Varbits *type) {
    if (type->expression)
        // cannot validate yet
        return;
    if (type->size < 0) ::P4::error(ErrorType::ERR_INVALID, "%1%: invalid type size", type);
}

/// The accept and reject states cannot be implemented
void ValidateParsedProgram::postorder(const IR::ParserState *s) {
    if (s->name == IR::ParserState::accept || s->name == IR::ParserState::reject)
        ::P4::error(ErrorType::ERR_INVALID,
                    "Invalid parser state: %1% should not be implemented, it is built-in", s->name);
}

/// All parameters of a constructor must be directionless.
/// This only checks controls, parsers and packages
void ValidateParsedProgram::container(const IR::IContainer *type) {
    for (auto p : type->getConstructorParameters()->parameters)
        if (p->direction != IR::Direction::None)
            ::P4::error(ErrorType::ERR_INVALID,
                        "%1%: invalid direction. Constructor parameters cannot have a direction",
                        p);
}

/// Tables must have an 'actions' property.
void ValidateParsedProgram::postorder(const IR::P4Table *t) {
    auto ac = t->getActionList();
    if (ac == nullptr)
        ::P4::error(ErrorType::ERR_EXPECTED, "%1%: expected '%2%' property", t->name,
                    IR::TableProperties::actionsPropertyName);
}

/// Checks that the names of the three parameter lists for some constructs
/// are all distinct (type parameters, apply parameters, constructor parameters)
void ValidateParsedProgram::distinctParameters(const IR::TypeParameters *typeParams,
                                               const IR::ParameterList *apply,
                                               const IR::ParameterList *constr) {
    absl::flat_hash_map<cstring, const IR::Node *, Util::Hash> found;

    for (auto p : typeParams->parameters) found.emplace(p->getName(), p);
    for (auto p : apply->parameters) {
        auto [it, inserted] = found.emplace(p->getName(), p);
        if (!inserted) ::P4::error(ErrorType::ERR_DUPLICATE, "%1% duplicates %2%.", it->second, p);
    }
    for (auto p : constr->parameters) {
        auto it = found.find(p->getName());
        if (it != found.end())
            ::P4::error(ErrorType::ERR_DUPLICATE, "%1% duplicates %2%.", it->second, p);
    }
}

/// Cannot invoke constructors in actions
void ValidateParsedProgram::postorder(const IR::ConstructorCallExpression *expression) {
    if (isInContext<IR::P4Action>())
        ::P4::error(ErrorType::ERR_INVALID,
                    "%1%: invalid call. Constructor calls not allowed in actions.", expression);
}

/// Variable names cannot be underscore
void ValidateParsedProgram::postorder(const IR::Declaration_Variable *decl) {
    if (decl->name.isDontCare())
        ::P4::error(ErrorType::ERR_INVALID, "%1%: invalid variable name.", decl);
}

/// Instance names cannot be don't care
/// Do not declare instances in apply {} blocks, parser states or actions
void ValidateParsedProgram::postorder(const IR::Declaration_Instance *decl) {
    if (!decl->type->is<IR::Type_Name>() && !decl->type->is<IR::Type_Specialized>() &&
        !decl->type->is<IR::Type_Extern>())  // P4_14 only?
        ::P4::error(ErrorType::ERR_INVALID, "%1%: invalid type for declaration", decl->type);
    if (decl->name.isDontCare())
        ::P4::error(ErrorType::ERR_INVALID, "%1%: invalid instance name.", decl);
    if (isInContext<IR::BlockStatement>() &&         // we're looking for the apply block
        isInContext<IR::P4Control>() &&              // of a control
        !isInContext<IR::Declaration_Instance>()) {  // but not in an instance initializer
        ::P4::error(
            ErrorType::ERR_INVALID,
            "%1%: invalid declaration. Instantiations cannot be in a control 'apply' block.", decl);
    }
    if (isInContext<IR::ParserState>())
        ::P4::error(ErrorType::ERR_INVALID,
                    "%1%: invalid declaration. Instantiations cannot be in a parser state.", decl);
    if (isInContext<IR::Function>() || isInContext<IR::Method>())
        ::P4::error(ErrorType::ERR_INVALID,
                    "%1%: invalid declaration. Instantiations cannot be in a function or method.",
                    decl);
    if (isInContext<IR::P4Action>())
        ::P4::error(ErrorType::ERR_INVALID,
                    "%1%: declaration. Instantiations not allowed in actions.", decl);
}

/// Constant names cannot be underscore
void ValidateParsedProgram::postorder(const IR::Declaration_Constant *decl) {
    if (decl->name.isDontCare())
        ::P4::error(ErrorType::ERR_INVALID, "%1%: invalid constant name.", decl);
}

void ValidateParsedProgram::postorder(const IR::EntriesList *l) {
    if (!isInContext<IR::P4Table>()) {
        ::P4::error(ErrorType::ERR_INVALID,
                    "%1%: invalid entries list. Table entries must belong to a table.", l);
        return;
    }
}

/// Default label in switch statement is always the last one.
void ValidateParsedProgram::postorder(const IR::SwitchStatement *statement) {
    const IR::SwitchCase *defaultFound = nullptr;
    for (auto c : statement->cases) {
        if (defaultFound != nullptr) {
            if (c->label->is<IR::DefaultExpression>())
                ::P4::error(ErrorType::ERR_INVALID,
                            "%1%switch has multiple 'default' labels:%2%%3%", statement->srcInfo,
                            defaultFound->label->srcInfo, c->label->srcInfo);
            else
                ::P4::error(ErrorType::ERR_INVALID,
                            "%1%switch label %2% follows 'default' label, which is not allowed.%3%",
                            statement->srcInfo, c->label, defaultFound->label->srcInfo);
            break;
        }
        if (c->label->is<IR::DefaultExpression>()) defaultFound = c;
    }
}

/// Return statements are not allowed in parsers
void ValidateParsedProgram::postorder(const IR::ReturnStatement *statement) {
    if (!isInContext<IR::Function>() && isInContext<IR::P4Parser>())
        ::P4::error(ErrorType::ERR_INVALID,
                    "%1%: invalid statement. 'return' statements not allowed in parsers.",
                    statement);
}

/// Exit statements are not allowed in parsers or functions
void ValidateParsedProgram::postorder(const IR::ExitStatement *statement) {
    if (isInContext<IR::P4Parser>())
        ::P4::error(ErrorType::ERR_INVALID,
                    "%1%: invalid statement. 'exit' statements not allowed in parsers.", statement);
    if (isInContext<IR::Function>())
        ::P4::error(ErrorType::ERR_INVALID,
                    "%1% invalid statement. 'exit' statements not allowed in functions.",
                    statement);
}

void ValidateParsedProgram::postorder(const IR::P4Program *program) {
    IR::IndexedVector<IR::Node> declarations;
    for (auto decl : *program->getDeclarations()) {
        cstring name = decl->getName();
        auto existing = declarations.getDeclaration(name);
        if (existing != nullptr) {
            if (!existing->is<IR::IFunctional>() || !decl->is<IR::IFunctional>() ||
                existing->typeId() != decl->typeId() || decl->is<IR::P4Action>()) {
                ::P4::error(ErrorType::ERR_DUPLICATE, "%1% duplicates %2%.", decl->getName(),
                            existing->getName());
            }
        } else {
            declarations.push_back(decl->getNode());
        }
    }
}

void ValidateParsedProgram::postorder(const IR::Dots *dots) {
    auto context = getContext();
    CHECK_NULL(context);
    if (context->node->is<IR::ListExpression>()) {
        auto list = context->node->to<IR::ListExpression>();
        if (context->child_index != (int)list->size() - 1)
            ::P4::error(ErrorType::ERR_INVALID, "%1% must be the last element", dots);
    }
}

/// Check that continue and break statements are only used in the context of a for statement.
void ValidateParsedProgram::postorder(const IR::BreakStatement *s) {
    if (!isInContext<IR::ForStatement>() && !isInContext<IR::ForInStatement>())
        ::P4::error(ErrorType::ERR_INVALID,
                    "%1%: break statement must be used in the context of a for statement.", s);
}

void ValidateParsedProgram::postorder(const IR::ContinueStatement *s) {
    if (!isInContext<IR::ForStatement>() && !isInContext<IR::ForInStatement>())
        ::P4::error(ErrorType::ERR_INVALID,
                    "%1%: continue statement must be used in the context of a for statement.", s);
}

}  // namespace P4
