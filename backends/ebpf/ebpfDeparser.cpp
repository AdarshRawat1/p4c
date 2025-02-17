/*
Copyright 2022-present Orange
Copyright 2022-present Open Networking Foundation

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

#include "ebpfDeparser.h"

namespace P4::EBPF {

DeparserBodyTranslator::DeparserBodyTranslator(const EBPFDeparser *deparser)
    : CodeGenInspector(deparser->program->refMap, deparser->program->typeMap),
      ControlBodyTranslator(deparser),
      deparser(deparser) {
    setName("DeparserBodyTranslator");
}

bool DeparserBodyTranslator::preorder(const IR::MethodCallExpression *expression) {
    auto mi = P4::MethodInstance::resolve(expression, control->program->refMap,
                                          control->program->typeMap);
    auto ext = mi->to<P4::ExternMethod>();
    if (ext != nullptr) {
        // We skip headers emit processing which is handled by DeparserHdrEmitTranslator
        if (ext->method->name.name == p4lib.packetOut.emit.name) return false;
    }

    return ControlBodyTranslator::preorder(expression);
}

DeparserPrepareBufferTranslator::DeparserPrepareBufferTranslator(const EBPFDeparser *deparser)
    : CodeGenInspector(deparser->program->refMap, deparser->program->typeMap),
      ControlBodyTranslator(deparser),
      deparser(deparser) {
    setName("DeparserPrepareBufferTranslator");
}

bool DeparserPrepareBufferTranslator::preorder(const IR::BlockStatement *s) {
    for (auto a : s->components) {
        if (a->is<IR::MethodCallStatement>()) {
            visit(a);
        }
    }

    return false;
}

bool DeparserPrepareBufferTranslator::preorder(const IR::MethodCallExpression *expression) {
    auto mi = P4::MethodInstance::resolve(expression, control->program->refMap,
                                          control->program->typeMap);
    auto ext = mi->to<P4::ExternMethod>();
    if (ext != nullptr) {
        processMethod(ext);
        return false;
    }

    return false;
}

void DeparserPrepareBufferTranslator::processMethod(const P4::ExternMethod *method) {
    if (method->method->name.name == p4lib.packetOut.emit.name) {
        auto decl = method->object;
        if (decl == deparser->packet_out) {
            if (method->expr->arguments->size() != 1) {
                ::P4::error(ErrorType::ERR_MODEL,
                            "Not enough arguments to emit() method, exactly 1 required");
            }

            auto expr = method->expr->arguments->at(0)->expression;
            auto exprType = deparser->program->typeMap->getType(expr);
            auto headerToEmit = exprType->to<IR::Type_Header>();
            if (headerToEmit == nullptr) {
                ::P4::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                            "Cannot emit a non-header type %1%", expr);
                return;
            }

            unsigned width = headerToEmit->width_bits();
            builder->emitIndent();
            builder->append("if (");
            this->visit(expr);
            builder->append(".ebpf_valid) ");
            builder->blockStart();
            builder->emitIndent();
            builder->appendFormat("%s += %d;", this->deparser->outerHdrLengthVar.c_str(), width);
            builder->newline();
            builder->blockEnd(true);
        }
    }
}

DeparserHdrEmitTranslator::DeparserHdrEmitTranslator(const EBPFDeparser *deparser)
    : CodeGenInspector(deparser->program->refMap, deparser->program->typeMap),
      DeparserPrepareBufferTranslator(deparser),
      deparser(deparser) {
    setName("DeparserHdrEmitTranslator");
}

void DeparserHdrEmitTranslator::processMethod(const P4::ExternMethod *method) {
    // This method handles packet_out.emit() only and is intended to skip other externs
    if (method->method->name.name == p4lib.packetOut.emit.name) {
        auto decl = method->object;
        if (decl == deparser->packet_out) {
            auto expr = method->expr->arguments->at(0)->expression;
            auto exprType = deparser->program->typeMap->getType(expr);
            auto headerToEmit = exprType->to<IR::Type_Header>();
            if (headerToEmit == nullptr) {
                ::P4::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                            "Cannot emit a non-header type %1%", expr);
            }

            cstring msgStr;
            builder->emitIndent();
            builder->append("if (");
            this->visit(expr);
            builder->append(".ebpf_valid) ");
            builder->blockStart();
            auto program = deparser->program;
            // We expect all headers to start on a byte boundary.
            unsigned width = headerToEmit->width_bits();
            if ((width % 8) != 0) {
                ::P4::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                            "Header %1% size %2% is not a multiple of 8 bits.", expr, width);
                return;
            }
            msgStr = absl::StrFormat("Deparser: emitting header %s", expr->toString().c_str());
            builder->target->emitTraceMessage(builder, msgStr.c_str());

            builder->emitIndent();
            builder->appendFormat("if ((u8*)%s < %s + BYTES(%d)) ", program->packetEndVar.c_str(),
                                  program->headerStartVar.c_str(), width);
            builder->blockStart();
            builder->target->emitTraceMessage(builder,
                                              "Deparser: invalid packet (packet too short)");
            builder->emitIndent();
            // We immediately return instead of jumping to reject state.
            // It avoids reaching BPF_COMPLEXITY_LIMIT_JMP_SEQ.
            builder->appendFormat("return %s;", builder->target->abortReturnCode().c_str());
            builder->newline();
            builder->blockEnd(true);
            builder->emitIndent();
            builder->newline();
            unsigned hdrOffsetBits = 0;
            for (auto f : headerToEmit->fields) {
                auto ftype = deparser->program->typeMap->getType(f);
                auto etype = EBPFTypeFactory::instance->create(ftype);
                auto et = etype->to<IHasWidth>();
                if (et == nullptr) {
                    ::P4::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                                "Only headers with fixed widths supported %1%", f);
                    return;
                }
                emitField(builder, f->name, expr, hdrOffsetBits, etype);
                hdrOffsetBits += et->widthInBits();
            }

            // Increment header pointer
            builder->emitIndent();
            builder->appendFormat("%s += BYTES(%u);", program->headerStartVar.c_str(), width);
            builder->newline();

            builder->blockEnd(true);
        } else {
            BUG("emit() should only be invoked for packet_out");
        }
    }
}

void DeparserHdrEmitTranslator::emitField(CodeBuilder *builder, cstring field,
                                          const IR::Expression *hdrExpr, unsigned hdrOffsetBits,
                                          EBPF::EBPFType *type) {
    unsigned alignment = hdrOffsetBits % 8;
    auto program = deparser->program;

    auto et = type->to<IHasWidth>();
    if (et == nullptr) {
        ::P4::error(ErrorType::ERR_UNSUPPORTED_ON_TARGET,
                    "Only headers with fixed widths supported %1%", hdrExpr);
        return;
    }
    unsigned widthToEmit = et->widthInBits();
    unsigned emitSize = 0;
    cstring swap = cstring::empty, msgStr;

    if (widthToEmit <= 64) {
        if (program->options.emitTraceMessages) {
            builder->emitIndent();
            builder->blockStart();
            builder->emitIndent();
            builder->append("u64 tmp = ");
            visit(hdrExpr);
            builder->appendFormat(".%s", field.c_str());
            builder->endOfStatement(true);
            msgStr = absl::StrFormat("Deparser: emitting field %v=0x%%llx (%u bits)", field,
                                     widthToEmit);
            builder->target->emitTraceMessage(builder, msgStr.c_str(), 1, "tmp");
            builder->blockEnd(true);
        }
    } else {
        msgStr = absl::StrFormat("Deparser: emitting field %v (%u bits)", field, widthToEmit);
        builder->target->emitTraceMessage(builder, msgStr.c_str());
    }

    if (widthToEmit <= 8) {
        emitSize = 8;
    } else if (widthToEmit <= 16) {
        swap = "bpf_htons"_cs;
        emitSize = 16;
    } else if (widthToEmit <= 32) {
        swap = "htonl"_cs;
        emitSize = 32;
    } else if (widthToEmit <= 64) {
        swap = "htonll"_cs;
        emitSize = 64;
    }
    unsigned shift =
        widthToEmit < 8 ? (emitSize - alignment - widthToEmit) : (emitSize - widthToEmit);

    if (!swap.isNullOrEmpty()) {
        builder->emitIndent();
        visit(hdrExpr);
        builder->appendFormat(".%v = %v(", field, swap);
        visit(hdrExpr);
        builder->appendFormat(".%v", field);
        if (shift != 0) builder->appendFormat(" << %d", shift);
        builder->append(")");
        builder->endOfStatement(true);
    }
    unsigned bitsInFirstByte = widthToEmit % 8;
    if (bitsInFirstByte == 0) bitsInFirstByte = 8;
    unsigned bitsInCurrentByte = bitsInFirstByte;
    unsigned left = widthToEmit;
    for (unsigned i = 0; i < (widthToEmit + 7) / 8; i++) {
        builder->emitIndent();
        builder->appendFormat("%s = ((char*)(&", program->byteVar.c_str());
        visit(hdrExpr);
        builder->appendFormat(".%v))[%d]", field, i);
        builder->endOfStatement(true);
        unsigned freeBits = alignment != 0 ? (8 - alignment) : 8;
        bitsInCurrentByte = left >= 8 ? 8 : left;
        unsigned bitsToWrite = bitsInCurrentByte > freeBits ? freeBits : bitsInCurrentByte;
        BUG_CHECK((bitsToWrite > 0) && (bitsToWrite <= 8), "invalid bitsToWrite %d", bitsToWrite);
        builder->emitIndent();
        if (alignment == 0 && bitsToWrite == 8) {  // write whole byte
            builder->appendFormat("write_byte(%s, BYTES(%u) + %d, (%s))",
                                  program->headerStartVar.c_str(), hdrOffsetBits,
                                  i,  // do not reverse byte order
                                  program->byteVar.c_str());
        } else {  // write partial
            shift = (8 - alignment - bitsToWrite);
            builder->appendFormat("write_partial_ex(%s + BYTES(%u) + %d, %d, %d, (%s >> %d))",
                                  program->headerStartVar.c_str(), hdrOffsetBits,
                                  i,  // do not reverse byte order
                                  bitsToWrite, shift, program->byteVar.c_str(),
                                  widthToEmit > freeBits ? alignment == 0 ? shift : alignment : 0);
        }
        builder->endOfStatement(true);
        left -= bitsToWrite;
        bitsInCurrentByte -= bitsToWrite;
        alignment = (alignment + bitsToWrite) % 8;
        bitsToWrite = (8 - bitsToWrite);
        if (bitsInCurrentByte > 0) {
            builder->emitIndent();
            if (bitsToWrite == 8) {
                builder->appendFormat("write_byte(%s, BYTES(%u) + %d + 1, (%s << %d))",
                                      program->headerStartVar.c_str(), hdrOffsetBits,
                                      i,  // do not reverse byte order
                                      program->byteVar.c_str(), 8 - alignment % 8);
            } else {
                builder->appendFormat("write_partial_ex(%s + BYTES(%u) + %d + 1, %d, %d, (%s))",
                                      program->headerStartVar.c_str(), hdrOffsetBits,
                                      i,  // do not reverse byte order
                                      bitsToWrite, 8 + alignment - bitsToWrite,
                                      program->byteVar.c_str());
            }
            builder->endOfStatement(true);
            left -= bitsToWrite;
        }
        alignment = (alignment + bitsToWrite) % 8;
    }
    builder->newline();
}

bool EBPFDeparser::build() {
    hitVariable = program->refMap->newName("hit");
    auto pl = controlBlock->container->type->applyParams;
    if (pl->size() != 2) {
        ::P4::error(ErrorType::ERR_EXPECTED,
                    "Expected deparser block to have exactly 2 parameters");
        return false;
    }

    auto it = pl->parameters.begin();
    headers = *it;
    ++it;
    packet_out = *it;

    codeGen = new EBPF::ControlBodyTranslator(this);
    codeGen->substitute(headers, parserHeaders);

    scanConstants();
    return ::P4::errorCount() == 0;
}

void EBPFDeparser::emitBufferAdjusts(CodeBuilder *builder) const {
    builder->newline();
    builder->emitIndent();

    builder->appendFormat("int %s = BYTES(%s) - (%s - (u8*)%s)", outerHdrOffsetVar.c_str(),
                          outerHdrLengthVar.c_str(), program->headerStartVar.c_str(),
                          program->packetStartVar.c_str());
    builder->endOfStatement(true);
    builder->emitIndent();
    builder->appendFormat("if (%s != 0) ", outerHdrOffsetVar.c_str());
    builder->blockStart();
    builder->target->emitTraceMessage(builder, "Deparser: pkt_len adjusting by %d B", 1,
                                      outerHdrOffsetVar.c_str());
    builder->emitIndent();
    builder->appendFormat("int %s = 0", returnCode.c_str());
    builder->endOfStatement(true);
    builder->emitIndent();
    builder->appendFormat("%s = ", returnCode.c_str());
    program->progTarget->emitResizeBuffer(builder, program->model.CPacketName.toString(),
                                          outerHdrOffsetVar);
    builder->endOfStatement(true);

    builder->emitIndent();
    builder->appendFormat("if (%s) ", returnCode.c_str());
    builder->blockStart();
    builder->target->emitTraceMessage(builder, "Deparser: pkt_len adjust failed");
    builder->emitIndent();
    // We immediately return instead of jumping to reject state.
    // It avoids reaching BPF_COMPLEXITY_LIMIT_JMP_SEQ.
    builder->appendFormat("return %s;", builder->target->abortReturnCode().c_str());
    builder->newline();
    builder->blockEnd(true);
    builder->target->emitTraceMessage(builder, "Deparser: pkt_len adjusted");
    builder->blockEnd(true);
}

void EBPFDeparser::emit(CodeBuilder *builder) {
    codeGen->setBuilder(builder);

    for (auto a : controlBlock->container->controlLocals) emitDeclaration(builder, a);

    emitDeparserExternCalls(builder);
    builder->newline();

    emitPreDeparser(builder);

    builder->emitIndent();
    builder->appendFormat("int %s = 0", this->outerHdrLengthVar.c_str());
    builder->endOfStatement(true);

    auto prepareBufferTranslator = new DeparserPrepareBufferTranslator(this);
    prepareBufferTranslator->setBuilder(builder);
    prepareBufferTranslator->copyPointerVariables(codeGen);
    prepareBufferTranslator->substitute(this->headers, this->parserHeaders);
    controlBlock->container->body->apply(*prepareBufferTranslator);

    emitBufferAdjusts(builder);

    builder->emitIndent();
    builder->appendFormat("%v = %v;", program->packetStartVar,
                          builder->target->dataOffset(program->model.CPacketName.toString()));
    builder->newline();
    builder->emitIndent();
    builder->appendFormat("%v = %v;", program->headerStartVar, program->packetStartVar);
    builder->newline();
    builder->emitIndent();
    builder->appendFormat("%v = %v;", program->packetEndVar,
                          builder->target->dataEnd(program->model.CPacketName.toString()));
    builder->newline();

    // emit headers
    auto hdrEmitTranslator = new DeparserHdrEmitTranslator(this);
    hdrEmitTranslator->setBuilder(builder);
    hdrEmitTranslator->copyPointerVariables(codeGen);
    hdrEmitTranslator->substitute(this->headers, this->parserHeaders);
    controlBlock->container->body->apply(*hdrEmitTranslator);

    builder->newline();
}

}  // namespace P4::EBPF
