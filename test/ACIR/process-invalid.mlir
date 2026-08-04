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
// RUN: %not %acir_opt %t/unreachable-suspension.mlir 2>&1 | %FileCheck %s --check-prefix=BACKEDGE
// RUN: %not %acir_opt %t/for-iter-arg-live.mlir 2>&1 | %FileCheck %s --check-prefix=ITER-LIVE
// RUN: %not %acir_opt %t/if-path-live.mlir 2>&1 | %FileCheck %s --check-prefix=IF-LIVE
// RUN: %not %acir_opt %t/while-iter-arg-live.mlir 2>&1 | %FileCheck %s --check-prefix=WHILE-LIVE
// RUN: %not %acir_opt %t/malformed-scf.mlir 2>&1 | %FileCheck %s --check-prefix=MALFORMED

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
// PROGRESS: every scf.while backedge must suspend or prove bounded progress

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

//--- unreachable-suspension.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M() parameters {} graph {
    ac.process @p kind "control" {
      %true = arith.constant true
      %false = arith.constant false
      scf.while : () -> () {
        scf.condition(%true)
      } do {
        scf.if %false {
          ac.wait_until %true
        }
        scf.yield
      }
      ac.yield_sim
    }
    ac.return
  }
}
// BACKEDGE: every scf.while backedge must suspend or prove bounded progress

//--- for-iter-arg-live.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M(!ac.resource_token<@r>) parameters {} graph {
  ^bb0(%token : !ac.resource_token<@r>):
    ac.process @p kind "control" captures(%token : !ac.resource_token<@r>) {
    ^bb0(%captured : !ac.resource_token<@r>):
      %lb = arith.constant 0 : index
      %ub = arith.constant 4 : index
      %step = arith.constant 1 : index
      %true = arith.constant true
      %result = scf.for %i = %lb to %ub step %step
          iter_args(%iter = %captured) -> (!ac.resource_token<@r>) {
        ac.wait_until %true
        scf.yield %iter : !ac.resource_token<@r>
      }
      ac.yield_sim
    }
    ac.return
  }
}
// ITER-LIVE: cannot remain live across suspension

//--- if-path-live.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M(!ac.resource_token<@r>, !ac.resource_token<@r>, i1)
      parameters {} graph {
  ^bb0(%token : !ac.resource_token<@r>, %worker_token : !ac.resource_token<@r>,
       %condition : i1):
    ac.process @worker kind "workload"
        captures(%worker_token : !ac.resource_token<@r>) {
    ^bb0(%value : !ac.resource_token<@r>):
      ac.yield_sim
    }
    ac.process @p kind "control"
        captures(%token, %condition : !ac.resource_token<@r>, i1) {
    ^bb0(%captured : !ac.resource_token<@r>, %branch : i1):
      %delay = arith.constant 1 : i64
      scf.if %branch {
        ac.wait_until %branch
        ac.schedule @worker %captured after %delay : !ac.resource_token<@r>
      }
      ac.yield_sim
    }
    ac.return
  }
}
// IF-LIVE: cannot remain live across suspension

//--- while-iter-arg-live.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @M(!ac.resource_token<@r>, i1) parameters {} graph {
  ^bb0(%token : !ac.resource_token<@r>, %condition : i1):
    ac.process @p kind "control"
        captures(%token, %condition : !ac.resource_token<@r>, i1) {
    ^bb0(%captured : !ac.resource_token<@r>, %keep_running : i1):
      %result = scf.while (%iter = %captured)
          : (!ac.resource_token<@r>) -> !ac.resource_token<@r> {
        scf.condition(%keep_running) %iter : !ac.resource_token<@r>
      } do {
      ^bb0(%after : !ac.resource_token<@r>):
        ac.wait_until %keep_running
        scf.yield %after : !ac.resource_token<@r>
      }
      ac.yield_sim
    }
    ac.return
  }
}
// WHILE-LIVE: cannot remain live across suspension

//--- malformed-scf.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  "ac.module"() <{sym_name = "M", function_type = () -> (), static_params = {}}> ({
    "ac.process"() <{kind = "control", sym_name = "p"}> ({
      %true = "arith.constant"() <{value = true}> : () -> i1
      "scf.if"(%true) ({
        "ac.wait_until"(%true) : (i1) -> ()
      }) : (i1) -> ()
      "ac.yield_sim"() : () -> ()
    }) : () -> ()
    "ac.return"() : () -> ()
  }) : () -> ()
}
// MALFORMED: malformed scf.if region must terminate with scf.yield
