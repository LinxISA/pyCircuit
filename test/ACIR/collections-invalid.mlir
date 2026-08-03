// RUN: %split_file %s %t
// RUN: %not %acir_opt %t/negative-shape.mlir 2>&1 | %FileCheck %s --check-prefix=NEGATIVE
// RUN: %not %acir_opt %t/wrong-cardinality.mlir 2>&1 | %FileCheck %s --check-prefix=CARDINALITY
// RUN: %not %acir_opt %t/heterogeneous-shape.mlir 2>&1 | %FileCheck %s --check-prefix=HETERO
// RUN: %not %acir_opt %t/bad-permutation.mlir 2>&1 | %FileCheck %s --check-prefix=PERMUTE
// RUN: %not %acir_opt %t/duplicate-owned-path.mlir 2>&1 | %FileCheck %s --check-prefix=DUP-OWNED
// RUN: %not %acir_opt %t/too-large.mlir 2>&1 | %FileCheck %s --check-prefix=TOO-LARGE
// RUN: %not %acir_opt %t/view-overflow.mlir 2>&1 | %FileCheck %s --check-prefix=VIEW-OVERFLOW

//--- negative-shape.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.extern"() <{sym_name = "Leaf", function_type = () -> (), static_params = {}, implementation = {registry = "cpp", name = "Leaf"}}> : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.array"() <{definition = @Leaf, sym_name = "a", stable_id = "a", path = "a", shape = array<i64: -1>, static_args = []}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// NEGATIVE: array shape dimensions must be non-negative

//--- wrong-cardinality.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.extern"() <{sym_name = "Leaf", function_type = () -> (), static_params = {}, implementation = {registry = "cpp", name = "Leaf"}}> : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.array"() <{definition = @Leaf, sym_name = "a", stable_id = "a", path = "a", shape = array<i64: 2>, static_args = [{}]}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// CARDINALITY: one concrete static argument set per lexicographically ordered element

//--- heterogeneous-shape.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.extern"() <{sym_name = "A", function_type = (i32) -> i32, static_params = {}, implementation = {registry = "cpp", name = "A"}}> : () -> ()
  "ac.module.extern"() <{sym_name = "B", function_type = (i64) -> i32, static_params = {}, implementation = {registry = "cpp", name = "B"}}> : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = (i32, i32) -> (i32, i32), static_params = {}}> ({
  ^bb0(%a : i32, %b : i32):
    %x:2 = "ac.instances"(%a, %b) <{definitions = [@A, @B], names = ["a", "b"], stable_ids = ["a", "b"], paths = ["a", "b"], interface = (i32) -> i32, static_args = [{}, {}]}> : (i32, i32) -> (i32, i32)
    "ac.return"(%x#0, %x#1) : (i32, i32) -> ()
  }) : () -> ()
}
// HETERO: does not implement the exact declared common interface

//--- bad-permutation.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "Top", function_type = (i32, i32) -> (i32, i32), static_params = {}}> ({
  ^bb0(%a : i32, %b : i32):
    %x:2 = "ac.view"(%a, %b) <{kind = "permutation", indices = array<i64: 0, 0>, shape = array<i64: 2>}> : (i32, i32) -> (i32, i32)
    "ac.return"(%x#0, %x#1) : (i32, i32) -> ()
  }) : () -> ()
}
// PERMUTE: permutation indices must be an in-bounds bijection

//--- duplicate-owned-path.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.system"() <{sym_name = "s", root = @Top, root_name = "root", tick_epoch = 0 : i64, tick_unit = "cycle", seed_policy = {kind = "fixed", value = 0 : i64}, instrumentation = [], result_schema = {kind = "none"}, selected = true}> : () -> ()
  "ac.module.extern"() <{sym_name = "A", function_type = () -> (), static_params = {}, implementation = {registry = "cpp", name = "A"}}> : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.instances"() <{definitions = [@A, @A], names = ["a", "b"], stable_ids = ["a", "b"], paths = ["same", "same"], interface = () -> (), static_args = [{}, {}]}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// DUP-OWNED: duplicate elaborated hierarchy path 'root.same'

//--- too-large.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.extern"() <{sym_name = "Leaf", function_type = () -> (), static_params = {}, implementation = {registry = "cpp", name = "Leaf"}}> : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.array"() <{definition = @Leaf, sym_name = "huge", stable_id = "huge", path = "huge", shape = array<i64: 1048577>, static_args = []}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// TOO-LARGE: array cardinality exceeds static elaboration bound 1048576

//--- view-overflow.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.view"() <{kind = "concat", indices = array<i64>, shape = array<i64: 9223372036854775807, 3>}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// VIEW-OVERFLOW: view cardinality overflows 64 bits
