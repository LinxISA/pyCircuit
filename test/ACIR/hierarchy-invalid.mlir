// RUN: %split_file %s %t
// RUN: %not %acir_opt %t/no-selected.mlir 2>&1 | %FileCheck %s --check-prefix=NO-SELECTED
// RUN: %not %acir_opt %t/bad-ref.mlir 2>&1 | %FileCheck %s --check-prefix=BAD-REF
// RUN: %not %acir_opt %t/bad-call.mlir 2>&1 | %FileCheck %s --check-prefix=BAD-CALL
// RUN: %not %acir_opt %t/bad-return.mlir 2>&1 | %FileCheck %s --check-prefix=BAD-RETURN
// RUN: %not %acir_opt %t/duplicate-path.mlir 2>&1 | %FileCheck %s --check-prefix=DUP-PATH
// RUN: %not %acir_opt %t/dynamic-param.mlir 2>&1 | %FileCheck %s --check-prefix=DYNAMIC
// RUN: %not %acir_opt %t/generic-binding.mlir 2>&1 | %FileCheck %s --check-prefix=BINDING
// RUN: %not %acir_opt %t/private-export.mlir 2>&1 | %FileCheck %s --check-prefix=PRIVATE
// RUN: %not %acir_opt %t/missing-static-arg.mlir 2>&1 | %FileCheck %s --check-prefix=STATIC-ARG
// RUN: %not %acir_opt %t/unresolved-static-symbol.mlir 2>&1 | %FileCheck %s --check-prefix=STATIC-SYMBOL
// RUN: %not %acir_opt %t/unknown-generator.mlir 2>&1 | %FileCheck %s --check-prefix=UNKNOWN-GENERATOR
// RUN: %not %acir_opt %t/nonzero-epoch.mlir 2>&1 | %FileCheck %s --check-prefix=EPOCH
// RUN: %not %acir_opt %t/unresolved-workload.mlir 2>&1 | %FileCheck %s --check-prefix=WORKLOAD
// RUN: %not %acir_opt %t/direct-recursion.mlir 2>&1 | %FileCheck %s --check-prefix=DIRECT-RECURSION
// RUN: %not %acir_opt %t/mutual-recursion.mlir 2>&1 | %FileCheck %s --check-prefix=MUTUAL-RECURSION
// RUN: %not %acir_opt %t/absolute-local-path.mlir 2>&1 | %FileCheck %s --check-prefix=LOCAL-PATH
// RUN: %not %acir_opt %t/duplicate-id.mlir 2>&1 | %FileCheck %s --check-prefix=DUP-ID
// RUN: %not %acir_opt %t/mutual-recursion.mlir > /dev/null 2> %t/mutual.first
// RUN: %not %acir_opt %t/mutual-recursion.mlir > /dev/null 2> %t/mutual.second
// RUN: diff %t/mutual.first %t/mutual.second
// RUN: %not %acir_opt %t/static-arg-type.mlir 2>&1 | %FileCheck %s --check-prefix=STATIC-TYPE

//--- no-selected.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.system"() <{sym_name = "s", root = @Top, root_name = "root", tick_epoch = 0 : i64, tick_unit = "cycle", seed_policy = {kind = "fixed", value = 0 : i64}, instrumentation = [], result_schema = {kind = "none"}, selected = false}> : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({ "ac.return"() : () -> () }) : () -> ()
}
// NO-SELECTED: ACIR file requires exactly one selected ac.system, found 0

//--- bad-ref.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.system"() <{sym_name = "s", root = @Missing, root_name = "root", tick_epoch = 0 : i64, tick_unit = "cycle", seed_policy = {kind = "fixed", value = 0 : i64}, instrumentation = [], result_schema = {kind = "none"}, selected = true}> : () -> ()
}
// BAD-REF: root '@Missing' does not resolve to a module

//--- bad-call.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "Leaf", function_type = (i32) -> i32, static_params = {}}> ({
  ^bb0(%x : i32):
    "ac.return"(%x) : (i32) -> ()
  }) : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = (i64) -> i64, static_params = {}}> ({
  ^bb0(%x : i64):
    %v = "ac.instance"(%x) <{definition = @Leaf, sym_name = "x", stable_id = "x", path = "x", static_args = {}}> : (i64) -> i64
    "ac.return"(%v) : (i64) -> ()
  }) : () -> ()
}
// BAD-CALL: operand types do not match module signature

//--- bad-return.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "Top", function_type = (i32) -> i64, static_params = {}}> ({
  ^bb0(%x : i32):
    "ac.return"(%x) : (i32) -> ()
  }) : () -> ()
}
// BAD-RETURN: operand types and count must exactly match module results

//--- duplicate-path.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.system"() <{sym_name = "s", root = @Top, root_name = "root", tick_epoch = 0 : i64, tick_unit = "cycle", seed_policy = {kind = "fixed", value = 0 : i64}, instrumentation = [], result_schema = {kind = "none"}, selected = true}> : () -> ()
  "ac.module.extern"() <{sym_name = "Leaf", function_type = () -> (), static_params = {}, implementation = {registry = "cpp", name = "Leaf"}}> : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @Leaf, sym_name = "a", stable_id = "a", path = "same", static_args = {}}> : () -> ()
    "ac.instance"() <{definition = @Leaf, sym_name = "b", stable_id = "b", path = "same", static_args = {}}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// DUP-PATH: duplicate elaborated hierarchy path 'root.same'

//--- dynamic-param.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.extern"() <{sym_name = "Leaf", function_type = () -> (), static_params = {gain = 1.0 : f32}, implementation = {registry = "cpp", name = "Leaf"}}> : () -> ()
}
// DYNAMIC: static parameters must contain only concrete builtin static values

//--- generic-binding.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.generated"() <{sym_name = "Leaf", function_type = () -> (), static_params = {}, generator = {registry = "generic", name = "fallback"}}> : () -> ()
}
// BINDING: requires exact registered {registry, name} metadata

//--- private-export.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "Top", function_type = (!ac.resource_token<@owned>) -> !ac.resource_token<@owned>, static_params = {}}> ({
  ^bb0(%token : !ac.resource_token<@owned>):
    "ac.return"(%token) : (!ac.resource_token<@owned>) -> ()
  }) : () -> ()
}
// PRIVATE: private ownership handle cannot be exported from ac.module

//--- missing-static-arg.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.extern"() <{sym_name = "Leaf", function_type = () -> (), static_params = {width = 8 : i64}, implementation = {registry = "cpp", name = "Leaf"}}> : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @Leaf, sym_name = "leaf", stable_id = "leaf", path = "leaf", static_args = {}}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// STATIC-ARG: static argument names must exactly match definition parameters

//--- unresolved-static-symbol.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.extern"() <{sym_name = "Leaf", function_type = () -> (), static_params = {target = @Missing}, implementation = {registry = "cpp", name = "Leaf"}}> : () -> ()
}
// STATIC-SYMBOL: unresolved static symbol reference '@Missing'

//--- unknown-generator.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.generated"() <{sym_name = "Leaf", function_type = () -> (), static_params = {}, generator = {registry = "unknown", name = "Leaf"}}> : () -> ()
}
// UNKNOWN-GENERATOR: generated module requires registered registry 'ac'

//--- nonzero-epoch.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.extern"() <{sym_name = "Top", function_type = () -> (), static_params = {}, implementation = {registry = "cpp", name = "Top"}}> : () -> ()
  "ac.system"() <{sym_name = "s", root = @Top, root_name = "root", tick_epoch = 1 : i64, tick_unit = "cycle", seed_policy = {kind = "fixed", value = 0 : i64}, instrumentation = [], result_schema = {kind = "none"}, selected = true}> : () -> ()
}
// EPOCH: global tick epoch must be exactly 0

//--- unresolved-workload.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.extern"() <{sym_name = "Top", function_type = () -> (), static_params = {}, implementation = {registry = "cpp", name = "Top"}}> : () -> ()
  "ac.system"() <{sym_name = "s", root = @Top, root_name = "root", tick_epoch = 0 : i64, tick_unit = "cycle", primary_workload = @missing, seed_policy = {kind = "fixed", value = 0 : i64}, instrumentation = [], result_schema = {kind = "none"}, selected = true}> : () -> ()
}
// WORKLOAD: primary workload '@missing' is unresolved

//--- direct-recursion.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "Loop", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @Loop, sym_name = "self", stable_id = "self", path = "self", static_args = {}}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// DIRECT-RECURSION: recursive module instantiation cycle: @Loop -> @Loop

//--- mutual-recursion.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "A", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @B, sym_name = "b", stable_id = "b", path = "b", static_args = {}}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
  "ac.module"() <{sym_name = "B", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @A, sym_name = "a", stable_id = "a", path = "a", static_args = {}}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// MUTUAL-RECURSION: recursive module instantiation cycle: @A -> @B -> @A

//--- absolute-local-path.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.extern"() <{sym_name = "Leaf", function_type = () -> (), static_params = {}, implementation = {registry = "cpp", name = "Leaf"}}> : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @Leaf, sym_name = "leaf", stable_id = "leaf", path = "root.leaf", static_args = {}}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// LOCAL-PATH: path must be stable local segments

//--- duplicate-id.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.system"() <{sym_name = "s", root = @Top, root_name = "root", tick_epoch = 0 : i64, tick_unit = "cycle", seed_policy = {kind = "fixed", value = 0 : i64}, instrumentation = [], result_schema = {kind = "none"}, selected = true}> : () -> ()
  "ac.module.extern"() <{sym_name = "Leaf", function_type = () -> (), static_params = {}, implementation = {registry = "cpp", name = "Leaf"}}> : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @Leaf, sym_name = "a", stable_id = "same", path = "a", static_args = {}}> : () -> ()
    "ac.instance"() <{definition = @Leaf, sym_name = "b", stable_id = "same", path = "b", static_args = {}}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// DUP-ID: duplicate elaborated stable ownership id 'root/same'

//--- static-arg-type.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module.extern"() <{sym_name = "Leaf", function_type = () -> (), static_params = {width = 8 : i64}, implementation = {registry = "cpp", name = "Leaf"}}> : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @Leaf, sym_name = "leaf", stable_id = "leaf", path = "leaf", static_args = {width = 8 : i32}}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// STATIC-TYPE: static argument 'width' must match parameter attribute type 'i64'
