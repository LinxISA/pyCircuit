// RUN: %acir_opt %s | %FileCheck %s
// RUN: %acir_opt %s | %acir_opt | %FileCheck %s

builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.system"() <{sym_name = "soc", root = @Top, root_name = "root", tick_epoch = 0 : i64, tick_unit = "cycle", seed_policy = {kind = "fixed", value = 7 : i64}, instrumentation = [], result_schema = {kind = "none"}, selected = true}> : () -> ()
  "ac.system"() <{sym_name = "leaf_harness", root = @Leaf, root_name = "leaf", tick_epoch = 0 : i64, tick_unit = "cycle", seed_policy = {kind = "fixed", value = 9 : i64}, instrumentation = [], result_schema = {kind = "none"}, selected = false}> : () -> ()

  "ac.module"() <{sym_name = "Top", function_type = (i32) -> i32, static_params = {}}> ({
  ^bb0(%arg0 : i32):
    %0 = "ac.instance"(%arg0) <{definition = @Leaf, sym_name = "child", stable_id = "child", path = "child", static_args = {}}> : (i32) -> i32
    "ac.instance"() <{definition = @Reusable, sym_name = "left", stable_id = "left", path = "left", static_args = {}}> : () -> ()
    "ac.instance"() <{definition = @Reusable, sym_name = "right", stable_id = "right", path = "right", static_args = {}}> : () -> ()
    "ac.return"(%0) : (i32) -> ()
  }) : () -> ()

  "ac.module"() <{sym_name = "Leaf", function_type = (i32) -> i32, static_params = {}}> ({
  ^bb0(%arg0 : i32):
    "ac.return"(%arg0) : (i32) -> ()
  }) : () -> ()

  "ac.module.extern"() <{sym_name = "Ext", function_type = (i32) -> i32, static_params = {}, implementation = {registry = "cpp", name = "Ext"}}> : () -> ()
  "ac.module.generated"() <{sym_name = "Gen", function_type = (i32) -> i32, static_params = {}, generator = {registry = "ac", name = "Gen"}}> : () -> ()

  // One reusable definition may be instantiated by multiple parents. Its
  // relative child segment expands independently below each ownership path.
  "ac.module"() <{sym_name = "Reusable", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @Empty, sym_name = "leaf", stable_id = "leaf", path = "leaf", static_args = {}}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
  "ac.module"() <{sym_name = "ReuseHarness", function_type = () -> (), static_params = {}}> ({
    "ac.instance"() <{definition = @Reusable, sym_name = "left", stable_id = "left", path = "left", static_args = {}}> : () -> ()
    "ac.instance"() <{definition = @Reusable, sym_name = "right", stable_id = "right", path = "right", static_args = {}}> : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
  "ac.module.extern"() <{sym_name = "Empty", function_type = () -> (), static_params = {}, implementation = {registry = "cpp", name = "Empty"}}> : () -> ()

  // Graph-region SSA may use a value before its textual definition and cycle.
  "ac.module"() <{sym_name = "DataCycle", function_type = () -> i32, static_params = {}}> ({
    %a = "ac.instance"(%b) <{definition = @Leaf, sym_name = "left", stable_id = "data-left", path = "left", static_args = {}}> : (i32) -> i32
    %b = "ac.instance"(%a) <{definition = @Leaf, sym_name = "right", stable_id = "data-right", path = "right", static_args = {}}> : (i32) -> i32
    "ac.return"(%a) : (i32) -> ()
  }) : () -> ()
}

// CHECK: ac.system
// CHECK: ac.module
// CHECK: ac.instance
// CHECK: ac.return
// CHECK: ac.module.extern
// CHECK: ac.module.generated
