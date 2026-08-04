// RUN: %split_file %s %t
// RUN: %acir_opt --verify-each=false --pass-pipeline='builtin.module(ac-canonicalize-model,ac-freeze-topology)' --emit-bytecode -o %t/a.mlirbc %t/a.mlir
// RUN: %acir_opt --verify-each=false --pass-pipeline='builtin.module(ac-canonicalize-model,ac-freeze-topology)' --emit-bytecode -o %t/b.mlirbc %t/b.mlir
// RUN: cmp %t/a.mlirbc %t/b.mlirbc
// RUN: sha256sum %t/a.mlirbc | cut -d ' ' -f 1 > %t/a.sha256
// RUN: sha256sum %t/b.mlirbc | cut -d ' ' -f 1 > %t/b.sha256
// RUN: cmp %t/a.sha256 %t/b.sha256
// RUN: %acir_opt %t/a.mlirbc | %FileCheck %s --check-prefix=CANONICAL

//--- a.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @Z() parameters {} graph { ac.return }
  ac.module @Top() parameters {} graph {
    ac.instance @z of @Z() static {} id "z" path "z" : () -> ()
    ac.instance @a of @A() static {} id "a" path "a" : () -> ()
    ac.stat @requests kind "counter"
    ac.process @workload kind "workload" { ac.yield_sim }
    ac.return
  }
  ac.system @soc root @Top as "root" tick 0 "cycle"
      workload @Top::@workload seed {kind = "fixed", value = 7 : i64}
      instrumentation [] results {id = "default", format = "json"} selected true
  ac.module @A() parameters {} graph { ac.return }
}

//--- b.mlir
builtin.module attributes {ac.contract_epoch = "0.1"} {
  ac.module @A() parameters {} graph { ac.return }
  ac.system @soc root @Top as "root" tick 0 "cycle"
      workload @Top::@workload seed {kind = "fixed", value = 7 : i64}
      instrumentation [] results {id = "default", format = "json"} selected true
  ac.module @Top() parameters {} graph {
    ac.process @workload kind "workload" { ac.yield_sim }
    ac.stat @requests kind "counter"
    ac.instance @a of @A() static {} id "a" path "a" : () -> ()
    ac.instance @z of @Z() static {} id "z" path "z" : () -> ()
    ac.return
  }
  ac.module @Z() parameters {} graph { ac.return }
}

// CANONICAL: ac.system @soc
// CANONICAL: ac.module @A
// CANONICAL: ac.module @Top
// CANONICAL: ac.instance @a
// CANONICAL-NEXT: ac.instance @z
// CANONICAL: ac.process @workload
// CANONICAL: ac.stat @requests
// CANONICAL: ac.module @Z
