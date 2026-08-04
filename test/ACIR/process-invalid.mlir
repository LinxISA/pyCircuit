// RUN: %split_file %s %t
// RUN: %not %acir_opt %t/bad-kind.mlir 2>&1 | %FileCheck %s --check-prefix=KIND
// RUN: %not %acir_opt %t/no-suspend.mlir 2>&1 | %FileCheck %s --check-prefix=PROGRESS
// RUN: %not %acir_opt %t/linear-live.mlir 2>&1 | %FileCheck %s --check-prefix=LIVE
// RUN: %not %acir_opt %t/topology.mlir 2>&1 | %FileCheck %s --check-prefix=TOPOLOGY
// RUN: %not %acir_opt %t/missing-termination.mlir 2>&1 | %FileCheck %s --check-prefix=TERMINATION
// RUN: %not %acir_opt %t/capture-mismatch.mlir 2>&1 | %FileCheck %s --check-prefix=CAPTURE
// RUN: %not %acir_opt %t/result-live.mlir 2>&1 | %FileCheck %s --check-prefix=RESULT-LIVE
// RUN: %not %acir_opt %t/duplicate-owner-name.mlir 2>&1 | %FileCheck %s --check-prefix=OWNER-NAME
// RUN: %not %acir_opt %t/unstable-owner-segment.mlir 2>&1 | %FileCheck %s --check-prefix=OWNER-SEGMENT

//--- bad-kind.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @p kind "thread" { ac.yield_sim }
    ac.return
  }
}
// KIND: kind must be 'control', 'workload', or 'monitor'

//--- no-suspend.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @p kind "control" {
      %t = arith.constant true
      scf.while (%arg = %t) : (i1) -> i1 {
        scf.condition(%arg) %arg : i1
      } do {
      ^bb0(%arg : i1):
        scf.yield %arg : i1
      }
      ac.yield_sim
    }
    ac.return
  }
}
// PROGRESS: scf.while in ac.process must contain an explicit suspension point

//--- linear-live.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M(!ac.resource_token<@r>) parameters {} graph {
  ^bb0(%token : !ac.resource_token<@r>):
    ac.process @p kind "control" captures(%token : !ac.resource_token<@r>) {
    ^bb0(%captured : !ac.resource_token<@r>):
      %c1 = arith.constant 1 : i64
      ac.await_event @events
      ac.schedule @worker %captured after %c1 : !ac.resource_token<@r>
      ac.yield_sim
    }
    ac.return
  }
}
// LIVE: cannot remain live across suspension

//--- topology.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @p kind "control" {
      ac.instance @illegal of @M() static {} id "illegal" path "illegal" : () -> ()
      ac.yield_sim
    }
    ac.return
  }
}
// TOPOLOGY: ac.process contains unsupported operation ac.instance

//--- missing-termination.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @p kind "control" { %zero = arith.constant 0 : i64 }
    ac.return
  }
}
// TERMINATION: body must terminate with ac.yield_sim

//--- capture-mismatch.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M(i32) parameters {} graph {
  ^bb0(%value : i32):
    ac.process @p kind "control" captures(%value : i32) {
    ^bb0(%wrong : i64):
      ac.yield_sim
    }
    ac.return
  }
}
// CAPTURE: body arguments must exactly match capture types

//--- result-live.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @p kind "control" {
      %one = arith.constant 1 : i64
      %token, %received = ac.try_recv @tokens : !ac.resource_token<@r>
      ac.await_event @events
      ac.schedule @worker %token after %one : !ac.resource_token<@r>
      ac.yield_sim
    }
    ac.return
  }
}
// RESULT-LIVE: cannot remain live across suspension

//--- duplicate-owner-name.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @state kind "control" { ac.yield_sim }
    ac.stat @state kind "counter"
    ac.return
  }
}
// OWNER-NAME: duplicate local structural name 'state'

//--- unstable-owner-segment.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @"bad.name" kind "control" { ac.yield_sim }
    ac.return
  }
}
// OWNER-SEGMENT: symbol name must be one stable hierarchy owner segment
