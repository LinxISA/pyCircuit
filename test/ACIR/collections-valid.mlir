// RUN: %acir_opt %s | %FileCheck %s

builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.system"() <{sym_name = "collections", root = @Top, root_name = "root", tick_epoch = 0 : i64, tick_unit = "cycle", seed_policy = {kind = "fixed", value = 0 : i64}, instrumentation = [], result_schema = {kind = "none"}, selected = true}> : () -> ()
  "ac.module"() <{sym_name = "Leaf", function_type = (i32) -> i32, static_params = {}}> ({
  ^bb0(%arg0 : i32):
    "ac.return"(%arg0) : (i32) -> ()
  }) : () -> ()
  "ac.module"() <{sym_name = "Leaf2", function_type = (i32) -> i32, static_params = {}}> ({
  ^bb0(%arg0 : i32):
    "ac.return"(%arg0) : (i32) -> ()
  }) : () -> ()
  "ac.module"() <{sym_name = "Top", function_type = (i32, i32) -> (i32, i32), static_params = {}}> ({
  ^bb0(%a : i32, %b : i32):
    %x:2 = "ac.array"(%a, %b) <{definition = @Leaf, sym_name = "lanes", stable_id = "lanes", path = "lanes", shape = array<i64: 2>, static_args = [{}, {}]}> : (i32, i32) -> (i32, i32)
    %y:2 = "ac.instances"(%x#0, %x#1) <{definitions = [@Leaf, @Leaf2], names = ["a", "b"], stable_ids = ["a", "b"], paths = ["mix_a", "mix_b"], interface = (i32) -> i32, static_args = [{}, {}]}> : (i32, i32) -> (i32, i32)
    %z:2 = "ac.view"(%y#0, %y#1) <{kind = "permutation", indices = array<i64: 1, 0>, shape = array<i64: 2>}> : (i32, i32) -> (i32, i32)
    %selected = "ac.view"(%y#0, %y#1) <{kind = "selection", indices = array<i64: 0>, shape = array<i64: 1>}> : (i32, i32) -> i32
    %slice:2 = "ac.view"(%y#0, %y#1) <{kind = "slice", indices = array<i64: 0, 2, 1>, shape = array<i64: 2>}> : (i32, i32) -> (i32, i32)
    %concat:2 = "ac.view"(%y#0, %y#1) <{kind = "concat", indices = array<i64>, shape = array<i64: 2>}> : (i32, i32) -> (i32, i32)
    %zip:2 = "ac.view"(%y#0, %y#1) <{kind = "zip", indices = array<i64>, shape = array<i64: 2>}> : (i32, i32) -> (i32, i32)
    %bound:2 = "ac.view"(%y#0, %y#1) <{kind = "elementwise_binding", indices = array<i64>, shape = array<i64: 2>}> : (i32, i32) -> (i32, i32)
    "ac.return"(%z#0, %z#1) : (i32, i32) -> ()
  }) : () -> ()
  "ac.module"() <{sym_name = "ZipOrder", function_type = (i32, i64, i16, i1) -> (), static_params = {}}> ({
  ^bb0(%a0 : i32, %a1 : i64, %b0 : i16, %b1 : i1):
    %zip:4 = "ac.view"(%a0, %a1, %b0, %b1) <{kind = "zip", indices = array<i64>, shape = array<i64: 4>}> : (i32, i64, i16, i1) -> (i32, i16, i64, i1)
    "ac.return"() : () -> ()
  }) : () -> ()
}

// CHECK: ac.array
// CHECK: ac.instances
// CHECK: ac.view
