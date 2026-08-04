// RUN: %acir_opt_public %s | %FileCheck %s
// RUN: %acir_opt_public %s | %acir_opt_public | %FileCheck %s
// RUN: %acir_opt_public --pass-pipeline='builtin.module(canonicalize,cse)' %s | %FileCheck %s --check-prefix=EFFECTS

builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @Top(i32) parameters {} graph {
  ^bb0(%arg0 : i32):
    ac.process @control kind "control" captures(%arg0 : i32) {
    ^bb0(%capture : i32):
      %c0 = arith.constant 0 : i64
      %c1 = arith.constant 1 : i64
      %idx = index.constant 0
      %ready = arith.cmpi eq, %c0, %c0 : i64
      %accepted = ac.try_send @ready %capture : i32
      %value, %received = ac.try_recv @ready : i32
      ac.schedule @worker %value after %c1 : i32
      ac.wait_until %ready
      ac.wait_for @compute
      ac.await_event @done
      scf.if %accepted {
        ac.assert %received, "receive follows accepted send"
      }
      ac.yield_sim
    }
    ac.return
  }
}

// CHECK: ac.process @control kind "control" captures(%{{.*}} : i32)
// CHECK: ac.try_send @ready
// CHECK: ac.try_recv @ready
// CHECK: ac.schedule @worker
// CHECK: ac.wait_until
// CHECK: ac.wait_for @compute
// CHECK: ac.await_event @done
// CHECK: scf.if
// CHECK: ac.yield_sim
// EFFECTS: ac.try_send
// EFFECTS: ac.try_recv
// EFFECTS: ac.schedule
// EFFECTS: ac.wait_until
// EFFECTS: ac.wait_for
// EFFECTS: ac.await_event
// EFFECTS: ac.yield_sim
