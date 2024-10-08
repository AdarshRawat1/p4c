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

#ifndef BACKENDS_EBPF_EBPFCONTROL_H_
#define BACKENDS_EBPF_EBPFCONTROL_H_

#include "ebpfObject.h"
#include "ebpfTable.h"
#include "ebpfType.h"

namespace P4::EBPF {

class EBPFControl;

class ControlBodyTranslator : public virtual CodeGenInspector {
 protected:
    const EBPFControl *control;
    std::set<const IR::Parameter *> toDereference;
    std::vector<cstring> saveAction;
    P4::P4CoreLibrary &p4lib;

 public:
    explicit ControlBodyTranslator(const EBPFControl *control);

    /// Handle the packet_out.emit method.
    virtual void compileEmitField(const IR::Expression *expr, cstring field, unsigned alignment,
                                  EBPFType *type);
    virtual void compileEmit(const IR::Vector<IR::Argument> *args);
    virtual void processMethod(const P4::ExternMethod *method);
    virtual void processApply(const P4::ApplyMethod *method);
    virtual void processFunction(const P4::ExternFunction *function);
    void processCustomExternFunction(const P4::ExternFunction *function,
                                     EBPFTypeFactory *typeFactory);

    bool preorder(const IR::PathExpression *expression) override;
    bool preorder(const IR::MethodCallExpression *expression) override;
    bool preorder(const IR::ExitStatement *) override;
    bool preorder(const IR::ReturnStatement *) override;
    bool preorder(const IR::IfStatement *statement) override;
    bool preorder(const IR::SwitchStatement *statement) override;
    bool preorder(const IR::StructExpression *expr) override;
};

class EBPFControl : public EBPFObject {
 public:
    const EBPFProgram *program;
    const IR::ControlBlock *controlBlock;
    const IR::Parameter *headers;
    const IR::Parameter *accept;         // only for ebpfFilter progs
    const IR::Parameter *xdpInputMeta;   // only for xdp progs
    const IR::Parameter *xdpOutputMeta;  // only for xdp progs
    const IR::Parameter *parserHeaders;
    /// Replace references to headers with references to parserHeaders
    cstring hitVariable;
    ControlBodyTranslator *codeGen;
    const bool emitExterns;

    std::set<const IR::Parameter *> toDereference;
    std::map<cstring, EBPFTable *> tables;
    std::map<cstring, EBPFCounterTable *> counters;

    EBPFControl(const EBPFProgram *program, const IR::ControlBlock *block,
                const IR::Parameter *parserHeaders);
    virtual void emit(CodeBuilder *builder);
    virtual void emitDeclaration(CodeBuilder *builder, const IR::Declaration *decl);
    virtual void emitTableTypes(CodeBuilder *builder);
    virtual void emitTableInitializers(CodeBuilder *builder);
    virtual void emitTableInstances(CodeBuilder *builder);
    virtual bool build();
    EBPFTable *getTable(cstring name) const {
        auto result = ::P4::get(tables, name);
        BUG_CHECK(result != nullptr, "No table named %1%", name);
        return result;
    }
    EBPFCounterTable *getCounter(cstring name) const {
        auto result = ::P4::get(counters, name);
        BUG_CHECK(result != nullptr, "No counter named %1%", name);
        return result;
    }

 protected:
    void scanConstants();

    DECLARE_TYPEINFO(EBPFControl, EBPFObject);
};

}  // namespace P4::EBPF

#endif /* BACKENDS_EBPF_EBPFCONTROL_H_ */
