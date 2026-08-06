// RUN: %split_file %s %t
// RUN: %acir_opt_public --verify-each=false --pass-pipeline='builtin.module(ac-freeze-topology)' %t/compute-body.mlir -o %t/compute-body.frozen
// RUN: %not %acir_opt_public --ac-lower-to-acsim --ac-binding-profile=fast --ac-binding-target=arm64-apple-darwin %t/compute-body.frozen -o %t/compute-body.out 2>&1 | %FileCheck %s --check-prefix=COMPUTE
// RUN: test ! -s %t/compute-body.out

// Process bodies outside the yield-only v0.1 form are rejected atomically
// with ACLOWER-PROCESS-STATE: the diagnostic names the process, no partial
// ACSim is emitted, and the output file stays empty.

//--- compute-body.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.system @soc root @Top as "root" tick 0 "cycle"
      workload @Top::@workload seed {kind = "fixed", value = 7 : i64}
      instrumentation [] results {id = "default", format = "json"} selected true
  ac.module @Top() parameters {} graph {
    ac.process @workload kind "workload" {
      %zero = arith.constant 0 : i32
      ac.yield_sim
    }
    ac.return
  }
}

// COMPUTE: error: ACLOWER-PROCESS-STATE: ac-lower-to-acsim v0.1 lowers exactly the yield-only process form planned by ProcessStatePlan; process '@workload' has an unsupported body
