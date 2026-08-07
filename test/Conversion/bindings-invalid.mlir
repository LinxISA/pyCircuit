// RUN: %split_file %s %t
// RUN: %acir_opt --verify-each=false --pass-pipeline='builtin.module(ac-freeze-topology)' %t/extern.mlir -o %t/extern.frozen
// RUN: %not %acir_opt --ac-lower-to-acsim --ac-binding-registry=%S/Inputs/pure-fast.json --ac-binding-profile=fast --ac-binding-target=arm64-apple-darwin %t/extern.frozen -o %t/pure.out 2>&1 | %FileCheck %s --check-prefix=PURE
// RUN: test ! -s %t/pure.out
// RUN: %acir_opt --verify-each=false --pass-pipeline='builtin.module(ac-freeze-topology)' %t/sort-order.mlir -o %t/sort-order.frozen
// RUN: %not %acir_opt --ac-lower-to-acsim --ac-binding-profile=fast --ac-binding-target=arm64-apple-darwin %t/sort-order.frozen -o %t/sort.out 2>&1 | %FileCheck %s --check-prefix=SORT
// RUN: test ! -s %t/sort.out
// RUN: %acir_opt --verify-each=false --pass-pipeline='builtin.module(ac-freeze-topology)' %t/heterogeneous-array.mlir -o %t/heterogeneous-array.frozen
// RUN: %not %acir_opt --ac-lower-to-acsim --ac-binding-registry=%S/Inputs/stateful-fast.json --ac-binding-profile=fast --ac-binding-target=arm64-apple-darwin %t/heterogeneous-array.frozen -o %t/array.out 2>&1 | %FileCheck %s --check-prefix=ARRAY
// RUN: test ! -s %t/array.out
// RUN: %acir_opt --verify-each=false --pass-pipeline='builtin.module(ac-freeze-topology)' %t/time-domain.mlir -o %t/time-domain.frozen
// RUN: %not %acir_opt --ac-lower-to-acsim --ac-binding-profile=fast --ac-binding-target=arm64-apple-darwin %t/time-domain.frozen -o %t/td.out 2>&1 | %FileCheck %s --check-prefix=ACERR
// RUN: test ! -s %t/td.out
// RUN: %not %acir_opt --ac-lower-to-acsim --ac-binding-registry=%S/Inputs/bad-registry-structure.json --ac-binding-profile=fast --ac-binding-target=arm64-apple-darwin %t/extern.frozen 2>&1 | %FileCheck %s --check-prefix=REGISTRY
// RUN: %not %acir_opt --ac-lower-to-acsim --ac-binding-registry=%S/Inputs/bad-metadata-empty-work.json --ac-binding-profile=fast --ac-binding-target=arm64-apple-darwin %t/extern.frozen 2>&1 | %FileCheck %s --check-prefix=METADATA

// Negative lowering coverage: ownership, array-specialization,
// stage-boundary, and registry contract rejections all fail atomically with
// their exact ACLOWER-* codes.

//--- extern.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.system @soc root @Top as "root" tick 0 "cycle"
      workload @Top::@workload seed {kind = "fixed", value = 0 : i64}
      instrumentation [] results {id = "default", format = "json"} selected true
  ac.module.extern @Leaf : () -> i32 parameters {width = 8 : i64}
      implementation {registry = "cpp", name = "Leaf"}
  ac.module @Top() parameters {} graph {
    %leaf = ac.instance @leaf of @Leaf() static {width = 8 : i64}
        id "leaf" path "leaf" : () -> i32
    ac.process @workload kind "workload" { ac.yield_sim }
    ac.return
  }
}

//--- sort-order.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.system @soc root @Top as "root" tick 0 "cycle"
      workload @Top::@workload seed {kind = "fixed", value = 0 : i64}
      instrumentation [] results {id = "default", format = "json"} selected true
  ac.module @Top() parameters {} graph {
    ac.instance @zed of @Zebra() static {} id "zed" path "zed" : () -> ()
    ac.process @workload kind "workload" { ac.yield_sim }
    ac.return
  }
  ac.module @Zebra() parameters {} graph {
    ac.return
  }
}

//--- heterogeneous-array.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.system @soc root @Top as "root" tick 0 "cycle"
      workload @Top::@workload seed {kind = "fixed", value = 0 : i64}
      instrumentation [] results {id = "default", format = "json"} selected true
  ac.module.extern @Leaf : () -> i32 parameters {width = 8 : i64}
      implementation {registry = "cpp", name = "Leaf"}
  ac.module @Top() parameters {} graph {
    %cells:2 = ac.array @cells of @Leaf shape [2]()
        static [{width = 8 : i64}, {width = 16 : i64}]
        id "cells" path "cells" : () -> (i32, i32)
    ac.process @workload kind "workload" { ac.yield_sim }
    ac.return
  }
}

//--- time-domain.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.system @soc root @Top as "root" tick 0 "cycle"
      workload @Top::@workload seed {kind = "fixed", value = 0 : i64}
      instrumentation [] results {id = "default", format = "json"} selected true
  ac.module @Top() parameters {} graph {
    ac.time_domain @global period 1 phase 0 scale 1
    ac.process @workload kind "workload" { ac.yield_sim }
    ac.return
  }
}

// PURE: error: ACLOWER-OWNERSHIP: ownership placement of external declaration '@Leaf' requires a stateful binding, but binding 'Leaf' has effect 'pure'
// SORT: error: ACLOWER-OWNERSHIP: canonical ACSim declares modules in strictly symbol-sorted order, so module '@Top' cannot instantiate '@Zebra'; rename so every instantiated module sorts before its parent
// ARRAY: error: ACLOWER-ARRAY: differently specialized array elements are outside the v0.1 lowering stage; lower them as ordered named members instead
// ACERR: error: ACLOWER-UNSUPPORTED-CONSTRUCT: operation 'ac.time_domain' has no ACSim realization in the v0.1 lowering stage {{.*}}
// REGISTRY: error: ACLOWER-BINDING-REGISTRY: registry must contain exactly candidates and requests arrays
// METADATA: error: ACLOWER-BINDING-METADATA: binding effect requires exact executable entry points
