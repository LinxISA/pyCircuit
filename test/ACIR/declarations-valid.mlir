// RUN: %acir_opt %s | %FileCheck %s
// RUN: %acir_opt %s | %acir_opt | %FileCheck %s

builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.type_alias"() <{sym_name = "Word", target = i32}> : () -> ()
    "ac.struct"() <{sym_name = "Header", field_names = ["opcode", "tag"], field_types = [i8, i16]}> : () -> ()
    "ac.enum"() <{sym_name = "Opcode", enumerants = ["read", "write"]}> : () -> ()
    "ac.union"() <{sym_name = "Payload", field_names = ["kind", "word"], field_types = [i8, i32], discriminator = "kind"}> : () -> ()
    "ac.packet"() <{sym_name = "Request", field_names = ["opcode", "payload"], field_types = [i8, i32], serialization = {alignment = 4 : i64, endianness = "little", size = 8 : i64}}> : () -> ()
    "ac.transaction"() <{sym_name = "Dma", field_names = ["request", "tag"], field_types = [!ac.packet<@Request>, i16]}> : () -> ()
  }) : () -> ()
}

// CHECK: "ac.type_scope"
// CHECK: "ac.type_alias"
// CHECK: "ac.struct"
// CHECK: "ac.enum"
// CHECK: "ac.union"
// CHECK: "ac.packet"
// CHECK: "ac.transaction"
