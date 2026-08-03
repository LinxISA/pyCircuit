// RUN: %acir_opt %s | %FileCheck %s
// RUN: %acir_opt %s | %acir_opt | %FileCheck %s

builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.struct"() <{sym_name = "Header", field_names = ["opcode", "tag"], field_types = [i8, i16]}> : () -> ()
    "ac.packet"() <{sym_name = "Request", field_names = ["opcode", "payload"], field_types = [i8, i32], serialization = {alignment = 4 : i64, endianness = "little", size = 8 : i64}}> : () -> ()
    "ac.transaction"() <{sym_name = "Dma", field_names = ["request", "tag"], field_types = [!ac.packet<@Request>, i16]}> : () -> ()

    %opcode = "builtin.unrealized_conversion_cast"() : () -> i8
    %tag = "builtin.unrealized_conversion_cast"() : () -> i16
    %payload = "builtin.unrealized_conversion_cast"() : () -> i32
    %header = "ac.record.create"(%opcode, %tag) <{field_names = ["opcode", "tag"]}> : (i8, i16) -> !ac.struct<@Header>
    %got = "ac.record.get"(%header) <{field = "opcode"}> : (!ac.struct<@Header>) -> i8
    %updated = "ac.record.with"(%header, %got) <{field = "opcode"}> : (!ac.struct<@Header>, i8) -> !ac.struct<@Header>
    %packet = "ac.record.create"(%opcode, %payload) <{field_names = ["opcode", "payload"]}> : (i8, i32) -> !ac.packet<@Request>
    %packet2 = "ac.record.with"(%packet, %payload) <{field = "payload"}> : (!ac.packet<@Request>, i32) -> !ac.packet<@Request>
    %bytes = "ac.packet.serialize"(%packet2) <{packet = @Request}> : (!ac.packet<@Request>) -> !ac.vector<8 x i8>
    %copy = "ac.packet.deserialize"(%bytes) <{packet = @Request}> : (!ac.vector<8 x i8>) -> !ac.packet<@Request>
    %tx = "ac.record.create"(%copy, %tag) <{field_names = ["request", "tag"]}> : (!ac.packet<@Request>, i16) -> !ac.transaction<@Dma>
  }) : () -> ()
}

// CHECK: "ac.record.create"
// CHECK: "ac.record.get"
// CHECK: "ac.record.with"
// CHECK: "ac.packet.serialize"
// CHECK: "ac.packet.deserialize"
// CHECK-SAME: !ac.packet<@Request>
