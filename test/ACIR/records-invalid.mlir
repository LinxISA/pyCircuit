// RUN: %split_file %s %t
// RUN: %not %acir_opt %t/missing-field.mlir 2>&1 | %FileCheck %s --check-prefix=MISSING
// RUN: %not %acir_opt %t/create-type.mlir 2>&1 | %FileCheck %s --check-prefix=CREATE-TYPE
// RUN: %not %acir_opt %t/get-type.mlir 2>&1 | %FileCheck %s --check-prefix=GET-TYPE
// RUN: %not %acir_opt %t/with-identity.mlir 2>&1 | %FileCheck %s --check-prefix=IDENTITY
// RUN: %not %acir_opt %t/serialize-kind.mlir 2>&1 | %FileCheck %s --check-prefix=SERIALIZE-KIND
// RUN: %not %acir_opt %t/serialize-width.mlir 2>&1 | %FileCheck %s --check-prefix=SERIALIZE-WIDTH
// RUN: %not %acir_opt %t/deserialize-identity.mlir 2>&1 | %FileCheck %s --check-prefix=DESERIALIZE-ID

// MISSING: error: {{.*}}record.create fields must exactly match declaration
// CREATE-TYPE: error: {{.*}}field 'x' expects 'i8' but received 'i16'
// GET-TYPE: error: {{.*}}field 'x' has type 'i8' but operation returns 'i16'
// IDENTITY: error: {{.*}}record.with must preserve record identity
// SERIALIZE-KIND: error: {{.*}}packet.serialize requires a packet operand
// SERIALIZE-WIDTH: error: {{.*}}serialized byte vector width must equal packet size 4
// DESERIALIZE-ID: error: {{.*}}packet.deserialize result identity does not match serialization contract

//--- missing-field.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.struct"() <{sym_name = "Pair", field_names = ["x", "y"], field_types = [i8, i8]}> : () -> ()
    %x = "builtin.unrealized_conversion_cast"() : () -> i8
    %v = "ac.record.create"(%x) <{field_names = ["x"]}> : (i8) -> !ac.struct<@Pair>
  }) : () -> ()
}

//--- create-type.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.struct"() <{sym_name = "One", field_names = ["x"], field_types = [i8]}> : () -> ()
    %x = "builtin.unrealized_conversion_cast"() : () -> i16
    %v = "ac.record.create"(%x) <{field_names = ["x"]}> : (i16) -> !ac.struct<@One>
  }) : () -> ()
}

//--- get-type.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.struct"() <{sym_name = "One", field_names = ["x"], field_types = [i8]}> : () -> ()
    %v = "builtin.unrealized_conversion_cast"() : () -> !ac.struct<@One>
    %x = "ac.record.get"(%v) <{field = "x"}> : (!ac.struct<@One>) -> i16
  }) : () -> ()
}

//--- with-identity.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.struct"() <{sym_name = "A", field_names = ["x"], field_types = [i8]}> : () -> ()
    "ac.struct"() <{sym_name = "B", field_names = ["x"], field_types = [i8]}> : () -> ()
    %v = "builtin.unrealized_conversion_cast"() : () -> !ac.struct<@A>
    %x = "builtin.unrealized_conversion_cast"() : () -> i8
    %bad = "ac.record.with"(%v, %x) <{field = "x"}> : (!ac.struct<@A>, i8) -> !ac.struct<@B>
  }) : () -> ()
}

//--- serialize-kind.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.struct"() <{sym_name = "S", field_names = [], field_types = []}> : () -> ()
    %v = "builtin.unrealized_conversion_cast"() : () -> !ac.struct<@S>
    %bytes = "ac.packet.serialize"(%v) <{packet = @S}> : (!ac.struct<@S>) -> !ac.vector<1 x i8>
  }) : () -> ()
}

//--- serialize-width.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.packet"() <{sym_name = "P", field_names = [], field_types = [], serialization = {alignment = 1 : i64, endianness = "little", size = 4 : i64}}> : () -> ()
    %v = "builtin.unrealized_conversion_cast"() : () -> !ac.packet<@P>
    %bytes = "ac.packet.serialize"(%v) <{packet = @P}> : (!ac.packet<@P>) -> !ac.vector<8 x i8>
  }) : () -> ()
}

//--- deserialize-identity.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.packet"() <{sym_name = "P", field_names = [], field_types = [], serialization = {alignment = 1 : i64, endianness = "little", size = 4 : i64}}> : () -> ()
    "ac.packet"() <{sym_name = "Q", field_names = [], field_types = [], serialization = {alignment = 1 : i64, endianness = "little", size = 4 : i64}}> : () -> ()
    %bytes = "builtin.unrealized_conversion_cast"() : () -> !ac.vector<4 x i8>
    %v = "ac.packet.deserialize"(%bytes) <{packet = @P}> : (!ac.vector<4 x i8>) -> !ac.packet<@Q>
  }) : () -> ()
}
