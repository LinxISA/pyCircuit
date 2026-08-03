// RUN: %split_file %s %t
// RUN: %acir_opt %t/zero-chain.mlir | %FileCheck %s --check-prefix=ZERO
// RUN: %not %acir_opt %t/unresolved-source.mlir 2>&1 | %FileCheck %s --check-prefix=UNRESOLVED
// RUN: %not %acir_opt %t/repeated-source.mlir 2>&1 | %FileCheck %s --check-prefix=REPEATED

//--- zero-chain.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "Empty", function_type = () -> (), static_params = {}}> ({
    "ac.return"() : () -> ()
  }) : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @Empty, sym_name = "unrelated", stable_id = "unrelated", path = "unrelated", static_args = {}}> : () -> ()
    "ac.instance"() <{definition = @Empty, sym_name = "source", stable_id = "source", path = "source", static_args = {}}> : () -> ()
    "ac.view"() <{sym_name = "first", kind = "permutation", source_producers = [@source], source_shapes = [array<i64: 0>], indices = array<i64>, shape = array<i64: 0>}> : () -> ()
    "ac.view"() <{sym_name = "second", kind = "permutation", source_producers = [@first], source_shapes = [array<i64: 0>], indices = array<i64>, shape = array<i64: 0>}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// ZERO: ac.view @first
// ZERO: ac.view @second

//--- unresolved-source.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.view"() <{sym_name = "view", kind = "permutation", source_producers = [@missing], source_shapes = [array<i64: 0>], indices = array<i64>, shape = array<i64: 0>}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// UNRESOLVED: source producer '@missing' is unresolved

//--- repeated-source.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "Empty", function_type = () -> (), static_params = {}}> ({ "ac.return"() : () -> () }) : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @Empty, sym_name = "source", stable_id = "source", path = "source", static_args = {}}> : () -> ()
    "ac.view"() <{sym_name = "view", kind = "concat", source_producers = [@source, @source], source_shapes = [array<i64: 0>, array<i64: 0>], axis = 0 : i64, indices = array<i64>, shape = array<i64: 0>}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// REPEATED: source producers must not repeat
