// RUN: %split_file %s %t
// RUN: %not %acir_opt %t/duplicate-symbol.mlir 2>&1 | %FileCheck %s --check-prefix=DUP-SYMBOL
// RUN: %not %acir_opt %t/duplicate-field.mlir 2>&1 | %FileCheck %s --check-prefix=DUP-FIELD
// RUN: %not %acir_opt %t/duplicate-enumerant.mlir 2>&1 | %FileCheck %s --check-prefix=DUP-ENUM
// RUN: %not %acir_opt %t/unresolved-field.mlir 2>&1 | %FileCheck %s --check-prefix=UNRESOLVED
// RUN: %not %acir_opt %t/wrong-kind.mlir 2>&1 | %FileCheck %s --check-prefix=WRONG-KIND
// RUN: %not %acir_opt %t/recursive.mlir 2>&1 | %FileCheck %s --check-prefix=RECURSIVE
// RUN: %not %acir_opt %t/union-discriminator.mlir 2>&1 | %FileCheck %s --check-prefix=DISCRIMINATOR
// RUN: %not %acir_opt %t/packet-layout.mlir 2>&1 | %FileCheck %s --check-prefix=LAYOUT
// RUN: %not %acir_opt %t/alias-target.mlir 2>&1 | %FileCheck %s --check-prefix=ALIAS
// RUN: %not %acir_opt %t/transaction-fields.mlir 2>&1 | %FileCheck %s --check-prefix=TRANSACTION

// DUP-SYMBOL: error: {{.*}}redefinition of symbol named 'Item'
// DUP-FIELD: error: {{.*}}duplicate field 'x'
// DUP-ENUM: error: {{.*}}duplicate enumerant 'read'
// UNRESOLVED: error: {{.*}}unresolved named data type '@Missing'
// WRONG-KIND: error: {{.*}}named type '@Mode' requires ac.struct but resolves to ac.enum
// RECURSIVE: error: {{.*}}unbounded value recursion through '@Node'
// DISCRIMINATOR: error: {{.*}}union discriminator 'value' must name an integer or enum field
// LAYOUT: error: {{.*}}packet serialization metadata requires positive size and alignment and explicit endianness
// ALIAS: error: {{.*}}unresolved named data type '@Missing'
// TRANSACTION: error: {{.*}}field name and type counts must match

//--- duplicate-symbol.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.struct"() <{sym_name = "Item", field_names = [], field_types = []}> : () -> ()
    "ac.packet"() <{sym_name = "Item", field_names = [], field_types = [], serialization = {alignment = 1 : i64, endianness = "little", size = 1 : i64}}> : () -> ()
  }) : () -> ()
}

//--- duplicate-field.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.struct"() <{sym_name = "Pair", field_names = ["x", "x"], field_types = [i8, i16]}> : () -> ()
  }) : () -> ()
}

//--- duplicate-enumerant.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.enum"() <{sym_name = "Mode", enumerants = ["read", "read"]}> : () -> ()
  }) : () -> ()
}

//--- unresolved-field.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.struct"() <{sym_name = "Holder", field_names = ["item"], field_types = [!ac.struct<@Missing>]}> : () -> ()
  }) : () -> ()
}

//--- wrong-kind.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.enum"() <{sym_name = "Mode", enumerants = ["read"]}> : () -> ()
    "ac.struct"() <{sym_name = "Holder", field_names = ["mode"], field_types = [!ac.struct<@Mode>]}> : () -> ()
  }) : () -> ()
}

//--- recursive.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.struct"() <{sym_name = "Node", field_names = ["next"], field_types = [!ac.struct<@Node>]}> : () -> ()
  }) : () -> ()
}

//--- union-discriminator.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.union"() <{sym_name = "Bad", field_names = ["value"], field_types = [f32], discriminator = "value"}> : () -> ()
  }) : () -> ()
}

//--- packet-layout.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.packet"() <{sym_name = "Bad", field_names = [], field_types = [], serialization = {alignment = 0 : i64, size = 0 : i64}}> : () -> ()
  }) : () -> ()
}

//--- alias-target.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.type_alias"() <{sym_name = "MissingAlias", target = !ac.struct<@Missing>}> : () -> ()
  }) : () -> ()
}

//--- transaction-fields.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.transaction"() <{sym_name = "Bad", field_names = ["x"], field_types = []}> : () -> ()
  }) : () -> ()
}
