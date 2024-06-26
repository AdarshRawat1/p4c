/*
Copyright 2017 VMware, Inc.

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

#include <core.p4>
#include <pna.p4>

header Hdr1 {
    bit<8> a;
}

header Hdr2 {
    bit<16> b;
}

header_union U {
    Hdr1 h1;
    Hdr2 h2;
}

struct Headers {
    Hdr1 h1;
    U u;
}

struct Meta {}

parser p(packet_in b, out Headers h, inout Meta m,
    in    pna_main_parser_input_metadata_t istd) {
    state start {
        b.extract(h.h1);
        transition select(h.h1.a) {
            0: getH1;
            default: getH2;
        }
    }

    state getH1 {
        b.extract(h.u.h1);
        transition accept;
    }

    state getH2 {
        b.extract(h.u.h2);
        transition accept;
    }
}

control deparser(packet_out b, in Headers h, in Meta m,
    in    pna_main_output_metadata_t ostd) {
    apply {
        b.emit(h.h1);
        b.emit(h.u);
    }
}

control ingress(inout Headers h, inout Meta m,
    in    pna_main_input_metadata_t  istd,
    inout pna_main_output_metadata_t ostd) {
    apply {}
}

control PreControlImpl(
    in    Headers  hdr,
    inout Meta meta,
    in    pna_pre_input_metadata_t  istd,
    inout pna_pre_output_metadata_t ostd)
{
    apply {
    }
}


PNA_NIC(p(), PreControlImpl(), ingress(), deparser()) main;
