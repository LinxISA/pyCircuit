// RUN: %acir_opt %s | %FileCheck %s
// RUN: %acir_opt %s | %acir_opt | %FileCheck %s

builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.type_scope"() <{sym_name = "types"}> ({
    "ac.transaction"() <{sym_name = "A", fields = [{name = "tag", type = i8}]}> : () -> ()
    "ac.transaction"() <{sym_name = "B", fields = [{name = "tag", type = i8}]}> : () -> ()
  }) : () -> ()
  ac.module @M() parameters {} graph {
    ac.address_space @space width 8 unit "byte" id "space" path "space"
    ac.address_map @class_split source @space entries [
      {base = 0 : i64, size = 32 : i64, target = @space, offset = 0 : i64,
       permissions = ["read"], classes = [@types::@A]},
      {base = 0 : i64, size = 32 : i64, target = @space, offset = 0 : i64,
       permissions = ["read"], classes = [@types::@B]}
    ] default {kind = "unmapped"}
    ac.return
  }
}

// CHECK: ac.address_map @class_split
