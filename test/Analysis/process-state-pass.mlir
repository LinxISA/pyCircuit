// RUN: %acir_opt --verify-each=false --pass-pipeline='builtin.module(ac-freeze-topology,ac-lower-process-state)' %s | %FileCheck %s

// A minimal yield-only model. The freeze pass produces a frozen model,
// and the lower-process-state pass verifies it non-mutatingly.
// Both passes must succeed; the output must contain the frozen module unchanged.

// CHECK: module attributes {
// CHECK-SAME: ac.contract_epoch = "0.1"
// CHECK-SAME: ac.freeze_epoch = "0.1"
// CHECK-SAME: ac.topology_frozen = true
// CHECK: ac.process @workload

builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.system @soc root @Top as "root" tick 0 "cycle"
      workload @Top::@workload seed {kind = "fixed", value = 0 : i64}
      instrumentation [] results {id = "default", format = "json"}
      selected true
  ac.module @Top() parameters {} graph {
    ac.process @workload kind "workload" {
      ac.yield_sim
    }
    ac.return
  }
}
